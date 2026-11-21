#!/usr/bin/env python3
"""Check exact-source selection and failure behavior without network access."""

import importlib.util
import json
from pathlib import Path
import subprocess
import tarfile
import tempfile
import unittest
from unittest.mock import patch


def load_script(name):
    path = Path(__file__).resolve().parents[1] / "scripts" / f"{name}.py"
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


rust_sources = load_script("package_rust_sources")
debian_sources = load_script("collect_debian_sources")


class SourcePackagingTests(unittest.TestCase):
    def test_actual_artifacts_preserve_git_workspace_and_registry_notices(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            workspace = root / "plugin"
            cargo_home = root / "cargo"
            workspace.mkdir()
            (workspace / "Cargo.toml").write_text(
                '[package]\nname="plugin"\nversion="1.0.0"\nlicense="MPL-2.0"\n')
            checkout = cargo_home / "git/checkouts/bindings/revision"
            crate = checkout / "member"
            crate.mkdir(parents=True)
            (checkout / "Cargo.toml").write_text(
                '[workspace.package]\nversion="2.0.0"\nlicense="MIT"\n')
            (checkout / "LICENSE").write_text("Workspace notice fixture")
            (crate / "Cargo.toml").write_text(
                '[package]\nname="bindings"\nversion.workspace=true\nlicense.workspace=true\n')
            registry = cargo_home / "registry/src/index/parser-1.0.0"
            registry.mkdir(parents=True)
            (registry / "Cargo.toml").write_text(
                '[package]\nname="parser"\nversion="1.0.0"\nlicense="MIT OR Apache-2.0"\n')
            (registry / "LICENSE-MIT").write_text("Registry notice fixture")
            unrelated = cargo_home / "registry/src/index/unrelated-1.0.0"
            unrelated.mkdir()
            (unrelated / "LICENSE").write_text("Not selected")
            messages = root / "messages.jsonl"
            manifests = [workspace / "Cargo.toml", crate / "Cargo.toml", registry / "Cargo.toml"]
            messages.write_text("\n".join(json.dumps({"reason": "compiler-artifact",
                                                       "manifest_path": str(p), "fresh": True})
                                             for p in manifests + manifests))
            destination = root / "out"
            rust_sources.package_sources(messages, workspace, cargo_home, destination)
            packages = json.loads((destination / "cargo-artifacts.json").read_text())
            self.assertEqual(len(packages), 3)
            bindings = next(p for p in packages if p["name"] == "bindings")
            self.assertEqual((bindings["version"], bindings["license"]), ("2.0.0", "MIT"))
            with tarfile.open(destination / "cargo-dependency-sources.tar.gz") as archive:
                names = archive.getnames()
                self.assertIn("cargo/git/checkouts/bindings/revision/LICENSE", names)
                self.assertIn("cargo/registry/src/index/parser-1.0.0/LICENSE-MIT", names)
                self.assertFalse(any("unrelated" in name for name in names))

    def test_missing_license_stops_packaging(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "Cargo.toml").write_text('[package]\nname="plugin"\nversion="1.0.0"\n')
            messages = root / "messages.jsonl"
            messages.write_text(json.dumps({"reason": "compiler-artifact",
                                             "manifest_path": str(root / "Cargo.toml")}))
            with self.assertRaisesRegex(ValueError, "Missing license"):
                rust_sources.package_sources(messages, root, root / "cargo", root / "out")

    def test_empty_artifact_stream_stops_packaging(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            messages = root / "messages.jsonl"
            messages.write_text('{"reason":"build-finished","success":true}\n')
            with self.assertRaisesRegex(ValueError, "compiler artifacts"):
                rust_sources.package_sources(messages, root, root / "cargo", root / "out")

    def test_debian_sources_are_deduplicated_and_exact_versions_are_requested(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            manifest = root / "sources.tsv"
            manifest.write_text("codec\t1:2.3-4\ncodec\t1:2.3-4\npython\t3.13-1\n")
            with patch.object(debian_sources.subprocess, "run") as run:
                debian_sources.collect([manifest], root / "out")
            self.assertEqual(run.call_count, 1)
            self.assertEqual(run.call_args_list[0].args[0][-2:], ["codec=1:2.3-4", "python=3.13-1"])
            self.assertTrue(run.call_args_list[0].kwargs["check"])
            self.assertEqual((root / "out/source-packages.tsv").read_text(),
                             "codec\t1:2.3-4\npython\t3.13-1\n")

    def test_unavailable_debian_source_stops_release(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            manifest = root / "sources.tsv"
            manifest.write_text("codec\t1.0-1\n")
            failure = subprocess.CalledProcessError(100, "apt-get")
            with patch.object(debian_sources.subprocess, "run", side_effect=failure):
                with self.assertRaises(subprocess.CalledProcessError):
                    debian_sources.collect([manifest], root / "out")
            self.assertFalse((root / "out/source-packages.tsv").exists())


if __name__ == "__main__":
    unittest.main()
