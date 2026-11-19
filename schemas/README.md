# Configuration JSON Schemas

These independently written schemas describe the emulator's JSON configuration
files, based on its loaders and handlers. They use
[JSON Schema Draft 2020-12](https://json-schema.org/draft/2020-12/json-schema-core).
They are not ONVIF WSDL/XSD schemas and do not certify protocol conformance.

`config-files.json` maps each case-sensitive configuration filename to its schema.
`types.schema.json` provides shared definitions through relative `$ref` links.
`analytics-rule.schema.json` describes persisted Motion Region, Line Detector,
LineCounting, Field Detector and License Plate Recognition rules referenced by the media profiles schema.
Polygon geometry, normalized coordinate bounds and cross-rule resource limits
are checked by the C++ Rules implementation.
Keep this directory intact when using the schemas with other validators.
References resolve relative to the schema files. The bundled checker registers
local file URIs and does not download schemas.

## Validate after editing

From the repository root, create a separate environment for the optional checker:

```bash
python3 -m venv .venv-config-validation
.venv-config-validation/bin/python -m pip install -r schemas/requirements.txt
.venv-config-validation/bin/python scripts/validate_configs.py
```

Validate a copied configuration directory or selected files:

```bash
.venv-config-validation/bin/python scripts/validate_configs.py /tmp/my-server-configs
.venv-config-validation/bin/python scripts/validate_configs.py \
    server_configs/common.config 'server_configs/recording_search.config'
```

A directory check requires all registered files and rejects unknown `.config`
filenames. Individual file checks select the schema by filename. Errors include
the file and a JSON Pointer to the failing field. Exit status is `0` on success,
`1` on validation/file errors, and `2` on invalid command-line arguments.
Duplicate JSON properties and non-JSON constants such as `NaN` are rejected.
The checker never rewrites configuration and is not run automatically at server
startup. Python dependencies are optional development tools; the C++ server does
not use them. The main validator dependency and its `referencing` dependency
are MIT licensed; neither is vendored in this repository.

Run schema regression checks separately from the C++ tests:

```bash
.venv-config-validation/bin/python unit_tests/config_schema_tests.py
```

## Editor and configurator integration

Associate `.config` files with JSON in the editor and map filenames to schemas
using `config-files.json`. For example, VS Code workspace settings can include:

```json
{
  "files.associations": { "*.config": "json" },
  "json.schemas": [
    {
      "fileMatch": ["/server_configs/common.config"],
      "url": "./schemas/common.schema.json"
    },
    {
      "fileMatch": ["/server_configs/media_profiles.config"],
      "url": "./schemas/media-profiles.schema.json"
    }
  ]
}
```

Repeat the mapping for other filenames as needed. Do not add `$schema` to the
configuration files themselves: some service handlers iterate over configuration
properties, and the schemas reject unknown fields.

A future GUI can reuse the filename mapping, field types, required fields,
descriptions, and enumerations. Profile tokens and collection sizes are not fixed
to the shipped examples. Newly created, unattached profiles are accepted.
Most objects reject unknown properties to catch typos; namespace and scope maps
allow arbitrary keys. Known historical numeric/boolean strings are accepted
alongside native JSON scalars in shared types. This also accounts for
Boost.PropertyTree writing scalar values as strings in `media_profiles.config`.
Defaults in the application do not cause the validator to insert values.

## Validation boundaries

The schemas check file structure, required fields, scalar representations,
selected enumerations, and local constraints such as port limits, RTP parity,
and conditional file-streaming settings. They describe current configuration
storage; an accepted codec or capability value does not imply streaming support.
Native integer fields in shared startup settings should be written as JSON
numbers. Duration checks cover nonnegative day/time durations used by these
settings. Recording dates check the basic lexical shape, not calendar validity.

JSON Schema cannot compare arbitrary values or resolve token references between
these files. Passing validation does not establish:

- Unique tokens or logins, valid profile/source/encoder/PTZ references, or
  agreement between the Media and Media2 catalogs.
- Ordered RTP ports, range endpoints, or recording dates; complete calendar
  validity; correspondence between advertised services and implemented behavior.
- Available input/response files, reachable advertised addresses, local discovery
  interfaces, free ports, or valid environment overrides.
- A realizable user topology or support for the requested codecs, resolutions,
  authentication modes, or capabilities in the running emulator.

The shipped examples retain existing simulation limitations and inconsistencies.
For example, event source labels need not resolve to the configured media source
tokens. A future topology configurator needs a separate semantic validation layer
for relationships, uniqueness, capability consistency, and runtime resources.
Profile changes also require accounting for the separate `ProfilesConfig()` and
`MediaProfilesManager` views described in the
[architecture overview](../docs/architecture.md#configuration-and-state).

When changing configuration, update the corresponding schema, consumers,
examples, and regression checks together. Do not infer protocol correctness from
these local storage schemas.
