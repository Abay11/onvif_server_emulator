#!/usr/bin/env python3
"""Check embedded administration and actual WebUI/SOAP restarts with copied configs.

Run in an isolated dev container; default service ports must be available.
"""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request

from system_reboot_smoke import call, wait_for


def request(path, body=None, headers=None, expected=200):
    req = urllib.request.Request(f"http://127.0.0.1:8080{path}", body, headers or {})
    try:
        response = urllib.request.urlopen(req, timeout=5)
    except urllib.error.HTTPError as error:
        response = error
    with response:
        data = response.read()
        assert response.status == expected, (path, response.status, data)
        assert response.headers["Cache-Control"] == "no-store"
        assert response.headers["X-Content-Type-Options"] == "nosniff"
        assert "frame-ancestors 'none'" in response.headers["Content-Security-Policy"]
        return data, response.headers


def status():
    raw, _ = request("/admin/api/status")
    assert b"admin-secret-must-not-leak" not in raw
    assert b'"password"' not in raw and b'"users"' not in raw
    data = json.loads(raw)
    assert isinstance(data["profiles"], list), data["profiles"]
    assert isinstance(data["uptime_seconds"], int)
    assert isinstance(data["reboot_available"], bool)
    return data


def wait_instance(process, previous):
    deadline = time.monotonic() + 30
    while time.monotonic() < deadline:
        assert process.poll() is None, process.returncode
        try:
            data = status()
            if data["instance_id"] != previous:
                return data
        except (OSError, urllib.error.URLError):
            pass
        time.sleep(0.1)
    raise AssertionError("Application instance did not change after reboot")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=Path("build/main"))
    parser.add_argument("--network-smoke", action="store_true")
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[1]
    with tempfile.TemporaryDirectory(prefix="onvif-admin-smoke-") as directory:
        configs = Path(directory) / "configs"
        shutil.copytree(repo / "server_configs", configs)
        common_file = configs / "common.config"
        common = json.loads(common_file.read_text())
        common["users"][0]["password"] = "admin-secret-must-not-leak"
        common["advertised"]["ipv4"] = "192.0.2.123"
        common_file.write_text(json.dumps(common))
        device_file = configs / "device.config"
        device = json.loads(device_file.read_text())
        original = device["GetDeviceInformation"]["Manufacturer"]
        env = dict(os.environ, ONVIF_ADVERTISED_IPV4="127.0.0.1",
                   ONVIF_DISCOVERY_INTERFACE_IPV4="127.0.0.1")
        with (Path(directory) / "server.log").open("w+") as log:
            process = subprocess.Popen([str(args.binary.resolve()), str(configs)], env=env,
                                       stdout=log, stderr=subprocess.STDOUT)
            try:
                wait_for(process, 8080, original)
                page, headers = request("/admin")
                assert b"System overview" in page
                assert headers["Content-Type"].startswith("text/html")
                for path, mime in (("/admin/app.css", "text/css"), ("/admin/app.js", "text/javascript")):
                    asset, headers = request(path)
                    assert len(asset) > 100
                    assert headers["Content-Type"].startswith(mime)
                before = status()
                assert before["settings"]["network"]["advertised_ipv4"] == "127.0.0.1"
                assert before["settings"]["discovery"]["interface_ipv4"] == "127.0.0.1"
                assert before["settings"]["device"]["Manufacturer"] == original
                request("/admin/api/reboot", expected=405)
                body = json.dumps({"confirm": True}).encode()
                request("/admin/api/reboot", body, {"Content-Type": "application/json"}, expected=403)
                reboot_headers = {"Content-Type": "application/json", "X-ONVIF-Admin": "reboot"}
                request("/admin/api/reboot", body, dict(reboot_headers, Origin="https://other.example"), expected=403)
                assert status()["instance_id"] == before["instance_id"]
                device["GetDeviceInformation"]["Manufacturer"] = "Updated through WebUI reboot"
                device_file.write_text(json.dumps(device))
                assert status()["settings"]["device"]["Manufacturer"] == original
                acknowledgement, _ = request("/admin/api/reboot", body, reboot_headers, expected=202)
                assert json.loads(acknowledgement) == {"status": "rebooting"}
                after = wait_instance(process, before["instance_id"])
                assert after["pid"] == before["pid"] == process.pid
                assert after["settings"]["device"]["Manufacturer"] == device["GetDeviceInformation"]["Manufacturer"]
                if args.network_smoke:
                    subprocess.run([sys.executable, str(repo / "unit_tests/network_smoke.py")], check=True)
                # The SOAP operation must still drive the same application restart.
                call(8080, "SystemReboot")
                latest = wait_instance(process, after["instance_id"])
                assert latest["pid"] == before["pid"]
                # WebUI access is intentionally independent of SOAP Digest in stage one.
                common["authentication"] = "digest"
                common_file.write_text(json.dumps(common))
                request("/admin/api/reboot", body, reboot_headers, expected=202)
                protected = wait_instance(process, latest["instance_id"])
                assert protected["settings"]["application"]["onvif_authentication"] == "digest"
                assert protected["settings"]["application"]["web_authentication"] == "none"
                print("PASS embedded assets, typed status, effective environment overrides, secret exclusion, "
                      "cross-origin rejection, actual WebUI config reload, new instance with stable PID, "
                      "and SOAP reboot coexistence")
            except BaseException:
                log.flush()
                log.seek(0)
                print(log.read(), file=sys.stderr)
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
