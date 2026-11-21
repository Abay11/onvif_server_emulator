#!/usr/bin/env python3
"""Exercise detector controls and rule persistence against a source-built process."""
import argparse
from copy import deepcopy
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
import xml.etree.ElementTree as ET

from network_smoke import soap
from system_journal_smoke import journal

BASE = "http://127.0.0.1:8080"


def api(path="/admin/api/detectors", command=None, expected=200):
    request = urllib.request.Request(BASE + path, None if command is None else json.dumps(command).encode(),
                                     {} if command is None else {"Content-Type": "application/json", "X-ONVIF-Admin": "detectors"})
    try:
        response = urllib.request.urlopen(request, timeout=10)
    except urllib.error.HTTPError as error:
        response = error
    with response:
        raw = response.read()
        assert response.status == expected, (response.status, raw)
        return json.loads(raw)


def wait(process, predicate, timeout=15):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        assert process.poll() is None, process.returncode
        try:
            result = predicate()
            if result:
                return result
        except (OSError, urllib.error.URLError):
            pass
        time.sleep(0.2)
    raise AssertionError("Detector change did not become observable")


def stored():
    rules = api()["configurations"][0]["rules"]
    if not rules:
        return None
    result = deepcopy(rules[0])
    result.pop("enabled", None)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=Path("build/main"))
    parser.add_argument("--network-smoke", action="store_true")
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[1]
    with tempfile.TemporaryDirectory(prefix="onvif-detectors-") as directory:
        configs = Path(directory) / "configs"
        shutil.copytree(repo / "server_configs", configs)
        events_file = configs / "event.config"
        events = json.loads(events_file.read_text())
        for section in events.values():
            if isinstance(section, dict) and "EventGenerationTimeout" in section:
                section["EventGenerationTimeout"] = 1
        events_file.write_text(json.dumps(events))
        env = dict(os.environ, ONVIF_ADVERTISED_IPV4="127.0.0.1", ONVIF_DISCOVERY_INTERFACE_IPV4="127.0.0.1")
        with (Path(directory) / "process.log").open("w+") as log:
            process = subprocess.Popen([str(args.binary.resolve()), str(configs)], env=env,
                                       stdout=log, stderr=subprocess.STDOUT)
            try:
                catalog = wait(process, lambda: api())
                assert len(catalog["simple"]) == 4 and len(catalog["families"]) == 6
                with urllib.request.urlopen(BASE + "/admin/detectors.js", timeout=5) as response:
                    assert response.status == 200 and b"pointermove" in response.read()
                cfg = catalog["configurations"][0]
                token = cfg["token"]
                rule = deepcopy(next(t for t in cfg["templates"] if t["Type"] == "tt:MotionRegionDetector"))
                rule["Name"] = "WebUI Region"
                api(command={"action": "create", "configuration": token, "name": rule["Name"], "rule": rule})
                previous = stored()
                rule = deepcopy(previous)
                rule["MotionRegion"]["Sensitivity"] = "0.8"
                command = {"action": "modify", "configuration": token, "name": rule["Name"], "rule": rule, "previous": previous}
                api(command=command)
                api(command=command, expected=409)
                current = stored()
                invalid = deepcopy(current)
                invalid["MotionRegion"]["Sensitivity"] = "2"
                api(command=dict(command, previous=current, rule=invalid), expected=400)
                assert stored() == current
                xml = soap("127.0.0.1", 8080, "analytics", "http://www.onvif.org/ver20/analytics/wsdl", "GetRules",
                           f"<t:ConfigurationToken>{token}</t:ConfigurationToken>")
                assert any(item.get("Name") == rule["Name"] for item in ET.fromstring(xml).iter() if item.tag.endswith("}Rule"))

                def rule_events(after):
                    return [entry for entry in journal(after=after, limit=200)["entries"]
                            if entry["category"] == "detector" and "WebUI Region" in entry["message"]]

                cursor = journal(limit=200)["cursor"]
                wait(process, lambda: rule_events(cursor))
                for scope in ({"action": "toggle", "configuration": token, "name": rule["Name"]},
                              {"action": "detector", "id": "rules"},
                              {"action": "detector", "id": "tt:MotionRegionDetector"}):
                    api(command=dict(scope, enabled=False))
                    time.sleep(0.2)
                    cursor = journal(limit=200)["cursor"]
                    time.sleep(2.2)
                    assert not rule_events(cursor), scope
                    api(command=dict(scope, enabled=True))
                    wait(process, lambda: rule_events(cursor))
                # Manual execution is independent of the automatic timer.
                cursor = journal(limit=200)["cursor"]
                api(command={"action": "manual", "id": "MotionAlarm"})
                assert any(e["category"] == "detector" and e["source"] == events["MotionAlarm"]["Topic"]
                           for e in journal(after=cursor, limit=200)["entries"])
                before = events_file.read_bytes()
                rejected = api(command={"action": "settings", "id": "MotionAlarm", "settings": {"EventGenerationTimeout": 0}}, expected=422)
                assert rejected["validation_errors"] and events_file.read_bytes() == before
                api(command={"action": "settings", "id": "MotionAlarm", "settings": {"Enabled": False}})
                api(command={"action": "manual", "id": "MotionAlarm"}, expected=409)
                api(command={"action": "settings", "id": "MotionAlarm", "settings": {"Enabled": True, "AutoGenerateEvents": True}})
                cursor = journal(limit=200)["cursor"]
                wait(process, lambda: any(e["source"] == events["MotionAlarm"]["Topic"] for e in journal(after=cursor, limit=200)["entries"]))
                api(command={"action": "settings", "id": "MotionAlarm", "settings": {"AutoGenerateEvents": False}})
                cursor = journal(limit=200)["cursor"]
                time.sleep(2.2)
                assert not any(e["source"] == events["MotionAlarm"]["Topic"] for e in journal(after=cursor, limit=200)["entries"])
                # Scoped rule execution uses the same generator with auto mode off.
                api(command={"action": "settings", "id": "tt:MotionRegionDetector", "settings": {"AutoGenerateEvents": False}})
                cursor = journal(limit=200)["cursor"]
                for _ in range(8):
                    api(command={"action": "manual", "id": "tt:MotionRegionDetector", "configuration": token, "name": rule["Name"]})
                assert rule_events(cursor)
                api(command={"action": "toggle", "configuration": token, "name": rule["Name"], "enabled": False})
                api(command={"action": "manual", "id": "tt:MotionRegionDetector", "configuration": token, "name": rule["Name"]}, expected=409)
                api(command={"action": "toggle", "configuration": token, "name": rule["Name"], "enabled": True})
                api(command={"action": "settings", "id": "tt:MotionRegionDetector", "settings": {"AutoGenerateEvents": True}})
                api(command={"action": "detector", "id": "rules", "enabled": False})
                instance = api("/admin/api/status")["instance_id"]
                req = urllib.request.Request(BASE + "/admin/api/reboot", b'{"confirm":true}',
                                             {"Content-Type": "application/json", "X-ONVIF-Admin": "reboot"})
                with urllib.request.urlopen(req, timeout=5) as response:
                    assert response.status == 202
                wait(process, lambda: api("/admin/api/status")["instance_id"] != instance)
                assert api()["rules_enabled"] is True
                assert stored() == current  # Rule edits persisted across exec.
                api(command={"action": "delete", "configuration": token, "name": rule["Name"], "previous": current})
                assert stored() is None
                if args.network_smoke:
                    for script in ("network_smoke.py", "event_delivery_smoke.py", "analytics_smoke.py"):
                        subprocess.run([sys.executable, str(repo / "unit_tests" / script)], check=True)
                print("PASS detector catalog/assets, rule create/edit/delete and persistence, invalid/stale edits, "
                      "ONVIF visibility, per-rule/family/parent gates, validated live settings and auto-off manual execution")
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
