# Configuration JSON Schemas

These independently written schemas describe the emulator's JSON configuration
files, based on its loaders and handlers. They use
[JSON Schema Draft 2020-12](https://json-schema.org/draft/2020-12/json-schema-core).
They are not ONVIF WSDL/XSD schemas and do not certify protocol conformance.

`config-files.json` maps each case-sensitive configuration filename to its schema.
`types.schema.json` provides shared definitions through relative `$ref` links.
`analytics-rule.schema.json` describes persisted Motion Region, Line Detector,
LineCounting, Field Detector, License Plate Recognition and Face Recognition rules referenced by the media profiles schema.
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
startup. The server now invokes the same checker before persisting its managed
configuration; Python 3, jsonschema and referencing are runtime dependencies for
that save gate. CMake checks the imports and Docker dev/build/runtime stages
include the required Debian packages. The main validator dependency and its `referencing` dependency
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

The WebUI save gate reuses these schemas and their filename mapping. Other
configurators can reuse the field types, required fields,
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
Profile changes share an authoritative `ProfilesConfig()` /
`MediaProfilesManager` tree, with immutable snapshots published to RTSP as described in the
[architecture overview](../docs/architecture.md#configuration-and-state).

When changing configuration, update the corresponding schema, consumers,
examples, and regression checks together. Do not infer protocol correctness from
these local storage schemas.

## Server save gate and WebUI errors

The server installs a `ConfigsReaderWriter::BeforeSave` hook for its shared media
profile manager. The writer serializes the complete future document once and
validates those exact bytes before creating a temporary configuration file. On
success the existing atomic rename writes the validated document; on rejection
existing mutation transactions restore their prior model. Standalone uses of
`ConfigsReaderWriter` can install the same hook explicitly with `ValidateConfig`;
the raw reader/writer does not guess a schema for arbitrary fixture filenames.
Currently the WebUI persists rules through this manager; overview settings and
runtime enable switches do not write config files.

The C++ adapter runs the bundled checker through `posix_spawn`, without a shell.
Candidate input, validator output and diagnostics use anonymous memory file
descriptors. Other server descriptors are closed in the child. The checker uses
only bundled schemas and never retrieves remote references. A five-second timeout,
a failed process/import, or missing resources blocks the write rather than
bypassing validation. Geometric and ONVIF semantic validation remains separate.

CMake stages `config_validation/scripts/validate_configs.py` and the complete
schema set alongside the build. When distributing the executable, copy its
`config_validation/` directory alongside it. Docker copies both executable and
resources out of the build cache into `/app`. Native builds record the selected
Python interpreter; the runtime environment must supply that interpreter and
its dependencies. A source-build fallback locates staged resources for CTest's
nested executable directory. Validation does not depend on the process cwd.

The internal machine interface is:

```bash
python3 scripts/validate_configs.py --stdin media_profiles.config < candidate.json
```

It returns JSON with `valid`, `errors` and an optional `truncated` flag. Each issue
contains `file`, a JSON Pointer `path`, and a short `message`. At most 20 issues
are returned. Messages describe constraints without printing configuration
values, which could contain credentials. Duplicate keys, NaN and invalid syntax
are also rejected. The existing human-readable CLI and exit codes are unchanged;
the stdin mode returns a report for a completed validation, including rejection.
A broken validator exits unsuccessfully and the C++ caller reports unavailability.

The WebUI returns HTTP 422 / `ConfigValidationFailed` with `validation_errors` for
schema rejection and HTTP 503 / `ValidationUnavailable` if the checker cannot
run. Both leave the target file unchanged. The save/apply action shows a modal
with file, field path and reason, retaining input for correction. Parameter,
geometry, stale-edit, malformed-request and transport errors also open the modal;
transport errors do not claim that a save definitively failed. Ordinary ONVIF
rule operations preserve their ConfigurationConflict SOAP fault behavior instead
of exposing administration JSON error details.

Validation tests include:

```bash
ctest --test-dir build --output-on-failure
python3 unit_tests/config_schema_tests.py
python3 unit_tests/config_validation_smoke.py
```

The smoke test copies installed runtime artifacts into a temporary layout, checks
a full-document rejection outside the edited rule, unchanged file/model and no
temporary config file on rejection, missing-resource failure, and successful save
after correction. It does not replace browser acceptance of modal focus, close,
Escape, field diagnostics and retained draft values.

Optional browser regression (a test-only image/environment with Chromium,
chromium-driver and python3-selenium installed):

```bash
python3 unit_tests/config_validation_browser.py
```

This checks that Save opens the schema-error modal, details exclude values,
closing it preserves the draft, local required-field failures use the modal,
Escape dismisses it, and saving succeeds after correction and reload.
