# Detector administration

Open `/admin/#detectors` on the existing ONVIF HTTP listener. The detector panel
lists built-in detectors and rule-based analytics, displays current settings, and
provides a uniform-color schematic frame with editable geometry overlays. It
uses embedded HTML/CSS/JavaScript and the same C++ configuration and validation
model as the SOAP Analytics service. It does not analyze an actual video frame.

## Detector list and controls

The built-in list contains Digital Inputs Alarm, Motion Alarm, Cell Motion and
Audio Detection. Buttons are green for enabled detectors and red for disabled
ones; their accessible labels also state the status. Enabled switches live in
the selected item's settings, including the rule engine and individual rules.
Rule button colors include the parent and detector-type enable gates.

Built-in detectors and the six rule-based types expose **Enabled**, **Auto generate
events** and **Event generation timeout (seconds)**. Save changes validates the
complete candidate `event.config`, atomically persists it, and reschedules the
corresponding timer on the event worker without a restart. If event.config has
changed externally since startup/the last save, the server returns 409 instead of
overwriting those changes; restart the application to load external file edits. Intervals accept whole
seconds from 1 through 2147483647. Topics and source/token bindings are read-only;
the running generators capture these at startup. Invalid saves open the existing
validation dialog and leave the file and running settings unchanged.

Rule-based Analytics groups rules by Motion Region Detector, Line Detector,
LineCounting, Field Detector, License Plate Recognition and Face Recognition.
Select Detector settings within an expanded type to edit the settings shared by
all its rules and configurations. Individual rule settings show the inherited
automatic mode and interval and provide a link to the detector settings.

`Enabled` defaults to true independently of `AutoGenerateEvents`. Disabling auto
generation leaves manual execution available. `AutoGenerateEvents` is the
only supported configuration key for automatic generation. The singular per-input
`GenerateEvent` in `device.config` remains a separate input simulation flag.
All detectors and rule types default to a 30-second event generation interval.
Individual rules inherit the interval of their detector type.

Saved detector/type settings survive application restart. The rule engine and
individual rule enable switches remain runtime controls and reset on restart.
These switches are separate from standard rule parameters, such as Motion
Region's persisted `Armed` parameter.

**Run detector once** invokes the same generator method used by the automatic
timer, on the source's worker, without changing the timer deadline. It is blocked
in both the browser and server when the detector, rule or rule engine is disabled.
Save pending edits before running it. A run advances one simulation step; it may
emit zero, one or multiple notifications depending on rule geometry, arming,
thresholds and state transitions. It does not manufacture a successful detection.
A rule-level run publishes only that configuration/rule's resulting notifications,
although the shared synthetic simulation state advances for the whole type.
Manual events use the ordinary publication path for the journal and all clients;
no PullPoint or metadata connection is used to trigger them.

`DetectorControls` gates live notifications in `EventSource::Publish`, before the
internal journal and external subscriber queues. It also gates per-client
synchronization snapshots. Disabled detectors/rules therefore produce no new
journal, PullPoint or event-metadata publications after the control takes effect.
Messages already queued or in flight are not recalled; existing journal history
is retained. A source message carries an internal analytics configuration token
to distinguish equal rule names. This token is not added to ONVIF XML, and its
size is included in internal queue accounting.

This is an emulator-specific notification control, rather than a standardized
ONVIF arming operation. Shared Line Detector input sampling and state calculations continue
in the background, even with its auto generation off. Its output remains gated
by each destination type's AutoGenerateEvents setting. Other generators pause
their automatic timers when automatic mode is off. This keeps shared object tracking, counts and scene metadata
working when another rule type is paused; resuming does not reset accumulated
counts. An intentional simulation deviation is that synchronization snapshots for
paused detectors/rules are withheld, even though their rules remain visible in
GetRules/GetEventProperties. The scope is this administration pause control; the
reason is to make disabling a detector suppress its event output consistently
across all transports. No new ONVIF control capability is advertised.

## Rules and scene geometry

Create rule selects a canonical default template from the existing rule parser.
Existing rules can be selected, edited, saved or deleted. The application retains
its existing limit of **four rules total** across all types and configurations.
Names identify rules and cannot be renamed in place. The type can be changed,
loading that type's defaults while retaining the rule name. Parameter fields
include threshold, sensitivity, direction, image options and other supported
settings. Arrays such as LineCounting reset times use a JSON-array input.

Geometric elements are drawn over a schematic frame:

- Motion Region: a polygon in pixels, using the associated source Bounds and its
  x/y origin. Armed and Sensitivity remain the native MotionRegion attributes.
- Line Detector: an ordered polyline in normalized coordinates.
- LineCounting: one or more ordered normalized polylines.
- Field Detector: a normalized polygon.
- License Plate Recognition: optional normalized Region and SnapLine elements,
  which can be added or removed.
- Face Recognition: no configurable geometry; the current implementation exposes
  image mode and threshold parameters instead.

