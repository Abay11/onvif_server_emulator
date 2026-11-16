# Working in This Repository

## Context

ONVIF Server Emulator emulates a camera/NVR using C++20 on Linux. Before changing
code, read [docs/architecture.md](docs/architecture.md), then inspect the relevant
sources. Build instructions are in [BUILD.md](BUILD.md), configuration is described
in [README.md](README.md), and Windows/WSL2 networking is covered in
[docs/WSL2.md](docs/WSL2.md). Documentation describes the current design; verify
actual operation and parameter support against handlers and tests.

## Where to Make Changes

- Startup, shared settings, and base URLs: `main.cpp`, `Server.*`.
- Service factory and shared context: `include/IOnvifServer.h`, `src/IOnvifServer.cpp`.
- SOAP: `onvif_services/<service>_service.*`; shared dispatcher:
  `IOnvifService.cpp`; base handler: `onvif/OnvifRequest.h`.
- Events: `onvif_services/event_service.cpp`, `onvif_services/pullpoint/`, and
  `onvif_services/physical_components/`. The Event service has its own dispatcher,
  separate from `IOnvifService`.
- Discovery: `onvif_services/discovery_service.*`; RTSP pipelines and replay: `RtspServer.*`.
- XML, SOAP, authorization, and media profiles: `utility/`; recording models: `onvif/Recording.*`.
- Configuration and resources: `server_configs/`; checks and fixtures: `unit_tests/`.

## Build and Run

Run commands from the repository root. Linux is supported; on Windows, use the
documented WSL2/container deployment.

```bash
git submodule update --init --recursive
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DENABLE_UNIT_TESTS=ON
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
./build/main ./server_configs
```

Dependencies include Boost (with Unit Test Framework for tests), GStreamer RTSP
Server, pkg-config, and a C++20 compiler with `std::format` support. Package lists
and the container `dev` target are documented in `BUILD.md`. If dependencies are
missing, report the specific reason the build or tests could not run.

The application defaults to `./server_configs` relative to its working directory.
A normal CMake application build does not copy configuration beside the executable:
pass the path explicitly or run from the repository root. Starting the server
occupies HTTP/RTSP/UDP ports; use a configuration copy when testing profile changes.

## Change Guidelines

- Write all documentation and code comments in English. Use English when adding
  or updating documentation or comments.
- Follow the surrounding style. `.clang-format` specifies Microsoft style, tabs
  of width 2, and a 120-column limit. Avoid formatting unrelated files in full.
- Preserve the existing header/implementation split. Source lists in both
  `CMakeLists.txt` files are explicit: add new `.cpp` files to the appropriate target.
- For SOAP operations, check handler registration, operation names, namespaces,
  security levels, and fault responses. Use the existing XML/SOAP/HTTP helpers.
- Keep bind and advertised endpoints distinct. Build client URLs through
  `ServerConfigs::HttpAddress()`, `RtspAddress()`, and `PullPointAddress()`.
  Check consistency across discovery, Device, Media/Media2, replay, and events.
- `.config` files contain JSON and their names are case-sensitive. When changing
  the schema, update the loader, examples, and affected fixtures. Verify where a
  setting is consumed before assuming it works; pipelines are partly hardcoded.
- `MediaProfilesManager` writes to `media_profiles.config`; the separate
  `ProfilesConfig()` tree does not update automatically. Account for both views.
- For asynchronous code, check callback dependency lifetimes, timers, shutdown,
  and thread joins. The Event service stores global state; complete restart and
  multiple independent servers within one process are not supported reliably.
- `Simple-Web-Server/` is a third-party Git submodule. Changes normally belong in
  the emulator; any submodule edits or revision changes must be directly relevant
  to the task. Do not edit generated files in `build/`.
- Preserve existing user changes and unrelated untracked files.

## Public API and ONVIF Contract

