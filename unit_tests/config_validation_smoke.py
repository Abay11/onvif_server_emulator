#!/usr/bin/env python3
"""Check pre-write schema rejection and packaged validator resources on a real server."""
from admin_session import urlopen, set_credentials
import argparse
from copy import deepcopy
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time
import urllib.error
import urllib.request


def request(path, command=None, expected=200, marker="detectors"):
    req = urllib.request.Request("http://127.0.0.1:8080" + path,
                                 None if command is None else json.dumps(command).encode(),
                                 {} if command is None else {"Content-Type": "application/json", "X-ONVIF-Admin": marker})
    try:
        response = urlopen(req, timeout=15)
    except urllib.error.HTTPError as error:
        response = error
    with response:
        raw = response.read()
        assert response.status == expected, (response.status, raw)
        return json.loads(raw)


def wait(process, previous=None):
    deadline = time.monotonic() + 30
    while time.monotonic() < deadline:
        assert process.poll() is None, process.returncode
        try:
            current = request("/admin/api/status")
            if previous is None or current["instance_id"] != previous:
                return current
        except OSError:
            pass
        time.sleep(0.1)
    raise AssertionError("Server did not become available")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=Path("build/main"))
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    with tempfile.TemporaryDirectory(prefix="config-validation-") as directory:
        workspace = Path(directory)
        # Copy only installed runtime artifacts, independently of the executable's cwd.
        bundle = workspace / "app"
        bundle.mkdir()
        shutil.copy2(args.binary, bundle / "main")
        shutil.copytree(args.binary.resolve().parent / "config_validation", bundle / "config_validation")
        configs = workspace / "configs"
        shutil.copytree(root / "server_configs", configs)
        target = configs / "media_profiles.config"
        config = json.loads(target.read_text())
        config["TypoSetting"] = "never-echo-this-secret"
        target.write_text(json.dumps(config))
        before = target.read_bytes()
        env = dict(os.environ, ONVIF_ADVERTISED_IPV4="127.0.0.1", ONVIF_DISCOVERY_INTERFACE_IPV4="127.0.0.1")
        with (workspace / "server.log").open("w+") as log:
            process = subprocess.Popen([str(bundle / "main"), str(configs)], cwd=workspace, env=env,
                                       stdout=log, stderr=subprocess.STDOUT)
            try:
                state = wait(process)
                with urlopen("http://127.0.0.1:8080/admin/") as response:
                    assert b'id="config-error-dialog"' in response.read()
                catalog = request("/admin/api/detectors")["configurations"][0]
                rule = deepcopy(catalog["templates"][0])
                rule["Name"] = "Validation test"
                command = {"action": "create", "configuration": catalog["token"], "name": rule["Name"], "rule": rule}
                error = request("/admin/api/detectors", command, expected=422)
                assert error["error"] == "ConfigValidationFailed"
                assert error["validation_errors"][0]["file"] == "media_profiles.config"
                assert error["validation_errors"][0]["path"] == "/"
                assert "TypoSetting" in error["validation_errors"][0]["message"]
                assert "never-echo-this-secret" not in json.dumps(error)
                assert target.read_bytes() == before
                assert not request("/admin/api/detectors")["configurations"][0]["rules"]
                assert not list(configs.glob("media_profiles.config.*"))
                del config["TypoSetting"]
                target.write_text(json.dumps(config))
                request("/admin/api/reboot", {"confirm": True}, expected=202, marker="reboot")
                wait(process, state["instance_id"])
                # Missing packaged schemas must never bypass the save gate.
                schemas = bundle / "config_validation/schemas"
                hidden = bundle / "config_validation/hidden-schemas"
                schemas.rename(hidden)
                unchanged = target.read_bytes()
                try:
                    unavailable = request("/admin/api/detectors", command, expected=503)
                    assert unavailable["error"] == "ValidationUnavailable"
                    assert target.read_bytes() == unchanged
                    assert not request("/admin/api/detectors")["configurations"][0]["rules"]
                finally:
                    hidden.rename(schemas)
                request("/admin/api/detectors", command)
                assert len(request("/admin/api/detectors")["configurations"][0]["rules"]) == 1
                assert len(json.loads(target.read_text())["Analytics"][0]["rules"]) == 1
                print("PASS packaged validator, full candidate rejection before file creation, structured errors, "
                      "secret exclusion, unchanged file/model, fail-closed missing resources and valid save")
            except BaseException:
                log.flush()
                log.seek(0)
                print(log.read())
                raise
            finally:
                process.terminate()
                try:
                    process.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()


if __name__ == "__main__":
    main()
