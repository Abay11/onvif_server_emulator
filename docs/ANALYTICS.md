# Analytics Rules: Motion Region and Line Detector

The Analytics endpoint `/onvif/analytics_service` implements the Rules interface
for `tt:MotionRegionDetector` and `tt:LineDetector`. This is an emulator with
deterministic synthetic input. It does not inspect the RTSP stream or claim Profile M certification.

## Contract and references

The implementation targets sections 5.2.2, 6.1, 6.2, Annex A.1/A.2 and Annex C.1 of the
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

The Motion Region references were inspected on 2026-11-17; the Line Detector
contract was checked against the same versions on 2026-11-18. SHA-256 values for reproducibility:

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

The device supports up to four rules in total across both types and all analytics
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

## Line Detector (Annex A.2)

Line Detector uses the same six Rules operations, access levels, configuration
token and persistence transaction as Motion Region. `GetSupportedRules` describes
both rule types. `GetRuleOptions` without a type returns options for both; with
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
both types from the media manager's current tree.

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

The Analytics smoke test creates two Motion Region rules and one Line Detector,
checks SOAP faults, Media/Media2, PullPoint state transitions and crossing events,
and deletes its rules. It needs three free rule slots
and the default synthetic tick interval. Optional response capture supports
external schema validation without adding a runtime XML library dependency.

The Line Detector suite additionally checks defaults and reload, option filtering,
direction and vertex order, horizontal/diagonal segments, tangencies, vertex
crossings, object/source isolation, history reset, mixed-type atomic batches,
resource limits and persistence failures. Builds and CTest can run in the Docker
`dev` environment documented in BUILD.md; use a separate build volume and a
disposable configuration copy for network checks.

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
