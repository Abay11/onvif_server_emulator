#!/usr/bin/env python3
"""Preserve sources and license metadata for actual Cargo compiler artifacts."""

import argparse
import json
from pathlib import Path
import tarfile
import tomllib


def package_sources(messages, workspace, cargo_home, destination):
    workspace, cargo_home = workspace.resolve(), cargo_home.resolve()
    destination.mkdir(parents=True, exist_ok=True)
    manifests = set()
    for line in messages.read_text().splitlines():
        message = json.loads(line)
        if message.get("reason") == "compiler-artifact":
            manifests.add(Path(message["manifest_path"]).resolve())
    if not manifests:
        raise ValueError("Cargo did not report any compiler artifacts")

    roots = set()
    packages = []
    for manifest in sorted(manifests):
        data = tomllib.loads(manifest.read_text())
        package = data["package"]
        if manifest.is_relative_to(workspace):
            archive_manifest = Path("gst-plugins-rs") / manifest.relative_to(workspace)
        else:
            relative = manifest.relative_to(cargo_home)
            if relative.parts[:2] not in (("registry", "src"), ("git", "checkouts")):
                raise ValueError(f"Unexpected Cargo source location: {manifest}")
            source_root = cargo_home.joinpath(*relative.parts[:4])
            roots.add(source_root)
            archive_manifest = Path("cargo") / relative
        fields = {}
        for field in ("version", "license", "license-file"):
            value = package.get(field)
            if isinstance(value, dict) and value.get("workspace"):
                for parent in manifest.parents:
                    parent_manifest = parent / "Cargo.toml"
                    if parent_manifest.exists():
                        parent_data = tomllib.loads(parent_manifest.read_text())
                        inherited = parent_data.get("workspace", {}).get("package", {})
                        if field in inherited:
                            value = inherited[field]
                            break
                else:
                    raise ValueError(f"Cannot resolve workspace {field}: {manifest}")
            fields[field] = value
        if not fields["license"] and not fields["license-file"]:
            raise ValueError(f"Missing license declaration: {manifest}")
        packages.append({"name": package["name"], **fields, "manifest": str(archive_manifest)})

    # Keep complete selected checkout roots: inherited manifests, license files
    # and path dependencies may live above an individual crate directory.
    with tarfile.open(destination / "cargo-dependency-sources.tar.gz", "w:gz") as archive:
        for source_root in sorted(roots):
            archive.add(source_root, arcname=str(Path("cargo") / source_root.relative_to(cargo_home)),
                        filter=lambda info: None if ".git" in Path(info.name).parts else info)
    (destination / "cargo-artifacts.json").write_text(json.dumps(packages, indent=2) + "\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--messages", required=True, type=Path)
    parser.add_argument("--workspace", required=True, type=Path)
    parser.add_argument("--cargo-home", required=True, type=Path)
    parser.add_argument("--destination", required=True, type=Path)
    args = parser.parse_args()
    package_sources(args.messages, args.workspace, args.cargo_home, args.destination)


if __name__ == "__main__":
    main()
