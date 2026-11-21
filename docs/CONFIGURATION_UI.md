# Configuration administration

Open `/admin/#configuration` on the ONVIF HTTP port. The catalog contains media
profiles, ONVIF configuration entities and an advanced editor for supported
JSON `.config` files. The active settings and association tables below the editor
remain a view of the running application. Account passwords are never returned.

## Media profiles and configurations

Select a profile to change its name and configuration associations. Each selector
has a Not attached option for removing an association. Analytics and PTZ choices
respect the selected video source's compatible analytics configurations and PTZ
nodes. Metadata choices are unavailable when metadata transport is disabled.
Server validation checks all references, unique tokens, metadata source requirements,
compatible bindings, metadata filters and native analytics rule validation.
Incomplete profiles can be assembled incrementally, as with native profile creation.

Create media profile creates a non-fixed profile with a generated unique token.
Delete profile is disabled for fixed profiles; the server also rejects deleting
a fixed profile or changing its fixed flag to false through the advanced editor.
Changes are validated and persisted before replacing the shared media tree.
The status API, Media and Media2 read the same tree. Existing configuration tokens
and derived UseCount fields are read-only in entity forms. Reference counts are
recomputed when saving media changes.

Select an ONVIF configuration entity to edit its settings. Nested settings use
field groups; arrays use JSON inputs. Metadata supports Events, Analytics, Filters
and AnalyticsModules, including adding optional settings. Empty arrays remain
arrays when later native operations save the shared configuration. Analytics
rules are edited in the existing Detectors panel. Both Media1's
VideoEncoderConfigurations and Media2's VideoEncoder catalogs are exposed under
their file names; they remain separate configured representations.

Profile/metadata-only changes apply to new RTSP connections using the existing
metadata control. Active sessions retain their previous snapshots. Changing
other configuration entities requires an application restart for full effect:
SOAP readers can already observe saved descriptor values, while analytics state,
service initialization and stream factories can still hold earlier snapshots.
Some audio/video pipeline parameters remain hardcoded by the emulator; editing
an advertised encoder setting does not guarantee the actual media changes.

## Configuration files and restart

The advanced editor exposes registered files except `users.config`, managed in
**Users & authentication**. That section provides account creation, deletion and
enable/disable controls plus the ONVIF authentication selector. It shows the active
scheme separately from the saved scheme; both account and scheme edits require
restart through System. Existing passwords remain write-only and are preserved
when other accounts change. The server rejects removal or disabling of the last
enabled administrator. Concurrent edits return a conflict and require reload.
File names are selected from a server whitelist; arbitrary paths are rejected.
Each editor has Save configuration and Cancel edits controls. Raw JSON editors
preserve JSON types and empty objects/arrays. Full-document JSON Schema validation
runs before any temporary configuration file is created. Semantic media validation
runs before writing. A failed save keeps both the current file and running media
model intact, and the browser keeps the draft open for correction.

Changes to application/service files are persisted for the next application
restart. They do not modify the current listener, authentication, detector timers
or service caches. A successful-save dialog, persistent banner and status message
explicitly state when restart is needed. Use System / Restart application to reload files;
if the HTTP endpoint changes, reconnect at its new address. Environment variables
continue to override their corresponding file settings. The current browser draft
is protected from automatic refresh; explicit reload asks before discarding edits.

The `users.config` document represents existing passwords as
`__UNCHANGED_SECRET__`. Leave that marker to preserve the password for the same
login, even if accounts are reordered; provide a real password for a new/renamed
account. New values are write-only. Uniqueness of logins and RTP port ordering/parity
are checked in addition to the schema. Changing authentication requires restart.

A read-only Docker mount or directory permission/disk failure returns HTTP 507
with `ConfigurationWriteFailed`. The dialog explains that saving failed and asks
the user to check write permissions, read-only mounts and available disk space.
Mount the complete configuration directory with write access for persistence;
atomic saving creates a temporary file in that directory before renaming it.

## JSON interface

`GET /admin/api/configuration` returns `files` (name, opaque revision, JSON document
string), `metadata_enabled` and `restart_required`. Documents contain saved values;
these are distinct from active settings returned by `/admin/api/status`.

`POST /admin/api/configuration` accepts:

```json
{
  "file": "media_profiles.config",
  "revision": "revision returned by GET",
  "document": "complete candidate JSON document"
}
```

It requires `Content-Type: application/json`, `X-ONVIF-Admin: configuration` and the
same origin checks as the other administration routes. Requests are limited to
1 MiB. Success returns `status: saved` and a boolean `restart_required` for this
save. GET reports whether any save in this application run requires restart.
Opaque revisions plus disk comparisons reject stale browser saves/external edits
with HTTP 409, including concurrent native ONVIF mutations. Reload before retrying.
If the media file was edited externally and differs from the running shared tree,
restart to load it before attempting another media save.

Schema errors return 422 with file/path/message entries; an unavailable validator
returns 503. Invalid references, fixed-profile changes or native semantic failures
return 400. Write failures return 507. No password values are included in responses
or journal entries. The journal records the file name after a successful save.
These routes require the WebUI administrator session, including for reads.

## Contract and validation

The editor adds an emulator administration API, without new SOAP actions or XML
elements. Profile and metadata behavior use the existing shared model and the
[Media/Media2 and metadata contract references](RTSP_METADATA.md#contract-and-validation)
(ONVIF 24.12 at commit d7f3684501c4a6d58f879e72e1e3322c120f95e6).
Analytics validation uses the existing [Analytics contract](ANALYTICS.md#contract-and-references).
Configuration-file edits with a restart requirement are emulator settings, not
claims that equivalent SOAP setters or hardware media reconfiguration exist.

Use the documented Docker dev environment:

```bash
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
python3 unit_tests/configuration_ui_smoke.py
python3 unit_tests/configuration_ui_browser.py
```

The browser test requires Chromium, chromedriver and Selenium in the development
environment. For read-only mount acceptance, run the smoke test's `--read-only`
mode against a server whose configuration directory is mounted read-only.
