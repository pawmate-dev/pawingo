"""Build checksum-pinned native release archives without third-party Python dependencies.

Run the plugin phase first, rebuild only the host with PAWINGO_CATALOG_SOURCE pointing
at its generated catalog, then run the core phase. Nothing is uploaded by this tool.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tarfile
from pathlib import Path
from typing import Any

# Support both the CLI entry point and unittest imports from the repository root.
if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from tools.release_native import bundle_macho, bundle_pe

PROJECT = Path(__file__).resolve().parents[1]
SUPPORTED_PLATFORMS = {
    "linux-x86_64-gnu",
    "linux-aarch64-gnu",
    "windows-x86_64",
    "windows-aarch64",
    "macos-x86_64",
    "macos-aarch64",
}


def native_names(platform: str) -> tuple[str, str]:
    """Return the host basename and plugin entry for a compiled platform identity."""
    if platform.startswith("windows-"):
        return "pawingo.exe", "lib/japanese.dll"
    if platform.startswith("macos-"):
        return "pawingo", "lib/libjapanese.dylib"
    return "pawingo", "lib/libjapanese.so"


SYSTEM_LIBRARIES = re.compile(r"^(?:ld-linux.*|lib(?:c|m|pthread|dl|rt|resolv|util|anl)\.so(?:\..*)?)$")


def run(arguments: list[str]) -> str:
    """Run a trusted build tool and return stdout; failures stop packaging."""
    return subprocess.run(arguments, check=True, text=True, encoding="utf-8", capture_output=True).stdout.strip()


def checksum(path: Path) -> str:
    """Compute a file's SHA-256 with bounded memory."""
    with path.open("rb") as source:
        return hashlib.file_digest(source, "sha256").hexdigest()


def write_yaml(path: Path, value: object) -> None:
    """Write generated metadata as JSON, a YAML-compatible deterministic serialization."""
    # Match C++ embedded literal newlines: Windows text translation would change the pinned digest.
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8", newline="\n")


def info(binary: Path, allow_development: bool) -> dict[str, Any]:
    """Read the compiled identity; refuse accidental publication of development builds."""
    metadata: dict[str, Any] = json.loads(run([str(binary), "--build-info"]))
    if metadata["platform"] not in SUPPORTED_PLATFORMS:
        raise ValueError("Unsupported release platform: " + metadata["platform"])
    if metadata["release_build"] == "development" and not allow_development:
        raise ValueError("Set PAWINGO_RELEASE_BUILD before building a release.")
    return metadata


def copy_notices(destination: Path, libraries: dict[str, Path]) -> None:
    """Preserve supplied notices and available distro notices for bundled dependencies."""
    destination.mkdir(parents=True, exist_ok=True)
    for source in sorted((PROJECT / "licenses").iterdir()):
        if source.is_file():
            shutil.copy2(source, destination / source.name)
    for relative in ["LICENSE", "NOTICE", "native/fsrs/LICENSE.fsrs", "third_party/voicevox/LICENSE"]:
        source = PROJECT / relative
        shutil.copy2(source, destination / relative.replace("/", "-"))
    packages: set[str] = set()
    if shutil.which("pacman"):
        for source in libraries.values():
            result = subprocess.run(["pacman", "-Qqo", str(source)], text=True, capture_output=True, check=False)
            if result.returncode == 0:
                packages.update(result.stdout.split())
    for package in sorted(packages):
        source = Path("/usr/share/licenses") / package
        if source.is_dir():
            shutil.copytree(source, destination / package, dirs_exist_ok=True, symlinks=False)
    # Xmake may retain source license files outside its installation prefixes.
    cache = Path.home() / ".xmake/cache/packages"
    for package in ["ftxui", "sqlite_orm", "picosha2", "miniaudio", "expat", "yaml-cpp", "libarchive"]:
        for source in cache.glob(f"*/{package[0]}/{package}/*/source/**/LICENSE*"):
            if source.is_file():
                shutil.copy2(source, destination / f"{package}-{source.name}")


