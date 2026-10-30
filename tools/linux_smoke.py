"""Exercise an installed Linux release using only Python's standard library (3.10+).

Run inside a clean distribution container. No compiler, development libraries, source-tree
runtime assets or network services are needed by the installed application.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import sqlite3
import subprocess
import tarfile
import tempfile
import time
from pathlib import Path, PurePosixPath


def unpack(archive: Path, destination: Path) -> None:
    """Extract regular package members only, rejecting traversal, links and special files."""
    with tarfile.open(archive) as source:
        for member in source.getmembers():
            path = PurePosixPath(member.name)
            if path.is_absolute() or ".." in path.parts or not (member.isdir() or member.isfile()):
                raise ValueError(f"Unsafe archive member: {member.name}")
            target = destination.joinpath(*path.parts)
            if member.isdir():
                target.mkdir(parents=True, exist_ok=True)
            else:
                target.parent.mkdir(parents=True, exist_ok=True)
                with source.extractfile(member) as content, target.open("wb") as output:
                    shutil.copyfileobj(content, output)
                target.chmod(member.mode & 0o777)


def verify_checksums(directory: Path) -> None:
    """Verify every manifest entry before executing any code from the downloaded artifact."""
    verified = set()
    for line in (directory / "SHA256SUMS").read_text(encoding="utf-8").splitlines():
        expected, name = line.split("  ", 1)
        if Path(name).name != name or name in verified:
            raise ValueError(f"Unsafe checksum filename: {name}")
        digest = hashlib.sha256()
        with (directory / name).open("rb") as source:
            for chunk in iter(lambda: source.read(1024 * 1024), b""):
                digest.update(chunk)
        if digest.hexdigest() != expected:
            raise ValueError(f"Checksum mismatch: {name}")
        verified.add(name)
    required = {"plugins-v1.yaml", *(path.name for path in directory.glob("*.tar.gz"))}
    if len(required) != 3 or verified != required:
        raise ValueError("Checksum manifest must cover exactly the host, plugin and catalog")


def terminal_flow(binary: Path, environment: dict[str, str], rows: int, columns: int) -> None:
    """Navigate the real TUI, save a fixture word and quit, with bounded waits and cleanup."""
    # Keep this tool importable for packaging regressions on Windows as well.
    import fcntl
    import pty
    import select
    import struct
    import termios

    master, slave = pty.openpty()
    fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", rows, columns, 0, 0))
    transcript = bytearray()
    child = subprocess.Popen([str(binary)], cwd=binary.parent, env=environment, stdin=slave, stdout=slave, stderr=slave)
    os.close(slave)

    def wait_for(marker: str, start: int = 0) -> None:
        deadline = time.monotonic() + 30
        while time.monotonic() < deadline:
            if marker.encode() in transcript[start:]:
                return
            if select.select([master], [], [], 0.1)[0]:
                try:
                    transcript.extend(os.read(master, 65536))
                except OSError:
                    break
            if child.poll() is not None:
                break
        raise RuntimeError(f"TUI did not display {marker!r}: {transcript[-12000:]!r}")

    def send(keys: bytes, marker: str) -> None:
        start = len(transcript)
        os.write(master, keys)
        wait_for(marker, start)

    try:
        wait_for("日本語")
        if b"\x1b[?1049h" not in transcript:
            raise RuntimeError("TUI did not enter the alternate screen")
        send(b"\r", "音声設定")
        send(b"\r", "語彙検索")
        send("猫".encode() + b"\r", "feline")
        send(b"\r", "例文")
        send(b"\x13", "Saved")
        send(b"\x1b", "enter open")
        send(b"\x1b", "音声設定")
        send(b"\x1b", "Get plugins")
        os.write(master, b"q")
        wait_for("\x1b[?1049l")
        if child.wait(timeout=10) != 0:
            raise RuntimeError("TUI exited unsuccessfully")
    finally:
        if child.poll() is None:
            child.terminate()
            try:
                child.wait(timeout=5)
            except subprocess.TimeoutExpired:
                child.kill()
                child.wait()
        os.close(master)


def smoke(artifacts: Path, platform: str, fixture: Path) -> None:
    """Validate relocation, offline installation, UI navigation and persistent vocabulary."""
    verify_checksums(artifacts)
    catalog = artifacts / "plugins-v1.yaml"
    metadata = json.loads(catalog.read_text(encoding="utf-8"))
    plugin = next(item for item in metadata["plugins"] if item["id"] == "japanese")
    artifact = plugin["artifacts"][platform]
    version = plugin["host_version"]
    with tempfile.TemporaryDirectory(prefix="pawingo distro smoke ") as temporary:
        root = Path(temporary)
        host = root / "relocated host"
        library = root / "relocated plugin"
        plugin_archive = artifacts / f"pawingo-japanese-{plugin['version']}-{platform}.tar.gz"
        unpack(artifacts / f"pawingo-{version}-{platform}.tar.gz", host)
        unpack(plugin_archive, library)
        environment = {key: value for key, value in os.environ.items() if not key.startswith(("LD_", "PAWINGO_"))}
        environment.update(
            XDG_DATA_HOME=str(root / "data"),
            XDG_CONFIG_HOME=str(root / "config"),
            PAWINGO_PLUGIN_DIR=str(root / "empty plugins"),
            PAWINGO_JMDICT=str(fixture.resolve()),
            TERM="xterm-256color",
            LANG="C.UTF-8",
        )
        binary = host / "bin/pawingo"

        def run(*arguments: str) -> str:
            return subprocess.run(
                [str(binary), *arguments],
                cwd=root,
                env=environment,
                check=True,
                capture_output=True,
                text=True,
                encoding="utf-8",
                timeout=60,
            ).stdout

        identity = json.loads(run("--build-info"))
        if identity["platform"] != platform:
            raise ValueError(f"Wrong platform: {identity['platform']}")
        if run("--check-plugin", str(library / artifact["entry"])).strip() != "Japanese":
            raise ValueError("Packaged Japanese plugin did not load")
        run("--install-plugin", "japanese", str(plugin_archive), str(catalog))
        inventory = root / "data/pawingo/plugins/installed.yaml"
        if not inventory.is_file():
            raise RuntimeError("Offline installation did not persist the plugin inventory")
        print("Relocated host, native plugin and offline installation passed.", flush=True)
        for rows, columns in [(24, 80), (40, 120)]:
            terminal_flow(binary, environment, rows, columns)
            print(f"TUI navigation, Escape and alternate screen passed at {columns}x{rows}.", flush=True)
        with sqlite3.connect(root / "data/pawingo/pawingo.sqlite3") as database:
            words = database.execute("SELECT expression, reading FROM vocabulary WHERE language = 'ja'").fetchall()
            if ("猫", "ねこ") not in words:
                raise RuntimeError(f"Vocabulary did not survive application restart: {words}")
            if database.execute("PRAGMA integrity_check").fetchone() != ("ok",):
                raise RuntimeError("SQLite integrity check failed")
        print("Persistent vocabulary and SQLite integrity passed.", flush=True)


def main() -> None:
    """Run against one architecture's downloaded native artifact directory."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--artifacts", type=Path, required=True)
    parser.add_argument("--platform", choices=["linux-x86_64-gnu", "linux-aarch64-gnu"], required=True)
    parser.add_argument("--dictionary", type=Path, required=True)
    options = parser.parse_args()
    smoke(options.artifacts.resolve(), options.platform, options.dictionary.resolve())


if __name__ == "__main__":
    main()
