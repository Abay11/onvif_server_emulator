# Analytics Rules: Motion Region, Line Detector, LineCounting and Field Detector

The Analytics endpoint `/onvif/analytics_service` implements the Rules interface
for `tt:MotionRegionDetector`, `tt:LineDetector`, `tt:LineCounting` and `tt:FieldDetector`.
This is an emulator with deterministic synthetic input. It does not inspect the RTSP stream or claim Profile M certification.

## Contract and references

The implementation targets sections 5.2.2, 6.1, 6.2, Annex A.1/A.2/A.3/A.5 and Annex C.1 of the
[Analytics Service Specification 26.06](https://www.onvif.org/specs/srv/analytics/ONVIF-Analytics-Service-Spec.pdf).
The wire format uses the published
[Analytics WSDL 20.12](https://www.onvif.org/ver20/analytics/wsdl/analytics.wsdl),
[Rules schema 19.06](https://www.onvif.org/ver20/analytics/rules.xsd),
[ONVIF schema 25.12](https://www.onvif.org/ver10/schema/onvif.xsd) and its
[common schema 25.06](https://www.onvif.org/ver10/schema/common.xsd).
The WSDL also imports [metadata types 25.12](https://www.onvif.org/ver10/schema/metadatastream.xsd).
These are the compatible published wire definitions used with specification
26.06, rather than an assumption that every document has the same version number.
All imported definitions remain authoritative.

Media2 configuration serialization is checked against
[Media2 WSDL 26.06](https://www.onvif.org/ver20/media/wsdl/media.wsdl) and
`VideoAnalyticsConfiguration` / `ConfigurationEntity` in ONVIF schema 25.12.
The required child order is `tt:Name`, `tt:UseCount`,
`tt:AnalyticsEngineConfiguration`, `tt:RuleEngineConfiguration`.
`tt:Count` is invalid here. Although deprecated for Media2, `UseCount` remains
required by this schema and reports the number of profiles referencing the
configuration. The shared Media/Media2 serializer and the Analytics smoke test
check this structure. A response containing `tt:Count` indicates an older or
different implementation; rebuild and restart the executable or container
actually serving the tested endpoint.

The Motion Region references were inspected on 2026-11-17; the Line Detector and
LineCounting contracts were checked against the same versions on 2026-11-18.
SHA-256 values for reproducibility:

| File | SHA-256 |
| --- | --- |
| `analytics.wsdl` | `eeeff5a4776f318b1b56038f704802412eb4066d831c59518bb352a09d026d728` |
| `rules.xsd` | `02bb948a3ea0c37e66bcc296f8ef372e566b8680d84fecc384c72b679e2ed0a8` |

No specification or schema files are bundled with this implementation. The
explanations and request example below are independently written.

## Supported operations

`GetSupportedRules`, `GetRuleOptions`, `GetRules`, `CreateRules`, `ModifyRules`
and `DeleteRules` use a VideoAnalyticsConfiguration token, by default
`AnalyticsConfig_0`. Media's `GetVideoAnalyticsConfigurations` and Media2's
`GetAnalyticsConfigurations` return the same persisted rules. Read operations use
READ_MEDIA access; mutations use ACTUATE under the existing digest authorization.

The device supports up to four rules in total across all four types and all analytics
configurations.

## Motion Region Detector (Annex C.1)

Motion Region supports independent sensitivity per
region, disarming, rule names in notifications, and simple polygons of three to
eight vertices. The implementation rejects self-intersecting, degenerate and
out-of-bounds polygons, unsupported rule types and PTZ preset associations.
`PTZPresetMotionSupport=false` is an attribute of the options payload. Options
children belong to the analytics rules namespace; polygon points belong to the
ONVIF schema namespace.

Each rule has one `MotionRegion` ElementItem containing
`axt:MotionRegionConfig`. `Armed` and `Sensitivity` are attributes. Sensitivity is
finite and lies in `[0, 1]`. Polygon coordinates are pixels inside the associated
VideoSourceConfiguration's Bounds, rather than normalized scene coordinates.
An empty Parameters element on creation selects an armed region covering the
source Bounds with sensitivity 0.5. Omitted fields on modification retain their
values. Omitting the Polygon on creation selects the full Bounds.

Create, modify and delete requests process all entries as a batch. A validation
failure leaves the rules and event state unchanged. Fault subcodes distinguish
`NoConfig`, `InvalidRule`, `RuleAlreadyExistent`, `RuleNotExistent` and
`TooManyRules`; the last is a Receiver/Action fault. Element prefixes may vary;
namespace URIs and QName bindings determine the rule type.

For example, send this SOAP 1.2 request with action
`http://www.onvif.org/ver20/analytics/wsdl/CreateRules`:

```xml
<s:Envelope xmlns:s="http://www.w3.org/2003/05/soap-envelope"
            xmlns:tan="http://www.onvif.org/ver20/analytics/wsdl"
            xmlns:tt="http://www.onvif.org/ver10/schema"
            xmlns:axt="http://www.onvif.org/ver20/analytics">
  <s:Body>
    <tan:CreateRules>
      <tan:ConfigurationToken>AnalyticsConfig_0</tan:ConfigurationToken>
      <tan:Rule Name="Entrance" Type="tt:MotionRegionDetector">
        <tt:Parameters>
          <tt:ElementItem Name="MotionRegion">
            <axt:MotionRegionConfig Armed="true" Sensitivity="0.8">
              <tt:Polygon>
                <tt:Point x="100" y="100"/>
                <tt:Point x="700" y="100"/>
                <tt:Point x="700" y="600"/>
                <tt:Point x="100" y="600"/>
              </tt:Polygon>
            </axt:MotionRegionConfig>
          </tt:ElementItem>
        </tt:Parameters>
      </tan:Rule>
    </tan:CreateRules>
  </s:Body>
</s:Envelope>
```

### Configuration, simulation and events

Motion Region rules persist in `Analytics[].rules` in `media_profiles.config`. Each region rule
contains `Name`, `Type`, and `MotionRegion`; the latter contains `Armed`,
`Sensitivity` and a `Polygon` array of objects with `x` and `y` coordinates.
The configuration directory must be writable for mutations. Restart restores
rules; transient alarm state starts false. Persisted rules are validated on startup.

The event thread reads a locked snapshot owned by `AnalyticsRules`, never
the mutable media configuration tree. SOAP saves update the media manager's tree
and then commit the detector snapshot. The separate `ProfilesConfig()` tree is
not the source for rule serialization. Source tokens and Bounds are taken at
startup from compatible VideoSource configurations; changing those associations
requires a restart. The supplied setup has one video source.

`MotionRegion.EventGenerationTimeout` in `event.config` sets the synthetic tick
interval in seconds (default 2; use a positive integer). On tick `n`, starting at
1, a sample appears at `x = (n % 8) * 160 + 80`, `y = 360`. Odd ticks have strength
0.75; even ticks have strength 0. A region reports motion when armed, containing
the point, and the nonzero strength is at least `1 - Sensitivity`. Consequently,
small regions outside this trajectory and sensitivities below 0.25 may never
trigger. This deterministic input is an intentional simulation deviation from
hardware detection, allowing repeatable client tests independent of video content.
PTZ motion does not change the screen region; preset-bound regions are unsupported.

PullPoint delivers property events on
`tns1:RuleEngine/MotionRegionDetector/Motion`, with Source items `VideoSource` and
`RuleName`, and a boolean Data item `State`. Synchronization reports the current
state as Initialized. Creation queues Initialized; changes queue Changed and
removal queues Deleted. These notifications are delivered on the next tick;
sample-driven Changed notifications occur only when the state changes. A rule
modification resets its evaluated state to false before the next sample.
The topic is also described by `GetEventProperties`.

## Field Detector (Annex A.3)

`tt:FieldDetector` uses the same six Rules operations, access levels, atomic
persistence and shared four-rule limit. Its `Field` ElementItem contains a
`tt:Polygon` of 3–8 distinct vertices in normalized `[-1, 1]` coordinates on
both axes, with positive y upward. A simple concave polygon and either winding
are supported. Self-intersections, duplicate vertices (including a repeated
closing vertex), zero-area shapes, non-finite/out-of-range coordinates, extra
parameters and malformed payloads produce `InvalidRule`. Empty Parameters on
creation selects the full normalized frame. Omitted Field on modification retains
the polygon. `GetRuleOptions` returns `tt:PolygonOptions` with `RectangleOnly=false`
and `VertexLimits` 3–8, using ONVIF schema 25.12. The A.3 contract and the linked
WSDL/XSD definitions were inspected on 2026-11-19.

An independently written rule body for CreateRules, after ConfigurationToken:

```xml
<tan:Rule Name="EntranceZone" Type="tt:FieldDetector">
  <tt:Parameters>
    <tt:ElementItem Name="Field">
      <tt:Polygon>
        <tt:Point x="-0.5" y="-0.5"/>
        <tt:Point x="0.5" y="-0.5"/>
        <tt:Point x="0.5" y="0.5"/>
        <tt:Point x="-0.5" y="0.5"/>
      </tt:Polygon>
    </tt:ElementItem>
  </tt:Parameters>
</tan:Rule>
```

The persisted rule in `Analytics[].rules` has `Name`, `Type: "tt:FieldDetector"`
and `Field: [{"x": -0.5, "y": -0.5}, ...]`. Media and Media2 return this same
polygon. The JSON schema describes the stored shape; runtime validation also
checks geometry. Restart restores rules and starts with no tracked objects.

Notifications on `tns1:RuleEngine/FieldDetector/ObjectsInside` are properties,
with Source items `VideoSource`, `AnalyticsConfiguration` and `Rule`, and one
boolean Data item `IsInside`. It is true while at least one tracked object is
inside the polygon. Moving another object in or out leaves the property unchanged
unless the zone becomes empty or occupied. The optional ObjectId list is neither
advertised nor emitted. Synchronization returns the current property as Initialized.
Creation queues Initialized(false), modification clears that rule's tracked objects
and queues Changed(false), and deletion queues Deleted with the last value.
Failed batches and failed saves preserve both rule configuration and occupancy.
A one-second event timer drains lifecycle/disappearance notifications even when
no object samples arrive. The shared object sampler can drain them sooner.

Field Detector uses the existing Line Detector synthetic object trajectory and
`LineDetector.EventGenerationTimeout`; no separate trajectory or timeout is
introduced. Objects are represented as points, and boundary points count as inside.
The object remains present until its next sample or an explicit ForgetObject call;
there is no automatic expiry. This deterministic point model is an intentional
simulation deviation, independent of video content or object bounding boxes.
PTZ remapping, preset association and class filtering are unsupported and are not
advertised. The supplied central square alternates between occupied and empty;
the default full-frame polygon stays occupied after the first sample.

## Line Detector (Annex A.2)

Line Detector uses the same six Rules operations, access levels, configuration
token and persistence transaction as Motion Region. `GetSupportedRules` describes
all four rule types. `GetRuleOptions` without a type returns their options; with
`tt:LineDetector` it returns `Direction` (`tt:StringList`: `Left Right Any`) and
`Segments` (`tt:IntRange`: 2–8 points). The optional RuleType precedes
ConfigurationToken in the request, as required by Analytics WSDL 20.12.

The parameters are an optional `Direction` SimpleItem and one `Segments`
ElementItem containing `tt:Polyline` with `tt:Point` children. Coordinates lie
in `[-1, 1]` on both axes, with positive y upward (section 5.2.2); these are
different from Motion Region's pixel coordinates. The polyline must be simple,
open and non-intersecting. Duplicate vertices, overlapping or zero-length
segments, non-finite coordinates, unsupported parameters and invalid directions
produce `InvalidRule`. PTZ association and class filters are not advertised or
accepted. These rules apply to a static view; PTZ does not remap their geometry.

For a segment directed from A to B, `Left` detects travel toward its left side,
and `Right` toward its right side. Reversing the vertices reverses these meanings.
An empty Parameters element on creation selects `Any` and the vertical line
`(0, -1)` to `(0, 1)`. Omitted parameters on modification retain their values.
Changing a rule's type is rejected with Receiver/Action/TypeChangeProhibited.
Successful modification or deletion clears its object history; failed batches
and persistence failures preserve it. Restart reloads geometry and starts with
no tracked objects. Line rules share names and the four-rule resource limit with
Motion Region rules. Names are unique within an analytics configuration; the
resource limit applies across configurations.

For example, the following independently written rule body can be used in
CreateRules after ConfigurationToken, with the namespace bindings shown above:

```xml
<tan:Rule Name="EntranceLine" Type="tt:LineDetector">
  <tt:Parameters>
    <tt:SimpleItem Name="Direction" Value="Any"/>
    <tt:ElementItem Name="Segments">
      <tt:Polyline>
        <tt:Point x="0" y="-1"/>
        <tt:Point x="0" y="1"/>
      </tt:Polyline>
    </tt:ElementItem>
  </tt:Parameters>
</tan:Rule>
```

In `media_profiles.config`, a line rule has `Name`, `Type: "tt:LineDetector"`,
`Direction: "Any"` and `Segments: [{"x": 0, "y": -1}, {"x": 0, "y": 1}]`.
Rules are stored in the existing `Analytics[].rules` array. Media/Media2 serialize
all rule types from the media manager's current tree.

`LineDetector.EventGenerationTimeout` in `event.config` is the positive tick
interval in seconds (default 2). A synthetic object with ID `1` moves horizontally
at y=0 through x positions `-0.75, -0.25, 0.25, 0.75, 0.75, 0.25, -0.25, -0.75`,
then repeats for each configured video source. This is an intentional simulation
deviation: no objects are extracted from video, and no RTSP object metadata is
produced. Lines outside this trajectory do not fire. Detection interpolates
between successive off-line positions and emits at most one event per rule and
object per sample. Touching and returning to the same side, travelling along a
line and passing its outer endpoint do not count. Landing on the line defers
evaluation until the object leaves it; interior vertices are checked to distinguish
crossings from tangencies. The geometric comparison tolerance is 1e-9 in
normalized coordinates. `ForgetObject` releases history when a supplied track ends;
the built-in generator maintains one persistent object per video source.

Notifications use `tns1:RuleEngine/LineDetector/Crossed`, with Source items
`VideoSource`, `AnalyticsConfiguration` and `Rule`, and Data item `ObjectId`
(`tt:StringList`, containing the synthetic ID). `IsProperty=false` is advertised
in both GetSupportedRules and GetEventProperties. Messages omit PropertyOperation.
Create, modify, delete and synchronization do not emit line crossing events.

## Line crossing counting rule (Annex A.5)

The rule's QName is `tt:LineCounting`. Its property topic is
`tns1:RuleEngine/CountAggregation/Counter`. Source contains `VideoSource`,
`AnalyticsConfiguration` and `Rule`; Data contains `Count` of type `xs:int`.
The same Rules CRUD, configuration token, access levels, atomic persistence and
shared four-rule limit apply. Media and Media2 return the persisted rule configuration.

The supported parameters follow A.5 of Analytics 26.06 and the ItemList / Polyline
definitions in ONVIF schema 25.12, using Analytics WSDL 20.12 as linked above:

| Parameter | Wire representation | Supported values and defaults |
| --- | --- | --- |
| `Segments` | One or more ElementItems containing `tt:Polyline` | 1–4 polylines, each with 2–8 points in `[-1,1]`; defaults to the A.2 vertical line |
| `Direction` | Optional SimpleItem, `tt:Direction` | `Any` (default), `Left`, `Right`; orientation follows A.2 |
| `PassAllPolylines` | Optional SimpleItem, `xs:boolean` | `false` (default): each crossed polyline counts separately; `true`: one object must cross every polyline |
| `ReportTimeInterval` | Optional SimpleItem, `xs:duration` | `PT0S` (default, immediate) through `P1D`; fixed durations only, no nonzero year/month components |
| `ResetTime` | Repeated optional SimpleItems, `xs:time` | Daily reset schedule; omitted on creation means no scheduled reset |

An empty Parameters element on creation selects these defaults. Omitted fields on
modification retain their values. A supplied list of Segments or ResetTime items
replaces that entire list. To remove all reset times, delete and recreate the rule;
an empty time is invalid. SimpleItems must precede ElementItems. `GetRuleOptions`
advertises direction choices, point and polyline limits, and the duration range.
Boolean and unconstrained time parameters need no value-range option.

For `PassAllPolylines=false`, crossing two polylines in one sample adds two.
For `true`, each object's progress is independent, the order is unrestricted and
one count completes a passage through all polylines. Completion clears that
object's progress so it may complete another passage. `ForgetObject` clears
unfinished progress when a track ends. Crossing tolerance, touching, vertices,
outer endpoints and interpolation follow the A.2 implementation described above;
at most one crossing per polyline and object is recognized per sample. These
sampling limits are intentional simulation deviations. Different polylines may
intersect each other, but each individual polyline must be simple.

The count starts at zero on creation and restart. Configuration changes preserve
the current count but clear object histories and restart report/reset scheduling.
Failed mutations preserve both configuration and runtime state. Runtime counts,
unfinished passages and timing state are not persisted; configuration is.
The count saturates at 2147483647 until reset, keeping events inside `xs:int`.

Unzoned ResetTime values use the process's local time (`TZ`, UTC in the supplied
Docker image), following A.5's local-time convention. As an explicit emulator
extension, timezone-qualified `Z` or `±hh:mm` values are also accepted and their
offset is honored. Multiple daily reset times, fractional seconds and `24:00:00`
are supported. Reset clears the count and all partial passages, and reports a
changed zero immediately when needed, regardless of the report interval. Skipped
ticks or days still cause a reset when a scheduled boundary was crossed. System
clock adjustments use wall-clock time; moving backward does not itself reset a
count. DST normalization of local reset times follows the platform C library.
Duration and time fractions have microsecond resolution, rounded upward.

With a positive ReportTimeInterval, counts accumulate and Changed reports are
limited to that interval, starting at rule creation or modification. A dedicated
one-second timer checks resets and pending reports even when no objects cross.
Notifications therefore have timer resolution rather than hard real-time delivery.
Synchronization always returns the latest evaluated count as Initialized.
Creation queues Initialized, modification queues Changed, and deletion queues
Deleted with its final count. A pending lifecycle notification precedes subsequent
sample-driven reports. Unchanged counts do not generate periodic duplicates.

Counting uses the same synthetic object and `LineDetector.EventGenerationTimeout`
as A.2; it does not add a separate video analysis pipeline or metadata track.
For example, this independently written rule counts passage across two lines:

```xml
<tan:Rule Name="EntranceCounter" Type="tt:LineCounting">
  <tt:Parameters>
    <tt:SimpleItem Name="ReportTimeInterval" Value="PT5S"/>
    <tt:SimpleItem Name="ResetTime" Value="00:00:00"/>
    <tt:SimpleItem Name="Direction" Value="Any"/>
    <tt:SimpleItem Name="PassAllPolylines" Value="true"/>
    <tt:ElementItem Name="Segments">
      <tt:Polyline><tt:Point x="-0.25" y="-1"/><tt:Point x="-0.25" y="1"/></tt:Polyline>
    </tt:ElementItem>
    <tt:ElementItem Name="Segments">
      <tt:Polyline><tt:Point x="0.25" y="-1"/><tt:Point x="0.25" y="1"/></tt:Polyline>
    </tt:ElementItem>
  </tt:Parameters>
</tan:Rule>
```

The corresponding `Analytics[].rules` JSON entry has `Name`,
`Type: "tt:LineCounting"`, `Direction`, `PassAllPolylines`, `ReportTimeInterval`,
`ResetTime` as an array of strings and `Segments` as an array of point arrays.
For example, `Segments: [[{"x": 0, "y": -1}, {"x": 0, "y": 1}]]` defines one line.
Use an empty `ResetTime` array for a persisted configuration without resets.

The former branch's experimental MotionRegionDetector AnalyticsModule and sample
vehicle metadata are no longer exposed: MotionRegionDetector is a rule. Analytics
module, cell scene description and supported metadata capabilities are false.
Legacy module utilities remain covered by isolated fixtures. When migrating an
old configuration, remove the experimental `analyticsModules` entries and use
`rules` instead. This implementation does not add an RTSP metadata track or the
other features needed for a complete Profile M product. Existing Event service
restart and shutdown limitations described in the architecture still apply.

## Validation

Build and run CTest as described in [BUILD.md](../BUILD.md). The motion suites
cover defaults, persistence/reload, atomic failures, QName namespaces, polygon
geometry, sensitivity, arming, synchronization, limits and save failures.

Against a running instance with a disposable configuration copy and disabled
authentication, run:

```bash
python3 unit_tests/analytics_smoke.py --host 127.0.0.1 --capture /tmp/analytics-responses
python3 unit_tests/network_smoke.py --host 127.0.0.1 --advertised-ip 127.0.0.1 --multicast-interface LOCAL_IPV4
```

The Analytics smoke test creates two Motion Region rules, one Line Detector and
one LineCounting rule, then replaces one Motion Region rule with a Field Detector.
It checks SOAP faults, Media/Media2, PullPoint motion/crossing and zone occupancy
events, aggregated counts and a scheduled reset, and deletes its rules.
It needs four free rule slots
and the default synthetic tick interval. Optional response capture supports
external schema validation without adding a runtime XML library dependency.

The Line Detector suite additionally checks defaults and reload, option filtering,
direction and vertex order, horizontal/diagonal segments, tangencies, vertex
crossings, object/source isolation, history reset, mixed-type atomic batches,
resource limits and persistence failures. Builds and CTest can run in the Docker
`dev` environment documented in BUILD.md; use a separate build volume and a
disposable configuration copy for network checks.

The LineCounting suite uses an injected clock to test per-line counts, direction,
PassAllPolylines object isolation, aggregation without new samples, report-boundary
crossings, repeated/local/offset reset times, skipped days, atomic failures,
reload, lifecycle notifications and integer saturation. Its Docker Debug build
and all 119 Boost test cases passed through CTest. The combined Analytics smoke
test and the complete network smoke test passed against a configuration copy in
an isolated Docker network, including unicast/multicast discovery and RTP/TCP/UDP.

Field Detector validation on 2026-11-19 used the documented `osrv-dev:latest`
container and `osrv-analytics-build` volume: CMake Debug with unit tests enabled,
`cmake --build build --parallel 2`, and `ctest --test-dir build --output-on-failure`
passed. The extended Analytics smoke test and network smoke test passed against
that build with a disposable configuration in the container network namespace,
including multicast discovery and RTP over TCP/UDP. JSON Schema regression tests
passed (10 cases). With `xmlschema` 4.3.2 in XSD 1.1 mode and the linked official
imports, 64 captured Analytics responses, SOAP faults, Media/Media2 analytics
configuration payloads and notification messages passed schema validation.
These checks do not validate a Windows/VMS deployment or constitute certification.

For LineCounting, 71 captured response, fault and payload documents passed XSD 1.1
validation with the same WSDL/XSD imports and `xmlschema` 4.3.2, including
DurationRange options, Media/Media2 analytics configurations and PullPoint messages.

For the Line Detector integration, the Docker Debug build and all 110 Boost test
cases passed through CTest. The combined Analytics smoke test passed against a
disposable configuration. Using `xmlschema` 4.3.2 in XSD 1.1 mode with the above
WSDL/XSD imports, 41 captured response, fault and payload documents passed
validation, including Analytics operations, Media/Media2 analytics configuration
payloads and PullPoint messages. Both ONVIF schema imports (`onvif.xsd` and
`metadatastream.xsd`) must be loaded when compiling the Analytics WSDL schema.

During the Motion Region implementation, nine Analytics response bodies and the MotionRegion
payloads returned through Media/Media2 were validated against the above WSDL/XSD
with imports; three complete SOAP fault envelopes were also validated, using `xmlschema` 4.3.2 in XSD 1.1 mode. XSD 1.0 validation with
libxml2 could not compile the upstream wildcard content models (including
MotionRegionConfigOptions); the schemas were not patched. This is a validation
tool limitation, not a claim of complete protocol or product conformance.