def bundle_elf(source: Path, destination: Path, root: Path) -> None:
    """Copy a trusted ELF and its dynamic dependency closure, replacing absolute RPATHs.

    Glibc and the loader remain system requirements. Dependencies are copied as regular
    files named by their required SONAME, so the runtime installer need not accept links.
    """
    libraries: dict[str, Path] = {}
    output = run(["ldd", str(source)])
    if "not found" in output:
        raise ValueError(f"Unresolved dependencies for {source}:\n{output}")
    for line in output.splitlines():
        match = re.match(r"\s*(\S+) => (/\S+) \(", line)
        if not match:
            continue
        name = match[1]
        if SYSTEM_LIBRARIES.match(Path(name).name):
            continue
        if Path(name).name != name:
            raise ValueError(f"Absolute DT_NEEDED cannot be relocated safely: {name}")
        libraries[name] = Path(match[2]).resolve()
    library_dir = root / "lib"
    library_dir.mkdir(parents=True, exist_ok=True)
    for name, path in sorted(libraries.items()):
        target = library_dir / name
        shutil.copy2(path, target)
        run(["patchelf", "--set-rpath", "$ORIGIN", str(target)])
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, destination)
    rpath = "$ORIGIN" if destination.parent == library_dir else "$ORIGIN/../lib"
    run(["patchelf", "--set-rpath", rpath, str(destination)])
    write_yaml(
        root / "dependencies.yaml",
        {
            "system": {"libc": os.confstr("CS_GNU_LIBC_VERSION"), "platform": sys.platform},
            "bundled": [{"soname": name, "sha256": checksum(library_dir / name)} for name in sorted(libraries)],
        },
    )
    copy_notices(root / "licenses", libraries)


def bundle(source: Path, destination: Path, root: Path) -> None:
    """Select the native relocation adapter; every adapter rejects unresolved dependencies."""
    if sys.platform.startswith("linux"):
        bundle_elf(source, destination, root)
        return
    libraries = (bundle_pe if sys.platform == "win32" else bundle_macho)(source, destination, root)
    write_yaml(
        root / "dependencies.yaml",
        {
            "system": {"platform": sys.platform},
            "bundled": [
                {
                    "name": name,
                    "sha256": checksum((destination.parent if sys.platform == "win32" else root / "lib") / name),
                }
                for name in sorted(libraries)
            ],
        },
    )
    copy_notices(root / "licenses", libraries)


def archive(directory: Path, destination: Path) -> None:
    """Archive regular files/directories in stable order with normalized ownership and time."""
    epoch = int(os.environ.get("SOURCE_DATE_EPOCH", "0"))
    with tarfile.open(destination, "w:gz", format=tarfile.PAX_FORMAT) as output:
        for path in sorted(directory.rglob("*")):
            member = output.gettarinfo(str(path), path.relative_to(directory).as_posix())
            member.uid = member.gid = 0
            member.uname = member.gname = ""
            member.mtime = epoch
            if member.isfile():
                with path.open("rb") as content:
                    output.addfile(member, content)
            elif member.isdir():
                output.addfile(member)
            else:
                raise ValueError(f"Unexpected link or special file in package: {path}")


def package_plugin(options: argparse.Namespace, metadata: dict[str, Any]) -> None:
    """Package Japanese first and generate a catalog containing its final archive digest."""
    rows = [item for item in metadata["plugins"] if item["id"] == "japanese"]
    if not rows:
        raise ValueError("The embedded catalog has no Japanese metadata.")
    plugin = max(rows, key=lambda item: tuple(map(int, item["version"].split("."))))
    root = options.output / "plugin-stage"
    if root.exists():
        shutil.rmtree(root)
    root.mkdir(parents=True)
    _, entry = native_names(metadata["platform"])
    bundle(options.library, root / entry, root)
    manifest = {
        "schema": 1,
        **plugin,
        "host_version": metadata["version"],
        "plugin_api": 1,
        "platform": metadata["platform"],
        "abi": metadata["abi"],
        "entry": entry,
    }
    write_yaml(root / "plugin.yaml", manifest)
    filename = f"pawingo-japanese-{plugin['version']}-{metadata['platform']}.tar.gz"
    target = options.output / filename
    archive(root, target)
    artifact = {
        "url": f"https://github.com/{options.repo}/releases/download/{options.tag}/{filename}",
        "sha256": checksum(target),
        "size_bytes": target.stat().st_size,
        "entry": manifest["entry"],
        "abi": metadata["abi"],
    }
    catalog = {
        "schema": 1,
        "plugins": [
            {
                **plugin,
                "host_version": metadata["version"],
                "plugin_api": 1,
                "artifacts": {metadata["platform"]: artifact},
            }
        ],
    }
    write_yaml(options.output / "plugins-v1.yaml", catalog)


