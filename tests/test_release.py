"""Release-tool regressions for relocating native runtime dependencies safely."""

import json
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from tools.release import bundle, info


class ReleasePackagingTests(unittest.TestCase):
    """Ensure dependency discovery cannot write outside the staged package."""

    def test_system_loader_absolute_path_is_never_bundled(self) -> None:
        """A loader entry is a system requirement, including ldd's absolute-name form."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
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

            with patch("tools.release.run", side_effect=fake_run), patch("tools.release.copy_notices"):
                bundle(binary, stage / "bin/pawingo", stage)
            self.assertEqual((stage / "lib/libhelper.so.1").read_bytes(), library.read_bytes())
            self.assertEqual(sorted(path.name for path in (stage / "lib").iterdir()), ["libhelper.so.1"])

    def test_absolute_non_system_dependency_is_rejected(self) -> None:
        """Unknown absolute DT_NEEDED names cannot escape the package library directory."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            with (
                patch("tools.release.run", return_value="/outside/libhelper.so => /outside/libhelper.so (0x1)"),
                self.assertRaisesRegex(ValueError, "Absolute DT_NEEDED"),
            ):
                bundle(root / "source", root / "stage/bin/pawingo", root / "stage")
            self.assertFalse((root / "stage").exists())

    def test_development_identity_cannot_be_published_accidentally(self) -> None:
        """Development packaging needs an explicit opt-in even on a supported platform."""
        metadata = {"platform": "linux-x86_64-gnu", "release_build": "development"}
        with patch("tools.release.run", return_value=json.dumps(metadata)):
            with self.assertRaisesRegex(ValueError, "PAWINGO_RELEASE_BUILD"):
                info(Path("pawingo"), allow_development=False)
            self.assertEqual(info(Path("pawingo"), allow_development=True), metadata)


if __name__ == "__main__":
    unittest.main()
