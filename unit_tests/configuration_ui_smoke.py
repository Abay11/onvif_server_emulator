#!/usr/bin/env python3
"""Exercise profile/configuration editing, schema failures, restart hints and write failures."""
import argparse
from copy import deepcopy
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

from config_validation_smoke import request, wait
from network_smoke import soap

PATH = "/admin/api/configuration"


def view():
    return request(PATH)


def entry(name):
    return next(item for item in view()["files"] if item["name"] == name)


def save(name, document, expected=200, revision=None):
    current = entry(name)
    return request(PATH, {"file": name, "revision": revision or current["revision"],
                          "document": json.dumps(document)}, expected=expected, marker="configuration")


def exercise(read_only=False):
    initial = entry("media_profiles.config")
    media = json.loads(initial["document"])
    original = deepcopy(media)
    if read_only:
        before_profiles = request("/admin/api/status")["profiles"]
        media["MediaProfiles"][0]["Name"] = "Cannot persist this"
        result = save("media_profiles.config", media, expected=507)
        assert result["error"] == "ConfigurationWriteFailed"
        assert "read-only" in result["message"]
        assert entry("media_profiles.config")["document"] == initial["document"]
        assert request("/admin/api/status")["profiles"] == before_profiles
        print("PASS read-only mount: HTTP 507, explicit write failure, original file unchanged")
        return
    assert len(view()["files"]) == 15
    profile = deepcopy(media["MediaProfiles"][0])
    profile.update(token="ConfigurationWebProfile", Name="Configuration Web profile", fixed=False, Metadata="MetadataToken0")
    media["MediaProfiles"].append(profile)
    assert save("media_profiles.config", media)["restart_required"] is False
    xml = soap("127.0.0.1", 8080, "media2", "http://www.onvif.org/ver20/media/wsdl", "GetProfiles")
    assert b"ConfigurationWebProfile" in xml
    save("media_profiles.config", media, expected=409, revision=initial["revision"])
    media = json.loads(entry("media_profiles.config")["document"])
    invalid = deepcopy(media)
    invalid["MediaProfiles"] = invalid["MediaProfiles"][1:]
    save("media_profiles.config", invalid, expected=400)
    invalid = deepcopy(media)
    invalid["MediaProfiles"][-1]["Metadata"] = "missing"
    save("media_profiles.config", invalid, expected=400)
    invalid = deepcopy(media)
    invalid["VideoSource"][0]["CompatibleAnalyticsConfigurations"] = []
    save("media_profiles.config", invalid, expected=400)
    invalid = deepcopy(media)
    invalid["Metadata"][0]["Name"] = ""
    before = entry("media_profiles.config")["document"]
    result = save("media_profiles.config", invalid, expected=422)
    assert result["validation_errors"]
    assert entry("media_profiles.config")["document"] == before
    media["Metadata"][0].update(Events=False, Analytics=True, Filters=[])
    assert save("media_profiles.config", media)["restart_required"] is False
    xml = soap("127.0.0.1", 8080, "media", "http://www.onvif.org/ver10/media/wsdl", "GetMetadataConfiguration",
               "<t:ConfigurationToken>MetadataToken0</t:ConfigurationToken>")
    assert b"MetadataToken0" in xml
    media = json.loads(entry("media_profiles.config")["document"])
    media["MediaProfiles"][-1]["Metadata"] = ""
    assert save("media_profiles.config", media)["restart_required"] is False
    assert not json.loads(entry("media_profiles.config")["document"])["MediaProfiles"][-1]["Metadata"]
    media["MediaProfiles"] = [item for item in media["MediaProfiles"] if item["token"] != profile["token"]]
    assert save("media_profiles.config", media)["restart_required"] is False
    media["AudioEncoder"][0]["Bitrate"] = "32"
    assert save("media_profiles.config", media)["restart_required"] is True
    assert view()["restart_required"] is True
    common = json.loads(entry("common.config")["document"])
    users = json.loads(entry("users.config")["document"])
    assert all(item["password"] == "__UNCHANGED_SECRET__" for item in users["users"])
    users["users"].reverse()
    assert save("users.config", users)["restart_required"] is True
    common["loggingLevel"] = "INFO"
    assert save("common.config", common)["restart_required"] is True
    assert isinstance(json.loads(entry("common.config")["document"])["rtp"]["port_min"], int)
    invalid = deepcopy(common)
    invalid["rtp"]["port_min"] = 50200
    save("common.config", invalid, expected=400)
    # Restore media settings so standalone ONVIF smoke tests see their usual model.
    save("media_profiles.config", original)
    print("PASS media profile create/attach/detach/delete, fixed and compatibility guards, native ONVIF visibility, "
          "metadata editing, empty arrays, stale/schema rejection, write-only passwords and restart notices")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=Path("build/main"))
    parser.add_argument("--read-only", action="store_true", help="Check an externally started server on a read-only configuration mount")
    args = parser.parse_args()
    if args.read_only:
        exercise(True)
        return
    root = Path(__file__).resolve().parents[1]
    with tempfile.TemporaryDirectory(prefix="configuration-ui-") as directory:
        configs = Path(directory) / "configs"
        shutil.copytree(root / "server_configs", configs)
        common_path = configs / "common.config"
        common = json.loads(common_path.read_text()); common["authentication"] = "none"
        common_path.write_text(json.dumps(common))
        environment = dict(os.environ, ONVIF_ADVERTISED_IPV4="127.0.0.1", ONVIF_DISCOVERY_INTERFACE_IPV4="127.0.0.1")
        with (Path(directory) / "server.log").open("w+") as log:
            process = subprocess.Popen([str(args.binary.resolve()), str(configs)], env=environment,
                                       stdout=log, stderr=subprocess.STDOUT)
            try:
                wait(process)
                exercise()
                common = json.loads((configs / "users.config").read_text())
                assert {user["login"]: user["password"] for user in common["users"]} == {"a": "a1", "o": "o1", "u": "u1"}
            except BaseException:
                log.flush(); log.seek(0); print(log.read())
                raise
            finally:
                process.terminate()
                try: process.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    process.kill(); process.wait()


if __name__ == "__main__":
    main()