def package_core(options: argparse.Namespace, metadata: dict[str, Any]) -> None:
    """Package the host after its catalog has been embedded; produce checksums and smoke-test it."""
    catalog = json.loads((options.output / "plugins-v1.yaml").read_text(encoding="utf-8"))
    artifact = catalog["plugins"][0]["artifacts"][metadata["platform"]]
    if metadata["catalog_sha256"] != checksum(options.output / "plugins-v1.yaml"):
        raise ValueError("Rebuild the host with PAWINGO_CATALOG_SOURCE before packaging it.")
    if artifact["abi"] != metadata["abi"]:
        raise ValueError("The rebuilt host no longer matches the packaged plugin ABI.")
    root = options.output / "core-stage"
    if root.exists():
        shutil.rmtree(root)
    root.mkdir(parents=True)
    binary_name, entry = native_names(metadata["platform"])
    host = root / "bin" / binary_name
    bundle(options.binary, host, root)
    write_yaml(root / "build-info.yaml", metadata)
    target = options.output / f"pawingo-{metadata['version']}-{metadata['platform']}.tar.gz"
    archive(root, target)
    # Run the relocated host and load the actual packaged native library before release upload.
    relocated = info(host, options.allow_development)
    if relocated["abi"] != metadata["abi"]:
        raise ValueError("Relocated host changed build identity.")
    run([str(host), "--check-plugin", str(options.output / "plugin-stage" / entry)])
    plugin_archive = options.output / artifact["url"].rsplit("/", 1)[-1]
    if plugin_archive.stat().st_size != artifact["size_bytes"] or checksum(plugin_archive) != artifact["sha256"]:
        raise ValueError("The plugin archive no longer matches the generated catalog.")
    # The upload manifest contains only artifacts produced and verified by this invocation.
    outputs = [target, plugin_archive, options.output / "plugins-v1.yaml"]
    (options.output / "SHA256SUMS").write_text(
        "".join(f"{checksum(path)}  {path.name}\n" for path in outputs), encoding="utf-8", newline="\n"
    )


def main() -> None:
    """Parse release tooling arguments and write local artifacts only."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("phase", choices=["plugin", "core"])
    parser.add_argument("--output", type=Path, default=PROJECT / "build/release")
    parser.add_argument("--binary", type=Path, default=None)
    parser.add_argument("--library", type=Path, default=None)
    parser.add_argument("--repo", default="pawmate-dev/pawingo")
    parser.add_argument("--tag", required=True)
    parser.add_argument(
        "--allow-development", action="store_true", help="Allow local smoke-test artifacts, never publish these."
    )
    options = parser.parse_args()
    options.output = options.output.resolve()
    if options.binary is None or options.library is None:
        paths = json.loads((PROJECT / "build/release-paths.json").read_text(encoding="utf-8"))
        options.binary = options.binary or Path(paths["binary"])
        options.library = options.library or Path(paths["library"])
    options.binary = options.binary.resolve()
    options.library = options.library.resolve()
    if not re.fullmatch(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+", options.repo):
        parser.error("Invalid GitHub repository.")
    options.output.mkdir(parents=True, exist_ok=True)
    metadata = info(options.binary, options.allow_development)
    if options.tag != f"v{metadata['version']}":
        parser.error("The tag must match the compiled host version.")
    (package_plugin if options.phase == "plugin" else package_core)(options, metadata)


if __name__ == "__main__":
    main()
