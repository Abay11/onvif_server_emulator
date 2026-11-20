# Analytics Rules

The Analytics endpoint `/onvif/analytics_service` implements the Rules interface
for `tt:MotionRegionDetector`, `tt:LineDetector`, `tt:LineCounting`, `tt:FieldDetector`, `tt:LicensePlateRecognition` and `tt:FaceRecognition`.
This is an emulator with deterministic synthetic input. It does not inspect the RTSP stream or claim Profile M certification.

## Contract and references

The implementation targets sections 5.2.2, 6.1, 6.2, Annex A.1/A.2/A.3/A.5, Annex C.1 and Annex G.1/G.2/G.3 of the
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

The device supports up to four rules in total across all six types and all analytics
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

## License Plate Recognition (Annex G.3)

`tt:LicensePlateRecognition` uses the same Rules CRUD, atomic persistence,
configuration tokens, access levels and shared four-rule limit. Its G.1/G.3
contract uses Analytics 26.06, Analytics WSDL 20.12, ONVIF schema 25.12 and
metadata schema 25.12 linked above. Rule and event
payloads are independently implemented; no specification material is bundled.

| Parameter | Supported values and creation default |
| --- | --- |
| `IncludeImage` | `Embedded` (creation default), `LocalStorage`, or empty (no images); `RemoteStorage` is unsupported |
| `PlateLocation` | `Front`, `Rear`, or empty (default, either location) |
| `Country` | Country string, or empty (default, no country filter) |
| `Sensitivity` | Finite float in `[0,1]`, default `0.5` |
| `Threshold` | Finite confidence threshold in `[0,1]`, default `0.5` |
| `PlateMinWidth`, `PlateMaxWidth` | Percentage of scene image width in `[0,100]`, defaults `0`, `100`; minimum must not exceed maximum |
| `EventInterval` | Integer seconds in `[0,2147483647]`, default `0`; minimum time between notifications for the same country/plate within a rule |
| `Region` | Optional ElementItem containing `tt:Polygon`, 3–8 vertices in normalized `[-1,1]` coordinates; absent means full frame |
| `SnapLine` | Optional ElementItem containing `tt:Polyline`, 2–8 vertices in normalized `[-1,1]` coordinates; absent means no snap constraint |

All other parameters are SimpleItems. Omitted parameters on modification retain
their values. Duplicate/unknown parameters, malformed geometry, non-finite or
out-of-range numbers, and unsupported image modes return `InvalidRule`.
Geometry uses the same simple-polygon/polyline restrictions as Field/Line Detector.
`GetSupportedRules` describes these parameters and the emitted event fields;
`GetRuleOptions` exposes numeric and geometry limits. Country strings are not
restricted to an enumeration. IncludeImage options and the Analytics ImageSendingType
capability advertise `Embedded LocalStorage`.

For example, send this independently written rule body after ConfigurationToken
in CreateRules, using the namespace bindings of the Motion Region example:

```xml
<tan:Rule Name="EntryPlates" Type="tt:LicensePlateRecognition">
  <tt:Parameters>
    <tt:SimpleItem Name="Threshold" Value="0.8"/>
    <tt:SimpleItem Name="EventInterval" Value="5"/>
  </tt:Parameters>
</tan:Rule>
```

Rules persist as flat objects in `Analytics[].rules`, with `Name`, `Type` and the
SimpleItem names as JSON keys; optional `Region`/`SnapLine` are arrays of `{x,y}`
points. Media and Media2 serialize the same persisted values. Restart retains
configuration and clears transient sampling/repeat history. Successful modification
clears only that rule's history; failed batches/saves retain configuration and history.

A dedicated one-second event generator cycles through five synthetic vehicles:

| Plate | Country | Type | Brand / Model | Moving speed (m/s) | Plate width (%) | Likelihood |
| --- | --- | --- | --- | --- | --- | --- |
| TEST123 | US | Car | Test Brand / Synthetic Vehicle (blue car) | 12.5 | 10 | 0.95 |
| TEST456 | GB | Car | Test Motors / Red SUV | 8 | 12 | 0.92 |
| TEST789 | DE | Truck | Test Cargo / White Van | 6 | 14 | 0.97 |
| TEST321 | FR | Bus | Test Transit / Yellow Minibus | 10 | 16 | 0.90 |
| TEST654 | IT | Motorcycle | Test Riders / Green Motorcycle | 15 | 6 | 0.94 |

