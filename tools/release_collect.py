"""Verify native matrix artifacts and assemble one catalog and checksum manifest."""

from __future__ import annotations

import argparse
import json
import re
import shutil
import tarfile
from pathlib import Path

from tools.release import SUPPORTED_PLATFORMS, checksum, write_yaml


def collect(inputs: Path, output: Path, tag: str, build: str, expected: set[str]) -> None:
    """Require one verified pair per expected platform and a common release commit.

    Every input must have a checksum manifest and an exact catalog-pinned plugin. The
    output directory must be empty so retries cannot silently publish unrelated files.
    """
    if output.exists() and any(output.iterdir()):
        raise ValueError("The merged release output directory must be empty.")
    pairs = []
    seen: set[str] = set()
    for catalog_path in sorted(inputs.glob("*/plugins-v1.yaml")):
        directory = catalog_path.parent
        verified = {}
        for line in (directory / "SHA256SUMS").read_text(encoding="utf-8").splitlines():
            match = re.fullmatch(r"([a-f0-9]{64})  ([A-Za-z0-9_.-]+)", line)
            if not match or match[2] in verified:
                raise ValueError("Invalid or duplicate checksum entry.")
            file = directory / match[2]
            if checksum(file) != match[1]:
                raise ValueError(f"Checksum mismatch: {file}")
            verified[match[2]] = file
        if catalog_path.name not in verified:
            raise ValueError("The catalog must be included in its checksum manifest.")
        catalog = json.loads(catalog_path.read_text(encoding="utf-8"))
        if catalog.get("schema") != 1 or len(catalog["plugins"]) != 1:
            raise ValueError("Expected one Japanese plugin catalog per native build.")
        plugin = catalog["plugins"][0]
        if plugin["id"] != "japanese" or len(plugin["artifacts"]) != 1:
            raise ValueError("Expected one native Japanese artifact.")
        platform, artifact = next(iter(plugin["artifacts"].items()))
        if platform not in expected or platform in seen:
            raise ValueError(f"Unexpected or duplicate platform: {platform}")
        plugin_name = artifact["url"].rsplit("/", 1)[-1]
        core_name = f"pawingo-{tag.removeprefix('v')}-{platform}.tar.gz"
        if set(verified) != {core_name, plugin_name, catalog_path.name}:
            raise ValueError("Unexpected files in native release manifest.")
        archive = verified[plugin_name]
        if archive.stat().st_size != artifact["size_bytes"] or checksum(archive) != artifact["sha256"]:
            raise ValueError("Plugin disagrees with its pinned catalog.")
        with tarfile.open(verified[core_name]) as core:
            member = core.extractfile("build-info.yaml")
            if member is None:
                raise ValueError("Missing host build metadata.")
            metadata = json.load(member)
        if (
            metadata["version"] != tag.removeprefix("v")
            or metadata["release_build"] != build
            or metadata["platform"] != platform
            or metadata["abi"] != artifact["abi"]
            or metadata["catalog_sha256"] != checksum(catalog_path)
            or plugin["host_version"] != metadata["version"]
        ):
            raise ValueError("Native build identity or embedded catalog mismatch.")
        seen.add(platform)
        pairs.append((plugin, verified[core_name], archive))
    if seen != expected:
        raise ValueError(f"Missing matrix platforms: {sorted(expected - seen)}")
    merged = {key: value for key, value in pairs[0][0].items() if key != "artifacts"}
    merged["artifacts"] = {}
    for plugin, _, _ in pairs:
        if {key: value for key, value in plugin.items() if key != "artifacts"} != {
            key: value for key, value in merged.items() if key != "artifacts"
        }:
            baseline = {key: value for key, value in merged.items() if key != "artifacts"}
            candidate = {key: value for key, value in plugin.items() if key != "artifacts"}
            fields = sorted(
                key for key in baseline.keys() | candidate.keys() if baseline.get(key) != candidate.get(key)
            )
            raise ValueError(f"Matrix plugin metadata disagrees for {sorted(plugin['artifacts'])}: {', '.join(fields)}")
        merged["artifacts"].update(plugin["artifacts"])
    output.mkdir(parents=True, exist_ok=True)
    files = []
    for _, core, plugin_file in pairs:
        for source in (core, plugin_file):
            destination = output / source.name
            if destination.exists():
                raise ValueError("Duplicate release filename.")
            shutil.copy2(source, destination)
            files.append(destination)
    catalog = output / "plugins-v1.yaml"
    write_yaml(catalog, {"schema": 1, "plugins": [merged]})
    files.append(catalog)
    (output / "SHA256SUMS").write_text(
        "".join(f"{checksum(file)}  {file.name}\n" for file in sorted(files)), encoding="utf-8", newline="\n"
    )


def main() -> None:
    """Collect all configured native platforms before a single draft-upload job runs."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--inputs", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--tag", required=True)
    parser.add_argument("--build", required=True)
    options = parser.parse_args()
    collect(options.inputs, options.output, options.tag, options.build, SUPPORTED_PLATFORMS)


if __name__ == "__main__":
    main()
