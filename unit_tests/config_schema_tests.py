#!/usr/bin/env python3
"""Schema regression checks; run with the optional schemas/requirements.txt dependencies."""

import contextlib
from copy import deepcopy
import importlib.util
import io
import json
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("validate_configs", ROOT / "scripts/validate_configs.py")
validation = importlib.util.module_from_spec(spec)
spec.loader.exec_module(validation)


class ConfigSchemaTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.validators = validation.load_validators()
        cls.configs = {name: validation.load_json(ROOT / "server_configs" / name) for name in cls.validators}

    def check(self, name, config, valid):
        errors = list(self.validators[name].iter_errors(config))
        self.assertEqual(not errors, valid, str(errors))

    def test_http_https_defaults_and_invalid_combinations(self):
        original = self.configs["common.config"]
        candidate = deepcopy(original)
        candidate.pop("http"); candidate.pop("https")
        candidate["addresses"].pop("https_port")
        self.check("common.config", candidate, True)
        candidate = deepcopy(original)
        candidate["http"]["enabled"] = candidate["https"]["enabled"] = False
        self.check("common.config", candidate, False)
        for mutation in ("port", "certificate"):
            candidate = deepcopy(original)
            if mutation == "port":
                candidate["addresses"]["https_port"] = candidate["addresses"]["http_port"]
            else:
                candidate["https"]["certificate"] = "only-cert.pem"
            report = validation.validate_candidate("common.config", json.dumps(candidate), self.validators)
            self.assertFalse(report["valid"])
            self.assertTrue(report["errors"])

    def test_advertised_protocol_must_be_enabled(self):
        for scheme in ("http", "https"):
            candidate = deepcopy(self.configs["common.config"])
            candidate["advertised"]["web_scheme"] = scheme
            self.assertTrue(validation.validate_candidate("common.config", json.dumps(candidate), self.validators)["valid"])
            candidate[scheme]["enabled"] = False
            self.assertFalse(validation.validate_candidate("common.config", json.dumps(candidate), self.validators)["valid"])

    def test_all_shipped_configs_have_schemas_and_validate(self):
        self.assertEqual(set(self.validators), {p.name for p in (ROOT / "server_configs").glob("*.config")})
        for name, config in self.configs.items():
            with self.subTest(name=name):
                self.check(name, config, True)

    def test_detector_controls_and_interval_boundaries(self):
        for value, valid in [(1, True), (2147483647, True), ("1", True), ("2147483647", True),
                             (0, False), (-1, False), (2147483648, False), ("2147483648", False),
                             ("999999999999999999999", False), (1.5, False), ("1.5", False),
                             (True, False), ("", False), ("0", False)]:
            with self.subTest(value=value):
                config = deepcopy(self.configs["event.config"])
                config["MotionAlarm"]["EventGenerationTimeout"] = value
                self.check("event.config", config, valid)
        config = deepcopy(self.configs["event.config"])
        config["MotionAlarm"]["GenerateEvents"] = config["MotionAlarm"].pop("AutoGenerateEvents")
        self.check("event.config", config, False)
        config["MotionAlarm"]["AutoGenerateEvents"] = True
        self.check("event.config", config, False)

    def test_unknown_keys_and_wrong_root_types(self):
        for name, config in self.configs.items():
            with self.subTest(name=name):
                self.check(name, {**config, "typo": True}, False)
                self.check(name, [], False)

    def test_common_invalid_settings(self):
        cases = [
            ("addresses", "http_port", "65536"), ("addresses", "rtsp_port", -1),
            ("addresses", "http_port", "8554garbage"), ("addresses", "http_port", True),
            ("addresses", "ipv4", "256.1.2.3"), ("advertised", "ipv4", "0.0.0.0"),
            ("advertised", "ipv4", "239.255.255.250"), ("rtp", "port_min", 50001),
            ("rtp", "port_max", 50200), ("networkDelaySimulation", "milliseconds", 65536),
            ("multichannelSimulation", "channelCount", 256), ("fileStreaming", "enabled", "yes"),
            ("users", None, []), ("loggingLevel", None, "DEBIG"),
            ("rtsp_metadata", "enabled", "yes"),
        ]
        for section, key, value in cases:
            with self.subTest(section=section, key=key, value=value):
                config = deepcopy(self.configs["common.config"])
                if key is None:
                    config[section] = value
                else:
                    config[section][key] = value
                self.check("common.config", config, False)

    def test_defaults_and_conditionally_required_fields(self):
        config = deepcopy(self.configs["common.config"])
        for key in ["advertised", "rtp", "authenticationMethods", "portForwardingSimulation"]:
            del config[key]
        config["fileStreaming"] = {"enabled": False}
        self.check("common.config", config, True)
        config["fileStreaming"]["enabled"] = True
        self.check("common.config", config, False)
        config["fileStreaming"]["filePath"] = "video.mp4"
        self.check("common.config", config, True)
        config["portForwardingSimulation"] = {"enabled_for_http": True}
        self.check("common.config", config, False)
        self.check("discovery.config", {}, True)

    def test_profile_persistence_and_unattached_profiles(self):
        config = deepcopy(self.configs["media_profiles.config"])
        config["MediaProfiles"].append({"token": "new-profile", "fixed": "false", "Name": ""})
        self.check("media_profiles.config", config, True)
        config["VideoEncoder"][0]["RateControl"]["BitrateLimit"] = "12oops"
        self.check("media_profiles.config", config, False)
        config = deepcopy(self.configs["media_profiles.config"])
        config["VideoSource"][0]["Bounds"]["width"] = "-1280"
        self.check("media_profiles.config", config, False)
        config["VideoSource"][0]["Bounds"]["width"] = 1280
        self.check("media_profiles.config", config, True)

    def test_field_detector_persistence(self):
        config = deepcopy(self.configs["media_profiles.config"])
        rule = {"Name": "zone", "Type": "tt:FieldDetector", "Field": [
            {"x": "-0.5", "y": "-0.5"}, {"x": "0.5", "y": "-0.5"}, {"x": "0", "y": "0.5"}]}
        config["Analytics"][0]["rules"] = [rule]
        self.check("media_profiles.config", config, True)
        rule["Field"].pop()
        self.check("media_profiles.config", config, False)
        rule["Field"].append({"x": 0, "y": 0.5})
        rule["Segments"] = []
        self.check("media_profiles.config", config, False)

    def test_license_plate_persistence(self):
        config = deepcopy(self.configs["media_profiles.config"])
        rule = {"Name": "plates", "Type": "tt:LicensePlateRecognition", "IncludeImage": "",
                "PlateLocation": "Front", "Country": "US", "Sensitivity": "0.5", "Threshold": 0.8,
                "PlateMinWidth": 0, "PlateMaxWidth": 100, "EventInterval": "10"}
        config["Analytics"][0]["rules"] = [rule]
        self.check("media_profiles.config", config, True)
        rule["SnapLine"] = [{"x": 0, "y": -1}, {"x": 0, "y": 1}]
        self.check("media_profiles.config", config, True)
        rule["SnapLine"].pop()
        self.check("media_profiles.config", config, False)
        del rule["SnapLine"]
        rule["IncludeImage"] = "Embedded"
        self.check("media_profiles.config", config, True)
        rule["IncludeImage"] = "RemoteStorage"
        self.check("media_profiles.config", config, False)

    def test_face_recognition_persistence(self):
        config = deepcopy(self.configs["media_profiles.config"])
        rule = {"Name": "faces", "Type": "tt:FaceRecognition", "IncludeImage": "Embedded", "Threshold": "0.5"}
        config["Analytics"][0]["rules"] = [rule]
        for mode in ("Embedded", "LocalStorage", ""):
            rule["IncludeImage"] = mode
            self.check("media_profiles.config", config, True)
        rule["IncludeImage"] = "RemoteStorage"
        self.check("media_profiles.config", config, False)
        rule["IncludeImage"] = "Embedded"
        rule["Threshold"] = "not-a-number"
        self.check("media_profiles.config", config, False)
        rule["Threshold"] = 0.5
        rule["Unknown"] = "value"
        self.check("media_profiles.config", config, False)

    def test_empty_recording_dates_and_required_keys(self):
        config = deepcopy(self.configs["recording_search.config"])
        config["Recordings"][0].update(DateFrom="", DateUntil="")
        self.check("recording_search.config", config, True)
        del config["Recordings"][0]["DateFrom"]
        self.check("recording_search.config", config, False)

    def test_nested_typos_and_event_timeout_units(self):
        config = deepcopy(self.configs["event.config"])
        config["PullPoint"]["Timeout"] = "PT60S"
        self.check("event.config", config, False)
        config["PullPoint"]["Timeout"] = 60
        self.check("event.config", config, True)
        config["MotionAlarm"]["EventGenerationTimout"] = 5
        self.check("event.config", config, False)

    def test_stream_duration_and_namespace_values(self):
        config = deepcopy(self.configs["media.config"])
        config["GetStreamUri"][0]["Timeout"] = "60 seconds"
        self.check("media.config", config, False)
        config["GetStreamUri"][0]["Timeout"] = "PT1M"
        self.check("media.config", config, True)
        config["Namespaces"]["tt"] = "not a URI"
        self.check("media.config", config, False)

    def test_candidate_validation_reports_paths_without_exposing_values(self):
        candidate = deepcopy(self.configs["users.config"])
        candidate["users"][0]["password"] = {"sensitive": "never-echo-this-secret"}
        report = validation.validate_candidate("users.config", json.dumps(candidate), self.validators)
        self.assertFalse(report["valid"])
        self.assertTrue(any(e["path"] == "/users/0/password" for e in report["errors"]))
        self.assertNotIn("never-echo-this-secret", json.dumps(report))
        self.assertFalse(validation.validate_candidate("common.config", '{"x":1,"x":2}', self.validators)["valid"])
        self.assertFalse(validation.validate_candidate("common.config", '{"x":NaN}', self.validators)["valid"])
        self.assertTrue(validation.validate_candidate("common.config", json.dumps(self.configs["common.config"]), self.validators)["valid"])

    def test_cli_directory_file_missing_and_unknown(self):
        with tempfile.TemporaryDirectory() as temp, contextlib.redirect_stdout(io.StringIO()), \
                contextlib.redirect_stderr(io.StringIO()):
            directory = Path(temp)
            for name, config in self.configs.items():
                (directory / name).write_text(json.dumps(config))
            self.assertEqual(validation.main([str(directory)]), 0)
            target = directory / "common.config"
            self.assertEqual(validation.main([str(target)]), 0)
            target.write_text('{"loggingLevel": "DEBUG", "loggingLevel": "ERROR"}')
            self.assertEqual(validation.main([str(target)]), 1)
            target.unlink()
            self.assertEqual(validation.main([str(directory)]), 1)
            unknown = directory / "Common.config"
            unknown.write_text('{}')
            self.assertEqual(validation.main([str(unknown)]), 1)
            self.assertEqual(validation.main([str(directory)]), 1)


if __name__ == "__main__":
    unittest.main()
