#!/usr/bin/env python3
"""Validate emulator configuration files against the bundled JSON Schemas."""

import argparse
import json
import re
from pathlib import Path
import sys

try:
    from jsonschema import Draft202012Validator, FormatChecker
    from referencing import Registry, Resource
except ImportError:
    sys.exit("Install validation dependencies: python3 -m pip install -r schemas/requirements.txt")

SCHEMAS_DIR = Path(__file__).resolve().parents[1] / "schemas"


def parse_json(text):
    def unique_keys(pairs):
        result = {}
        for key, value in pairs:
            if key in result:
                raise ValueError(f"Duplicate JSON property: {key}")
            result[key] = value
        return result

    def reject_constant(value):
        raise ValueError(f"Invalid JSON constant: {value}")

    return json.loads(text, object_pairs_hook=unique_keys, parse_constant=reject_constant)


def load_json(path):
    return parse_json(path.read_text(encoding="utf-8"))


def validate_candidate(filename, text, validators=None):
    """Validate in-memory data without writing a candidate or revealing values."""
    try:
        config = parse_json(text)
    except ValueError as error:
        return {"valid": False, "errors": [{"file": filename, "path": "/", "message": str(error)[:512]}]}
    validators = validators if validators is not None else load_validators()
    validator = validators.get(filename)
    if validator is None:
        return {"valid": False, "errors": [{"file": filename, "path": "/", "message": "No schema registered for this filename"}]}
    issues = []
    truncated = False
    for error in validator.iter_errors(config):
        if len(issues) == 20:
            truncated = True
            break
        pointer = "/" + "/".join(str(p).replace("~", "~0").replace("/", "~1") for p in error.absolute_path)
        # jsonschema messages can embed whole values, including account secrets.
        # Return schema constraints and field paths instead of configuration data.
        kind = error.validator
        if kind == "required":
            missing = [name for name in error.validator_value if name not in error.instance]
            message = "Missing required field(s): " + ", ".join(missing)
        elif kind == "additionalProperties":
            unknown = [str(key) for key in error.instance
                       if key not in error.schema.get("properties", {})
                       and not any(re.search(pattern, key) for pattern in error.schema.get("patternProperties", {}))]
            message = "Unknown field(s): " + ", ".join(unknown)
        elif kind == "type":
            message = "Expected JSON type: " + str(error.validator_value)
        elif kind in ("oneOf", "anyOf"):
            message = "Value does not match an allowed schema variant"
        else:
            message = "Value violates schema constraint: " + str(kind)
        issues.append({"file": filename, "path": pointer, "message": message[:512]})
    if filename == "common.config" and not issues:
        http = config.get("http", {}).get("enabled", True) in (True, "true")
        https = config.get("https", {}).get("enabled", True) in (True, "true")
        if http and https and int(config["addresses"]["http_port"]) == int(config["addresses"].get("https_port", 8443)):
            issues.append({"file": filename, "path": "/addresses/https_port", "message": "HTTP and HTTPS must use different ports"})
        scheme = config.get("advertised", {}).get("web_scheme", "auto")
        if (scheme == "http" and not http) or (scheme == "https" and not https):
            issues.append({"file": filename, "path": "/advertised/web_scheme", "message": "The advertised protocol must be enabled"})
        tls = config.get("https", {})
        if https and bool(tls.get("certificate")) != bool(tls.get("private_key")):
            issues.append({"file": filename, "path": "/https", "message": "Certificate and private key must both be configured, or both empty"})
    return {"valid": not issues, "errors": issues, "truncated": truncated}


def load_validators():
    manifest = load_json(SCHEMAS_DIR / "config-files.json")
    schemas = {}
    resources = []
    for path in sorted(SCHEMAS_DIR.glob("*.schema.json")):
        schema = load_json(path)
        Draft202012Validator.check_schema(schema)
        # Supply a local base URI without tying portable schema files to a host.
        schema["$id"] = path.as_uri()
        schemas[path.name] = schema
        resources.append((schema["$id"], Resource.from_contents(schema)))
    # Resolve only bundled resources: validation never downloads remote schemas.
    registry = Registry().with_resources(resources)
    return {
        filename: Draft202012Validator(schemas[schema_name], registry=registry, format_checker=FormatChecker())
        for filename, schema_name in manifest.items()
    }


def validate_file(path, validator):
    try:
        text = path.read_text(encoding="utf-8")
    except OSError as error:
        print(f"{path}: {error}", file=sys.stderr)
        return False
    report = validate_candidate(path.name, text, {path.name: validator})
    for error in report["errors"]:
        print(f"{path}:{error['path']}: {error['message']}", file=sys.stderr)
    if report["valid"]:
        print(f"OK: {path}")
    return report["valid"]


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("paths", nargs="*", type=Path, default=[Path("server_configs")],
                        help="Complete configuration directories or individual .config files (default: server_configs)")
    parser.add_argument("--stdin", metavar="FILENAME", help="Validate a candidate from stdin; return a structured JSON report")
    args = parser.parse_args(argv)
    if args.stdin:
        print(json.dumps(validate_candidate(args.stdin, sys.stdin.read()), ensure_ascii=True))
        return 0
    validators = load_validators()
    success = True
    files = []
    for path in args.paths:
        if path.is_dir():
            files.extend(path / filename for filename in validators)
            for extra in sorted(path.glob("*.config")):
                if extra.name not in validators:
                    print(f"{extra}: no schema registered for this filename", file=sys.stderr)
                    success = False
        else:
            files.append(path)
    for path in dict.fromkeys(files):
        validator = validators.get(path.name)
        if validator is None:
            print(f"{path}: no schema registered for this filename", file=sys.stderr)
            success = False
        elif not validate_file(path, validator):
            success = False
    return 0 if success else 1


if __name__ == "__main__":
    sys.exit(main())