The sequence repeats in table order, with one vehicle sampled each second for
every video source. Each vehicle advances its own x position every five seconds
through `-0.75, -0.25, 0.25, 0.75, 0.75, 0.25, -0.25, -0.75`, at y=0. The complete
trajectory cycle takes 40 seconds. This preserves independent plate histories for
SnapLine evaluation. With default parameters all five plates appear within five
seconds; Country, Threshold, width, Region/SnapLine and EventInterval settings can
reduce the emitted events. EventInterval remains per country/plate within a rule,
so another vehicle is not suppressed by the previous vehicle's notification.
The motorcycle is sampled from the rear (`PlateLocation=Rear`); the other four
vehicles use `Front`. A rule restricted to Front therefore excludes TEST654,
and a Rear-only rule selects it.
The sample must satisfy the region, location, country, width and threshold
filters. Sensitivity is simulated by requiring likelihood >= `1 - Sensitivity`.
When SnapLine is present, recognition occurs on crossing in either direction,
using Line Detector's boundary/tangency handling. The region is checked at the
current sample position. These deterministic filters, percentage ranges, seconds
for EventInterval and point geometry are explicit emulator choices where G.3 does
not fully prescribe an algorithm or range. There is no OCR, image extraction,
PTZ remapping, real vehicle classification or RTSP metadata. The extra vehicle
fields and image are synthetic test data, independent of the video stream.

PullPoint notifications use `tns1:RuleEngine/Recognition/LicensePlate`, with Source
SimpleItems `VideoSource` and `Rule`, Data SimpleItem `Likelihood` and Data
ElementItem `LicensePlateInfo`. The latter contains `tt:LicensePlateInfo` with
required `tt:PlateNumber` and optional `tt:CountryCode` in schema order, both of
metadata type `tt:StringLikelihood`. The country is omitted when absent in input.
Additional SimpleItems are `Speed` (metres per second) and `Direction` (ONVIF
`tt:Direction`). The generator reports `Right` or `Left` according to horizontal
motion and the moving speed in the table above; repeated endpoint positions
report `Any` and speed `0`.
Additional ElementItems are `VehicleInfo` (`tt:VehicleInfo`, with the selected vehicle
Type, Brand and Model in schema order) and `BoundingBox`
(`tt:Rectangle`, centered on the plate sample, width from the table and height 5 percent
of the frame, clipped to normalized bounds). No object ID or label is emitted.

With `IncludeImage=Embedded`, `Image` and `VehicleImage` are base64 ElementItems;
`ImageURI` and `VehicleImageURI` are also supplied as SimpleItems. With
`LocalStorage`, only the two URI fields are included. Empty IncludeImage omits
all four image fields. Existing persisted rules with empty IncludeImage remain
image-free; use ModifyRules to set it to `Embedded` to receive all four fields:

```xml
<tan:Rule Name="EntryPlates" Type="tt:LicensePlateRecognition">
  <tt:Parameters><tt:SimpleItem Name="IncludeImage" Value="Embedded"/></tt:Parameters>
</tan:Rule>
```

The URI names use uppercase `URI`, following the G.3 rule description (G.1 text
uses ImageUri). Both URLs use `ServerConfigs::HttpAddress()`, respecting advertised
addresses and port overrides, and serve immutable PNG data over HTTP GET at
`/analytics/lpr/plate.png` and `/analytics/lpr/vehicle.png` for TEST123. The other
vehicles use `/analytics/lpr/plate-TEST456.png` and
`/analytics/lpr/vehicle-TEST456.png`, with TEST789/TEST321/TEST654 substituted as appropriate.
Each URI always serves the same vehicle's bytes, even after later events.
`Image`/`ImageURI`
use `server_configs/rs/lpr-plate-test.png`, a separate close-up of the synthetic
plate `TEST123` marked TEST IMAGE. `VehicleImage`/`VehicleImageURI` use
`server_configs/rs/lpr-test.png`, the complete unbranded car test card marked
TEST IMAGE / NOT A REAL CAPTURE. Plate and vehicle bytes are distinct; each
embedded image matches the bytes returned by its own URL. Both are static test
assets, not crops extracted from live video. Additional pairs use the matching
`-TEST456`, `-TEST789`, `-TEST321` and `-TEST654` filename suffixes.
The routes are public static test resources, like the existing snapshot route.
All ten images are loaded and base64-encoded once during initialization. A missing or
unreadable asset prevents initialization with an explicit error. When using an
existing configuration mount, copy all `rs/lpr-test*.png` and
`rs/lpr-plate-test*.png` files into that mount.
This is not remote image upload; RemoteStorage remains unsupported.

