"""Relocate Mach-O and PE dependency closures using native platform adapters."""

from __future__ import annotations

import os
import re
import shutil
import struct
import subprocess
from pathlib import Path


def command(arguments: list[str]) -> str:
    """Run a native inspection or relocation tool, propagating its failure."""
    return subprocess.run(arguments, check=True, capture_output=True, text=True).stdout


def macho_dependencies(path: Path) -> list[str]:
    """Read load commands, excluding the first line containing the inspected filename."""
    return [line.strip().split(" (", 1)[0] for line in command(["otool", "-L", str(path)]).splitlines()[1:]]


def macho_rpaths(path: Path) -> list[str]:
    """Read LC_RPATH values without depending on localized explanatory text."""
    return re.findall(r"cmd LC_RPATH\n.*?\n\s*path (.*?) \(offset", command(["otool", "-l", str(path)]))


def bundle_macho(source: Path, destination: Path, root: Path) -> dict[str, Path]:
    """Copy a Mach-O closure, rewrite every non-system edge and ad-hoc sign changed images.

    System frameworks and /usr/lib remain operating-system dependencies. Unknown rpaths,
    conflicting install names and unresolved dependencies stop packaging.
    """
    libraries: dict[str, Path] = {}
    pending = [(source.resolve(), destination)]
    images: list[Path] = []
    executable = source.resolve().parent
    while pending:
        original, target = pending.pop()
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(original, target)
        target.chmod(target.stat().st_mode | 0o200)
        images.append(target)
        for name in macho_dependencies(original):
            if name.startswith(("/usr/lib/", "/System/Library/")):
                continue
            # dylibs list their own LC_ID_DYLIB as well as actual dependencies.
            if Path(name).name == original.name:
                continue
            expanded = name.replace("@loader_path", str(original.parent)).replace("@executable_path", str(executable))
            candidates = [Path(expanded)]
            if name.startswith("@rpath/"):
                candidates = [
                    Path(
                        value.replace("@loader_path", str(original.parent)).replace("@executable_path", str(executable))
                    )
                    / name.removeprefix("@rpath/")
                    for value in [*macho_rpaths(original), *macho_rpaths(source)]
                ]
            dependency = next((path.resolve() for path in candidates if path.is_file()), None)
            if dependency is None:
                raise ValueError(f"Unresolved Mach-O dependency: {original}: {name}")
            filename = dependency.name
            if filename in libraries and libraries[filename] != dependency:
                raise ValueError(f"Conflicting Mach-O dependency basename: {filename}")
            copied = root / "lib" / filename
            if filename not in libraries:
                libraries[filename] = dependency
                pending.append((dependency, copied))
            relative = os.path.relpath(copied, target.parent).replace(os.sep, "/")
            command(["install_name_tool", "-change", name, "@loader_path/" + relative, str(target)])
        if target.suffix == ".dylib":
            command(["install_name_tool", "-id", "@rpath/" + target.name, str(target)])
    for image in reversed(images):
        command(["codesign", "--force", "--sign", "-", str(image)])
    return libraries


def pe_imports(path: Path) -> tuple[int, list[str]]:
    """Read PE import and delay-import names, validating RVAs against file-backed sections."""
    data = path.read_bytes()

    def number(offset: int, form: str = "<I") -> int:
        return struct.unpack_from(form, data, offset)[0]

    if data[:2] != b"MZ":
        raise ValueError(f"Not a PE image: {path}")
    header = number(0x3C)
    if data[header : header + 4] != b"PE\0\0":
        raise ValueError(f"Invalid PE signature: {path}")
    machine = number(header + 4, "<H")
    optional = header + 24
    magic = number(optional, "<H")
    if magic not in (0x10B, 0x20B):
        raise ValueError(f"Unsupported PE optional header: {path}")
    directories = optional + (112 if magic == 0x20B else 96)
    directory_count = number(directories - 4)
    sections = optional + number(header + 20, "<H")

    def offset(rva: int) -> int:
        for index in range(number(header + 6, "<H")):
            section = sections + 40 * index
            address, size, raw = number(section + 12), number(section + 16), number(section + 20)
            if address <= rva < address + size and raw + rva - address < len(data):
                return raw + rva - address
        raise ValueError(f"PE RVA outside file-backed sections: {path}")

    names = []
    for directory, stride, name_field in [(1, 20, 12), (13, 32, 4)]:
        if directory >= directory_count:
            continue
        rva, size = number(directories + directory * 8), number(directories + directory * 8 + 4)
        if not rva:
            continue
        for descriptor in range(0, size, stride):
            item = offset(rva + descriptor)
            if not any(data[item : item + stride]):
                break
            if directory == 13 and not number(item) & 1:
                raise ValueError("PE delay imports must use RVAs.")
            start = offset(number(item + name_field))
            end = data.find(b"\0", start, start + 260)
            if end == -1:
                raise ValueError("Unterminated PE import name.")
            name = data[start:end].decode("ascii")
            if not re.fullmatch(r"[A-Za-z0-9_.+-]+\.dll", name, re.IGNORECASE):
                raise ValueError(f"Invalid PE dependency basename: {name}")
            names.append(name)
    return machine, names


def bundle_pe(source: Path, destination: Path, root: Path) -> dict[str, Path]:
    """Bundle architecture-matching non-system DLLs beside their owning executable/library.

    Windows system components remain system requirements. VC runtime DLLs are bundled;
    xmake package directories are searched only when loader search directories do not resolve.
    """
    libraries: dict[str, Path] = {}
    machine, _ = pe_imports(source)
    search = [source.parent, *[Path(value) for value in os.environ.get("PATH", "").split(os.pathsep) if value]]
    cache = Path(os.environ.get("XMAKE_GLOBALDIR", str(Path.home() / ".xmake"))) / "packages"
    installed = list(cache.rglob("*.dll")) if cache.is_dir() else []
    system = Path(os.environ.get("SYSTEMROOT", "C:/Windows")) / "System32"
    pending = [(source, destination)]
    while pending:
        original, target = pending.pop()
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(original, target)
        image_machine, names = pe_imports(original)
        if image_machine != machine:
            raise ValueError(f"PE architecture mismatch: {original}")
        for name in names:
            key = name.lower()
            if key in libraries or key.startswith(("api-ms-", "ext-ms-")):
                continue
            runtime = key.startswith(("vcruntime", "msvcp", "concrt"))
            if (system / name).is_file() and not runtime:
                continue
            candidates = [original.parent / name, *[directory / name for directory in search]]
            if runtime:
                candidates.append(system / name)
            candidates.extend(path for path in installed if path.name.lower() == key)
            dependency = next(
                (path.resolve() for path in candidates if path.is_file() and pe_imports(path)[0] == machine), None
            )
            if dependency is None:
                raise ValueError(f"Unresolved PE dependency: {original}: {name}")
            libraries[key] = dependency
            pending.append((dependency, destination.parent / name))
    return libraries
