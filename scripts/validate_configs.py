#!/usr/bin/env python3
"""Validate emulator configuration files against the bundled JSON Schemas."""

import argparse
import json
from pathlib import Path
import sys

try:
    from jsonschema import Draft202012Validator, FormatChecker
    from referencing import Registry, Resource
except ImportError:
    sys.exit("Install validation dependencies: python3 -m pip install -r schemas/requirements.txt")

SCHEMAS_DIR = Path(__file__).resolve().parents[1] / "schemas"


def load_json(path):
    def unique_keys(pairs):
        result = {}
        for key, value in pairs:
            if key in result:
                raise ValueError(f"Duplicate JSON property: {key}")
            result[key] = value
        return result

    def reject_constant(value):
        raise ValueError(f"Invalid JSON constant: {value}")

    with path.open(encoding="utf-8") as source:
        return json.load(source, object_pairs_hook=unique_keys, parse_constant=reject_constant)


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
        config = load_json(path)
    except (OSError, ValueError) as error:
        print(f"{path}: {error}", file=sys.stderr)
        return False
    errors = sorted(validator.iter_errors(config), key=lambda error: tuple(str(p) for p in error.absolute_path))
    for error in errors:
        pointer = "/" + "/".join(str(p).replace("~", "~0").replace("/", "~1") for p in error.absolute_path)
        print(f"{path}:{pointer}: {error.message}", file=sys.stderr)
    if not errors:
        print(f"OK: {path}")
    return not errors


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("paths", nargs="*", type=Path, default=[Path("server_configs")],
                        help="Complete configuration directories or individual .config files (default: server_configs)")
    args = parser.parse_args(argv)
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
