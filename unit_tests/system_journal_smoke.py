#!/usr/bin/env python3
"""Verify internal detector journaling before any ONVIF/metadata consumer exists."""
from admin_session import urlopen, set_credentials
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
from urllib.parse import urlencode

from network_smoke import Rtsp
from system_reboot_smoke import call


def journal(**parameters):
    url = "http://127.0.0.1:8080/admin/api/journal?" + urlencode(parameters)
    with urlopen(url, timeout=5) as response:
        assert response.headers["Cache-Control"] == "no-store"
        result = json.load(response)
    assert isinstance(result["entries"], list)
    assert isinstance(result["cursor"], int)
    assert isinstance(result["dropped"], int)
    return result


def wait_for(process, predicate, timeout=15):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        assert process.poll() is None, process.returncode
        try:
            result = journal(limit=200)
            if predicate(result):
                return result
        except urllib.error.URLError:
            pass
        time.sleep(0.2)
    raise AssertionError("Expected journal event did not arrive")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=Path("build/main"))
    parser.add_argument("--network-smoke", action="store_true")
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[1]
    with tempfile.TemporaryDirectory(prefix="onvif-journal-") as directory:
        configs = Path(directory) / "configs"
        shutil.copytree(repo / "server_configs", configs)
        common_file = configs / "common.config"
        common = json.loads(common_file.read_text())
        common["rtsp_metadata"]["enabled"] = False
        users_file = configs / "users.config"
        users = json.loads(users_file.read_text())
        users["users"][0]["password"] = "journal-secret-must-not-leak"
        set_credentials("a", "journal-secret-must-not-leak")
        users_file.write_text(json.dumps(users))
        common_file.write_text(json.dumps(common))
        # Bound event arrival independently of the user's configured generator intervals.
        event_file = configs / "event.config"
        events = json.loads(event_file.read_text())
        events["MotionAlarm"].update(EventGenerationTimeout=1, AutoGenerateEvents=True, Enabled=True)
        event_file.write_text(json.dumps(events))
        env = dict(os.environ, ONVIF_ADVERTISED_IPV4="127.0.0.1", ONVIF_DISCOVERY_INTERFACE_IPV4="127.0.0.1")
        with (Path(directory) / "process.log").open("w+") as log:
            process = subprocess.Popen([str(args.binary.resolve()), str(configs)], env=env,
                                       stdout=log, stderr=subprocess.STDOUT)
            try:
                before = wait_for(process, lambda page: any(e["category"] == "detector" for e in page["entries"]))
                assert not any(e["category"] == "client" for e in before["entries"]), before
                assert any(e["source"] == "application" for e in before["entries"])
                filtered = journal(category="detector", limit=1)
                assert len(filtered["entries"]) == 1 and filtered["entries"][0]["category"] == "detector"
                assert filtered["entries"][0]["id"] == journal(category="detector", limit=1)["entries"][0]["id"]
                cursor = before["cursor"]
                call(8080, "GetDeviceInformation")
                stream = Rtsp("127.0.0.1", 8554)
                stream.request("OPTIONS", "rtsp://127.0.0.1:8554/Live&HighStream")
                stream.socket.close()
                page = wait_for(process, lambda p: any(e["source"] == "RTSP" and "disconnected" in e["message"] for e in p["entries"]))
                assert any(e["source"] == "ONVIF" and e["category"] == "client" for e in page["entries"])
                incremental = journal(after=cursor, limit=200)
                assert all(e["id"] > cursor for e in incremental["entries"])
                assert incremental["cursor"] >= cursor and not incremental["reset"]
                assert "journal-secret-must-not-leak" not in json.dumps(page)
                if args.network_smoke:
                    for script in ("network_smoke.py", "event_delivery_smoke.py"):
                        subprocess.run([sys.executable, str(repo / "unit_tests" / script)], check=True)
                print("PASS detector capture without ONVIF subscribers with metadata disabled, independent reads, "
                      "filters, incremental cursors, lifecycle, ONVIF/RTSP client activity and secret exclusion")
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