The server's public API, accepted requests, and emitted responses must conform
syntactically and semantically to the applicable
[ONVIF specifications](https://www.onvif.org/profiles/specifications/), service WSDL
files, and XSD schemas, including their imported definitions. Treat these documents
as the authoritative contract for implemented functionality.

- Before changing an operation, identify the applicable specification version,
  service WSDL, and schemas. Record the references used in the change description
  or relevant documentation; do not silently mix incompatible versions.
- Check syntax against WSDL/XSD: operation and action identifiers, namespace URIs,
  element and attribute names, element order, cardinality, required/optional fields,
  data types, and allowed values. Well-formed XML alone is insufficient.
- Check semantics against the specification: parameter meaning, token references,
  state transitions, time ranges, units, capabilities, access rules, and required
  fault behavior. Schema validity alone does not establish semantic correctness.
- Keep advertised services and capabilities consistent with implemented behavior.
  Apply the same contract to discovery, events, media, and replay, including the
  transport and addressing requirements defined by the applicable specifications.
- For protocol changes, add or update representative request/response and fault
  checks. Validate XML against the applicable schemas where practical and test
  semantic behavior separately. Existing fixtures and implementation behavior are
  not substitutes for the normative contract.
- Document any intentional simulation deviation with its scope and reason. Do not
  introduce an undocumented deviation or describe existing limitations as compliant.

## Intellectual Property and Licensing

- Respect ONVIF's and other rights holders' copyrights, trademarks, patents, and
  applicable license terms. Public availability of a specification, WSDL, XSD,
  example, or tool does not by itself grant unrestricted redistribution rights.
- Before copying or modifying third-party material, check the terms for the
  specific file and version, including the
  [ONVIF specifications repository](https://github.com/onvif/specs) and applicable
  [website terms](https://www.onvif.org/terms-of-use/). Prefer links and independently
  written explanations to reproducing specification text. For permitted copies,
  retain required copyright notices, license text, attribution, and modification
  notices; record provenance and version, including for generated artifacts.
- Check dependency licenses and distribution obligations when adding libraries,
  GStreamer plugins/codecs, generated code, or container contents. Do not assume
  that the project's license covers third-party materials or grants patent rights;
  consult the applicable [ONVIF patent declarations](https://www.onvif.org/profiles/specifications/patent-declarations/)
  where relevant. Do not infer member-only rights from public access.
- Use the ONVIF name descriptively without implying affiliation or endorsement.
  Do not add ONVIF logos or profile symbols without verifying usage rights. Do not
  claim official ONVIF conformance unless the specific product/software version
  has completed the [ONVIF conformance process](https://www.onvif.org/conformant-products/)
  and the claim is authorized under the applicable policies.
- If rights for a proposed inclusion or distribution are unclear, document the
  issue and obtain clarification from the rights holder or qualified legal counsel
  before including or distributing that material. Continue independent work that
  does not depend on the unresolved rights. These rules do not certify legal compliance.

## Validation

For C++ changes, build the affected targets and run CTest using the commands above.
Tests that modify configuration must use copies: CMake creates `unit_tests/fixtures`
inside the build directory and sets the CTest working directory. Prefer CTest to
running `test_executable` manually from an arbitrary directory. For behavior fixes,
add coverage of the observable result to the appropriate Boost.Test suite;
register new test `.cpp` files in `unit_tests/CMakeLists.txt`.

For network changes, also run `python3 unit_tests/network_smoke.py` against a running
server, using addresses appropriate to your environment as described in `BUILD.md`.
The script expects default service settings and disabled authentication; it also
checks multicast discovery and UDP media. Results depend on interfaces and network
conditions. Unit tests do not replace the Windows/VMS checks in `docs/WSL2.md`.

For documentation-only changes, check links, commands, and consistency with the
sources; rebuilding the application is optional. Update the relevant documentation
when changing architecture, configuration, build steps, or startup behavior.
In the final response, report the outcome, checks performed, and material limits
of the validation.