Normalized x increases from -1 to 1 left to right; normalized y increases from
-1 to 1 bottom to top. Pixel y increases downwards. Line arrows indicate vertex
order; the Direction parameter selects the crossing side. Vertex handles support
pointer dragging, and labelled numeric x/y inputs provide a keyboard alternative.
Vertices can be added or removed within supported limits; server validation is
authoritative for geometry, ranges, intersections and cardinality. Counting
supports adding/removing its polylines. The frame and overlays show settings,
rather than actual detections, camera video, or face/plate recognition results.

The UI refreshes settings periodically while not editing and preserves unsaved
edits. The server checks the previous canonical rule on modify/delete and returns
409 if another client changed it. Cancel edits or Refresh detectors reloads the
current server state. A failed/uncertain request does not claim success and asks
the user to inspect current settings before retrying.

## Persistence and shared model

Rule mutations use `AnalyticsRules::RuleXml` and `AnalyticsRules::Execute` in the
same process; they are not HTTP/SOAP requests to the server itself. The existing
parser validates the complete candidate and its source bindings and limits, then
the existing manager callback saves `media_profiles.config` before committing
state. Media, Media2, Analytics and the WebUI share these changes. A writable
configuration mount is required for rule editing. Failed validation or persistence
leaves the existing rule intact. Before any file is written, the full candidate
JSON is checked against the bundled configuration schema. A rejected save opens
a modal with file, field path and reason and keeps the draft for correction; see
[save validation](../schemas/README.md#server-save-gate-and-webui-errors). Rule changes and saved detector settings survive SystemReboot; the rule engine
and individual rule runtime switches do not. Creating/deleting a rule through either interface removes any
stale runtime switch for that name. Deleting a paused rule may therefore publish
a final Deleted lifecycle notification if its type and parent remain enabled.

The applicable ONVIF contract versions and normative references remain those in
[Analytics Rules](ANALYTICS.md#contract-and-references): Analytics Specification
26.06, Analytics WSDL 20.12, Rules schema 19.06 and their documented compatible
imports. This change reuses their existing validator and serializers and adds no
ONVIF elements or operations. Administration JSON and runtime pause controls are
emulator extensions. Third-party specifications/schemas are not redistributed.

## JSON interface

Both methods use `/admin/api/detectors` and require the WebUI administrator
session for both reads and writes, independently of SOAP Digest settings.

GET returns `simple`, `families`, `rules_enabled`, `max_rules` and
`configurations`. Configuration entries have token/name, compatible source bounds,
canonical `rules` and default `templates`. Rule `enabled` fields describe runtime
switches and are not stored as rule parameters. Settings scalars follow the
existing PropertyTree string convention. The rule parent flag is a JSON boolean,
and list fields are JSON arrays even when empty. Detector/type entries include
`settings` and `manual_available`. Configurations without a
compatible source have no creation templates.

POST requires `Content-Type: application/json`, `X-ONVIF-Admin: detectors` and a
same-origin request when Origin/Fetch Metadata are supplied. Bodies are limited
to 32 KiB. Supported commands:

| action | Required data |
| --- | --- |
| `settings` | `id` (built-in ID or `tt:` type), `settings` containing editable keys `Enabled`, `AutoGenerateEvents`, `EventGenerationTimeout` |
| `manual` | `id`; optional `configuration` and `name` to scope a rule run |
| `detector` | `id` (`rules`, built-in ID, or `tt:` rule type), `enabled` (runtime override) |
| `toggle` | `configuration`, `name`, `enabled` |
| `create` | `configuration`, `name`, canonical `rule` |
| `modify` | `configuration`, `name`, canonical `rule`, canonical `previous` |
| `delete` | `configuration`, `name`, canonical `previous` |

For `previous`, omit the WebUI-only `enabled` field. Successful changes return
`200 {"status":"updated"}`. Invalid data/rules receive 400; missing same-origin
request headers receive 403; wrong content type receives 415; stale rules, resource
limits and persistence/configuration conflicts receive 409. Full-document JSON
schema errors receive 422 with `validation_errors`; an unavailable checker
receives 503. Both prevent the file write. Responses use the
same no-cache, framing and content policies as other administration endpoints.

## Validation

Use the [Docker dev workflow](../BUILD.md#reusable-development-environment):

```bash
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
python3 unit_tests/detectors_ui_smoke.py --network-smoke
# Optional Chromium/Selenium browser acceptance:
python3 unit_tests/detector_controls_browser.py
```

CTest covers default templates and CRUD for all six types, stale edit rejection,
per-rule name isolation, disabled publication/synchronization, re-enabling and
journal isolation. Existing suites cover geometry, persistence faults and ONVIF
serialization. The smoke test runs a disposable source-built process, exercises
actual event suppression/resumption at rule/type/parent level, enables an
initially automatic-off built-in detector, checks invalid/stale edits, verifies
GetRules visibility and persistence through exec, and deletes its rule. Its
optional network phase runs network_smoke.py, event_delivery_smoke.py and
analytics_smoke.py after controls have returned to their defaults.

Browser acceptance should check desktop/mobile layouts, type selection,
parameter edits, dragging and numeric vertex editing, line direction/order,
unsaved edits, stale save errors and delete/cancel controls. Automated Linux
checks do not replace browser inspection or Windows/VMS interoperability checks.
