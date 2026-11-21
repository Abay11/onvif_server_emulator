#!/usr/bin/env python3
"""Download exact Debian source packages from a package/version TSV manifest."""

import argparse
from pathlib import Path
import subprocess


def collect(manifests, destination):
    sources = set()
    for manifest in manifests:
        for line in manifest.read_text().splitlines():
            if line:
                name, version = line.split("\t")
                if not name or not version or any(c.isspace() for c in name + version):
                    raise ValueError(f"Invalid source package/version: {line!r}")
                sources.add((name, version))
    if not sources:
        raise ValueError("Source package manifest is empty")
    destination.mkdir(parents=True, exist_ok=True)
    packages = [f"{name}={version}" for name, version in sorted(sources)]
    for offset in range(0, len(packages), 40):
        # apt verifies the signed Sources index and source archive hashes.
        # A missing exact version must fail the release, not fetch a newer one.
        subprocess.run(["apt-get", "source", "--download-only", "--only-source", "--",
                        *packages[offset:offset + 40]], cwd=destination, check=True)
    (destination / "source-packages.tsv").write_text(
        "".join(f"{name}\t{version}\n" for name, version in sorted(sources)))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", action="append", required=True, type=Path)
    parser.add_argument("--destination", required=True, type=Path)
    args = parser.parse_args()
    collect(args.manifest, args.destination)


if __name__ == "__main__":
    main()