This is a non-property event: notifications omit PropertyOperation, and rule CRUD
and synchronization generate no recognition messages. GetEventProperties advertises
all structured fields as ElementItemDescriptions and the remaining fields as
SimpleItemDescriptions.
Repeat history is keyed by configuration, rule, country and plate; entries last
until rule modification/deletion or restart. There is no automatic track expiry.

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
all five rule types. `GetRuleOptions` without a type returns their options; with
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
deviation: no objects are extracted from video. The same samples can produce
[synthetic RTSP scene frames](RTSP_METADATA.md#synthetic-scene-description)
when analytics metadata is enabled. Lines outside this trajectory do not fire.
Detection interpolates between successive off-line positions and emits at most one event per rule and
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

## Face Recognition (Annex G.2)

`tt:FaceRecognition` uses topic `tns1:RuleEngine/Recognition/Face`. Its contract
uses Analytics 26.06 Annex G.1/G.2 and the WSDL,
ONVIF/common and metadata schema versions linked above. `ObjectId` and
`ParentObjectId` are ElementItems of metadata type `tt:ObjectId`, with an integer
`ObjectId` attribute; `BoundingBox` uses the four required `tt:Rectangle`
attributes in normalized screen coordinates. Event descriptions use the G.2
spelling `ImageUri`, `RefImageUri` and `EnrollmentID` (case-sensitive).

Create a rule through `CreateRules` after `ConfigurationToken`:

```xml
<tan:Rule Name="TestFaces" Type="tt:FaceRecognition">
  <tt:Parameters>
    <tt:SimpleItem Name="IncludeImage" Value="Embedded"/>
    <tt:SimpleItem Name="Threshold" Value="0.5"/>
  </tt:Parameters>
</tan:Rule>
```

`Threshold` is the optional generic confidence threshold from G.1, range 0–1,
default 0.5. `IncludeImage` defaults to `Embedded`: events include both base64
`Image` and a downloadable `ImageUri` to maximize test payload coverage.
`LocalStorage` sends only `ImageUri`; an empty value suppresses both recognized
image fields. `RefImageUri` is independent and remains available in every mode.
`RemoteStorage` is not supported or advertised because there is no remote upload
backend. Omitted parameters on modification retain their previous values.
Rules share the existing four-rule limit and persist in `media_profiles.config`.
No face rule is enabled by default; create one with SOAP to start notifications.

The dedicated one-second sampler cycles `TEST-PERSON-001` through
`TEST-PERSON-003`, with labels `Whitelisted`, `Blacklisted`, `Temporary` and
likelihood 0.98. Every event includes `VideoSource`, `Rule`, `Likelihood`,
`EnrollmentID`, `Label`, `RefImageUri`, `BoundingBox`, `ObjectId`, and
`ParentObjectId`. Recognition events are not properties: they have no
`PropertyOperation` or synchronization/lifecycle notifications.

Simulation scope: no actual detection, biometric comparison or enrollment API
is implemented. Face notifications can be carried by RTSP event metadata, but
the scene module does not produce face scene frames. Each synthetic face is
treated as an exact match to its own reference test card; `ImageUri` and `RefImageUri` deliberately point
to the same PNG. The rectangle and face/person object IDs are fixed synthetic
scene data, independent of the RTSP test pattern. The reference cards are loaded
once from `rs/face-test-001.png` through `003.png` and served at
`/analytics/face/face-001.png` through `003.png`, using `HttpAddress()`.
The shared rule engine rejects unavailable face images with `ConfigurationConflict`;
the server reports an image-loading error at startup if a resource file is missing.
See [asset provenance and generation prompts](../server_configs/rs/README.md#synthetic-face-test-resources).

Validation: the `face_recognition` Boost suite covers descriptions, serialization,
all three identities, image modes, confidence filtering, restart persistence,
transaction rollback, deletion, source filtering and invalid requests/samples.
`python3 unit_tests/face_recognition_smoke.py --host 127.0.0.1` checks the SOAP and
PullPoint wire format, all image downloads, advertised descriptions and image
modes against a running server with disposable configuration and auth disabled.

## Scene module and metadata delivery

MotionRegionDetector is a rule, not an analytics module. Cell scene descriptions
are unsupported. The fixed read-only `SyntheticObjectTracker` module describes
Line Detector simulator samples; see
[RTSP metadata](RTSP_METADATA.md#synthetic-scene-description) for configuration,
module selection, scene/event correlation and simulation limits.
`GetAnalyticsModules`, `GetSupportedAnalyticsModules` and `GetSupportedMetadata`
expose this module and its sample frame. `SupportedMetadata=true` advertises the
last operation. Module creation/modification and module options remain unsupported
and their capabilities remain false. The scene module uses the pinned ONVIF 24.12
contract set documented in the metadata guide; the rule references above apply
to the rule interface.

Rule notifications can be delivered independently through PullPoint and RTSP event
metadata. Scene frames are selected separately and do not include LPR, face or
motion-region scene analysis. RTSP metadata support does not establish all features
needed for a complete Profile M product. Event-service restart and shutdown
limitations are described in the architecture.

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
resource limits and persistence failures. Use the Docker development workflow in
BUILD.md and the `dev` image described in
[RTSP metadata](RTSP_METADATA.md#enable-the-transport) for positive metadata
transport coverage. Use a separate build volume and a disposable configuration
copy for network checks.

The LineCounting suite uses an injected clock to test per-line counts, direction,
object isolation, aggregation, report boundaries, reset schedules, reload and
integer saturation. Field Detector tests cover polygon geometry, occupancy,
object/source isolation and lifecycle transitions. License Plate Recognition tests
cover plate trajectories, repeat intervals, image modes and per-vehicle fields.

For schema validation, load both `onvif.xsd` and `metadatastream.xsd` when compiling
the Analytics WSDL: they share a target namespace and supply different types.
The configuration and scene smoke checks support an offline pinned schema cache;
see [RTSP metadata validation](RTSP_METADATA.md#contract-and-validation).
Some upstream wildcard models require XSD 1.1 element/wildcard priority; the
validation tools do not patch upstream schemas. Validate semantics separately
from XML syntax. Container checks do not establish Windows/VMS interoperability
or official ONVIF conformance.
