"""Release-tool regressions for relocating native runtime dependencies safely."""

import io
import json
import struct
import tarfile
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

from tools.release import bundle_elf, checksum, info, native_names, package_core, write_yaml
from tools.release_collect import collect
from tools.release_native import bundle_macho, bundle_pe, pe_imports


class ReleasePackagingTests(unittest.TestCase):
    """Ensure dependency discovery cannot write outside the staged package."""

    def test_system_loader_absolute_path_is_never_bundled(self) -> None:
        """A loader entry is a system requirement, including ldd's absolute-name form."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            binary = root / "source"
            binary.write_bytes(b"trusted binary fixture")
            library = root / "libhelper.so.1"
            library.write_bytes(b"dependency fixture")
            stage = root / "stage"

            def fake_run(arguments: list[str]) -> str:
                if arguments[0] == "ldd":
                    return (
                        f"libhelper.so.1 => {library} (0x1)\n"
                        "/lib64/ld-linux-x86-64.so.2 => /usr/lib/ld-linux-x86-64.so.2 (0x2)"
                    )
                return ""

            with (
                patch("tools.release.run", side_effect=fake_run),
                patch("tools.release.copy_notices"),
                patch("tools.release.os.confstr", return_value="glibc fixture", create=True),
            ):
                bundle_elf(binary, stage / "bin/pawingo", stage)
            self.assertEqual((stage / "lib/libhelper.so.1").read_bytes(), library.read_bytes())
            self.assertEqual(sorted(path.name for path in (stage / "lib").iterdir()), ["libhelper.so.1"])

    def test_absolute_non_system_dependency_is_rejected(self) -> None:
        """Unknown absolute DT_NEEDED names cannot escape the package library directory."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            with (
                patch("tools.release.run", return_value="/outside/libhelper.so => /outside/libhelper.so (0x1)"),
                self.assertRaisesRegex(ValueError, "Absolute DT_NEEDED"),
            ):
                bundle_elf(root / "source", root / "stage/bin/pawingo", root / "stage")
            self.assertFalse((root / "stage").exists())

    def test_release_manifest_excludes_unrelated_archives(self) -> None:
        """Only the current host and catalog-pinned plugin enter the release upload manifest."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            plugin = root / "pawingo-japanese-0.1.0-linux-x86_64-gnu.tar.gz"
            plugin.write_bytes(b"plugin fixture")
            (root / "unrelated-fixture.tar.gz").write_bytes(b"unrelated fixture")
            catalog = root / "plugins-v1.yaml"
            catalog.write_text(
                json.dumps(
                    {
                        "plugins": [
                            {
                                "artifacts": {
                                    "linux-x86_64-gnu": {
                                        "url": "https://example.test/" + plugin.name,
                                        "sha256": checksum(plugin),
                                        "size_bytes": plugin.stat().st_size,
                                        "abi": "fixture-sdk",
                                    }
                                }
                            }
                        ]
                    }
                )
            )
            metadata = {
                "platform": "linux-x86_64-gnu",
                "version": "0.1.0",
                "abi": "fixture-sdk",
                "catalog_sha256": checksum(catalog),
            }
            options = SimpleNamespace(output=root, binary=root / "pawingo", allow_development=True)
            with (
                patch("tools.release.bundle"),
                patch(
                    "tools.release.archive", side_effect=lambda _directory, target: target.write_bytes(b"core fixture")
                ),
                patch("tools.release.info", return_value=metadata),
                patch("tools.release.run"),
            ):
                package_core(options, metadata)
            names = {line.split("  ", 1)[1] for line in (root / "SHA256SUMS").read_text().splitlines()}
            self.assertEqual(names, {"pawingo-0.1.0-linux-x86_64-gnu.tar.gz", plugin.name, catalog.name})

    def test_native_entries_follow_the_compiled_platform(self) -> None:
        self.assertEqual(native_names("windows-aarch64"), ("pawingo.exe", "lib/japanese.dll"))
        self.assertEqual(native_names("macos-x86_64"), ("pawingo", "lib/libjapanese.dylib"))
        self.assertEqual(native_names("linux-aarch64-gnu"), ("pawingo", "lib/libjapanese.so"))

    def test_pe_imports_include_delay_loaded_dependencies(self) -> None:
        """Both import tables must contribute to the bundled Windows DLL closure."""
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "fixture.exe"
            data = bytearray(1024)
            data[:2] = b"MZ"
            struct.pack_into("<I", data, 0x3C, 0x80)
            data[0x80:0x84] = b"PE\0\0"
            struct.pack_into("<HH", data, 0x84, 0xAA64, 1)
            struct.pack_into("<H", data, 0x94, 240)
            struct.pack_into("<H", data, 0x98, 0x20B)
            struct.pack_into("<I", data, 0x104, 16)
            struct.pack_into("<II", data, 0x110, 0x1000, 40)
            struct.pack_into("<II", data, 0x170, 0x10A0, 64)
            struct.pack_into("<III", data, 0x194, 0x1000, 512, 512)
            struct.pack_into("<I", data, 0x20C, 0x1080)
            data[0x280:0x28D] = b"kernel32.dll\0"
            struct.pack_into("<II", data, 0x2A0, 1, 0x10E0)
            data[0x2E0:0x2EB] = b"helper.dll\0"
            path.write_bytes(data)
            self.assertEqual(pe_imports(path), (0xAA64, ["kernel32.dll", "helper.dll"]))
            struct.pack_into("<I", data, 0x20C, 0xFFFF)
            path.write_bytes(data)
            with self.assertRaisesRegex(ValueError, "RVA"):
                pe_imports(path)

    def test_pe_closure_is_colocated_and_checks_architecture(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            source = root / "host.exe"
            source.write_bytes(b"host")
            (root / "helper.dll").write_bytes(b"helper")
            (root / "leaf.dll").write_bytes(b"leaf")
            stage = root / "stage"

            def imports(path: Path) -> tuple[int, list[str]]:
                return 0x8664, {"host.exe": ["helper.dll"], "helper.dll": ["leaf.dll"]}.get(path.name, [])

            with patch("tools.release_native.pe_imports", side_effect=imports):
                bundle_pe(source, stage / "bin/host.exe", stage)
            self.assertEqual({path.name for path in (stage / "bin").iterdir()}, {"host.exe", "helper.dll", "leaf.dll"})
            with (
                patch("tools.release_native.pe_imports", side_effect=lambda path: (0x8664, ["missing.dll"])),
                self.assertRaisesRegex(ValueError, "Unresolved PE"),
            ):
                bundle_pe(source, stage / "other/host.exe", stage)

    def test_pe_rejects_a_dependency_for_another_architecture(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            host = root / "host.exe"
            host.write_bytes(b"host")
            (root / "helper.dll").write_bytes(b"wrong architecture")
            with (
                patch(
                    "tools.release_native.pe_imports",
                    side_effect=lambda path: (0xAA64, ["helper.dll"]) if path == host else (0x8664, []),
                ),
                self.assertRaisesRegex(ValueError, "Unresolved PE"),
            ):
                bundle_pe(host, root / "stage/bin/host.exe", root / "stage")

    def test_macho_dependency_edges_are_relative_and_signed(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            host = root / "host"
            library = root / "helper.dylib"
            host.write_bytes(b"host")
            library.write_bytes(b"helper")
            stage = root / "stage"
            with (
                patch(
                    "tools.release_native.macho_dependencies",
                    side_effect=lambda path: [str(library)] if path == host else [],
                ),
                patch("tools.release_native.command", return_value="") as native_command,
            ):
                bundle_macho(host, stage / "bin/host", stage)
            calls = [call.args[0] for call in native_command.call_args_list]
            self.assertIn(
                [
                    "install_name_tool",
                    "-change",
                    str(library),
                    "@loader_path/../lib/helper.dylib",
                    str(stage / "bin/host"),
                ],
                calls,
            )
            self.assertEqual(sum(call[0] == "codesign" for call in calls), 2)

    def test_matrix_catalog_requires_complete_matching_builds(self) -> None:
        """Aggregation rejects missing platforms, different commits and corrupt artifacts."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            inputs = root / "inputs"
            expected = {"linux-x86_64-gnu", "windows-aarch64"}
            for platform in expected:
                directory = inputs / platform
                directory.mkdir(parents=True)
                plugin = directory / f"pawingo-japanese-0.1.0-{platform}.tar.gz"
                plugin.write_bytes(b"plugin")
                catalog = directory / "plugins-v1.yaml"
                write_yaml(
                    catalog,
                    {
                        "schema": 1,
                        "plugins": [
                            {
                                "id": "japanese",
                                "version": "0.1.0",
                                "host_version": "0.1.0",
                                "artifacts": {
                                    platform: {
                                        "url": "https://example.test/" + plugin.name,
                                        "sha256": checksum(plugin),
                                        "size_bytes": plugin.stat().st_size,
                                        "abi": platform,
                                    }
                                },
                            }
                        ],
                    },
                )
                metadata = {
                    "version": "0.1.0",
                    "release_build": "commit",
                    "platform": platform,
                    "abi": platform,
                    "catalog_sha256": checksum(catalog),
                }
                core = directory / f"pawingo-0.1.0-{platform}.tar.gz"
                with tarfile.open(core, "w:gz") as archive:
                    data = json.dumps(metadata).encode()
                    member = tarfile.TarInfo("build-info.yaml")
                    member.size = len(data)
                    archive.addfile(member, io.BytesIO(data))
                (directory / "SHA256SUMS").write_text(
                    "".join(f"{checksum(file)}  {file.name}\n" for file in [catalog, plugin, core])
                )
            with self.assertRaisesRegex(ValueError, "Missing matrix"):
                collect(inputs, root / "missing", "v0.1.0", "commit", expected | {"macos-aarch64"})
            with self.assertRaisesRegex(ValueError, "identity"):
                collect(inputs, root / "mismatch", "v0.1.0", "different", expected)
            collect(inputs, root / "merged", "v0.1.0", "commit", expected)
            merged = json.loads((root / "merged/plugins-v1.yaml").read_text())
            self.assertEqual(set(merged["plugins"][0]["artifacts"]), expected)
            self.assertEqual(len((root / "merged/SHA256SUMS").read_text().splitlines()), 5)
            plugin.write_bytes(b"corrupt")
            with self.assertRaisesRegex(ValueError, "Checksum"):
                collect(inputs, root / "corrupt", "v0.1.0", "commit", expected)

    def test_development_identity_cannot_be_published_accidentally(self) -> None:
        """Development packaging needs an explicit opt-in even on a supported platform."""
        metadata = {"platform": "linux-x86_64-gnu", "release_build": "development"}
        with patch("tools.release.run", return_value=json.dumps(metadata)):
            with self.assertRaisesRegex(ValueError, "PAWINGO_RELEASE_BUILD"):
                info(Path("pawingo"), allow_development=False)
            self.assertEqual(info(Path("pawingo"), allow_development=True), metadata)


if __name__ == "__main__":
    unittest.main()
