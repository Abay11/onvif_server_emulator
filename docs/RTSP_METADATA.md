# Live RTSP metadata

The GStreamer RTSP server can deliver the shared event source as an ONVIF XML
metadata RTP track alongside live video/audio. PullPoint subscribers and RTSP
clients receive independent copies of the same generated notifications. The track
can also carry synthetic analytics scene frames, separately enabled in the
metadata configuration.

## Enable the transport

The transport and SOAP metadata configuration APIs are enabled by default.
The optional `common.config` setting is:

```json
"rtsp_metadata": { "enabled": true }
```

The default is also true when the setting is omitted. Restart the server after
changing it. Set it to false to disable the feature. Enabled startup requires the
`rtponvifmetadatapay` GStreamer plugin; a missing plugin produces a startup error.
Normal Docker runtime builds include the pinned plugin, source and
[dependency notices](../experiments/rtsp_metadata/README.md#plugin-provenance-and-notices). For source builds/tests use the `dev` target:

```bash
docker build -f docker/Dockerfile --target dev -t osrv-dev:latest .
```

Follow [BUILD.md](../BUILD.md) for repository/build mounts, CMake and network
settings. The `dev` image includes the payloader and supports the default metadata
setting without configuration changes.

## Configure a profile through SOAP

With the transport enabled, Media and Media2 expose metadata configuration APIs.
The shipped `media_profiles.config` has two reusable configurations,
`MetadataToken0` and `MetadataToken1`, initially enabling all events. Profiles are
initially unattached: enabling the transport alone does not add a metadata track.
Older configuration directories need the `Metadata` catalog from the example.
Detach metadata from all profiles before disabling the transport. Startup rejects
persisted metadata associations when the switch is false, and Media2 rejects
adding metadata while it is disabled, keeping profile responses and SDP consistent.

1. List configurations using Media `GetMetadataConfigurations`, or Media2
   `GetMetadataConfigurations` with optional ConfigurationToken/ProfileToken.
2. Read `GetMetadataConfigurationOptions`. Media also provides
   `GetMetadataConfiguration` and `GetCompatibleMetadataConfigurations`.
3. Use `SetMetadataConfiguration` to select events and filters. Media accepts
   ForcePersistence; the obsolete flag is treated as true even when false is sent.
   Media2 has no ForcePersistence field. All accepted changes persist.
4. Attach the token using Media `AddMetadataConfiguration` (ProfileToken and
   ConfigurationToken), or Media2 `AddConfiguration` (ProfileToken and a
   Configuration containing Type=Metadata and Token). Addition replaces the
   previous metadata reference; UseCount is computed from actual references.
5. Request `GetStreamUri` for that profile. DESCRIBE the returned URI and select
   its application track, alone or with video/audio. Both services return the
   same stable `/onvif/profile/<SHA-256-of-profile-token>` URI. The URI is
   installed at startup and stays valid across configuration changes and reboot.

Remove the association using Media `RemoveMetadataConfiguration` or Media2
`RemoveConfiguration` with Type=Metadata. Profile responses include the full
metadata configuration in the schema-defined location. Media2 advertises
Metadata in ConfigurationsSupported only when the transport is enabled.

The fixed `/Live&HighStream` and `/Live&LowStream` mounts remain available for
legacy video/audio clients; metadata clients must use the profile URI returned
by SOAP. Replay at `/Recording0` has no historical metadata track.

### Supported settings and filters

The Events element controls event delivery: include an empty Events element for
all events, omit it for no events, or include Events/Filter for selected events.
An attached configuration with both events and analytics disabled retains the
application track and sends empty metadata documents. This is different from removing its association,
which removes the metadata track from newly prepared streams. Media2 CreateProfile,
AddConfiguration and RemoveConfiguration process their configuration references
as a batch, validating metadata/source dependencies on the final profile.

The accepted filter dialects are OASIS Concrete and ONVIF ConcreteSet topics,
and ONVIF ItemFilter message content, using the URIs in Core 24.12. Concrete
selects one exact topic. ConcreteSet supports exact paths, `|` alternatives and
`//.` subtrees. QName matching uses namespace URIs, including bindings inherited
from the SOAP envelope; prefix aliases are interchangeable. ItemFilter supports
boolean presence tests on SimpleItem/ElementItem, Name/Value comparisons,
parentheses, `not`, `and` and `or` with the Core-defined precedence. For example:

```xml
<tt:Events xmlns:tt="http://www.onvif.org/ver10/schema"
           xmlns:wsnt="http://docs.oasis-open.org/wsn/b-2"
           xmlns:topic="http://www.onvif.org/ver10/topics">
  <tt:Filter>
    <wsnt:TopicExpression Dialect="http://www.onvif.org/ver10/tev/topicExpression/ConcreteSet">topic:RuleEngine/CellMotionDetector//.</wsnt:TopicExpression>
    <wsnt:MessageContent Dialect="http://www.onvif.org/ver10/tev/messageContentFilter/ItemFilter">boolean(//tt:SimpleItem[@Name='IsMotion' and @Value='true'])</wsnt:MessageContent>
  </tt:Filter>
</tt:Events>
```

One topic expression and one content expression can be combined; both must match.
Each expression is limited to 4096 UTF-8 bytes. Unsupported dialects, syntax,
subscription policies, metadata producers or configuration values return
Sender/InvalidArgVal/ConfigModify. Missing configuration/profile tokens return
NoConfig/NoProfile. Input UseCount is ignored; returned UseCount reflects actual
associations. Topic/content filters apply to property snapshots as well as live
notifications. They do not change independent PullPoint subscriptions; existing
PullPoint filter simulation limits remain described in [event delivery](EVENT_DELIVERY.md).

Only uncompressed metadata is supported. Configuration responses explicitly
include `CompressionType="None"`, including after SetMetadataConfiguration, so
clients can read back the selected mode. Analytics can be enabled independently
as described below. PTZ status/position,
GeoLocation and ShapePolygon cannot be enabled. Options report false PTZ flags,
CompressionType=None and MaxContentFilterSize=4096. The supported multicast
placeholder is IPv4 0.0.0.0, port 0, TTL 1, AutoStart=false; multicast streaming is
unadvertised and unsupported. Media SessionTimeout is fixed at PT60S; Media2
ignores the deprecated timeout field.

### Synthetic scene description

Set `tt:Analytics` to `true` in a metadata configuration and attach it to a profile
that references both a VideoSourceConfiguration and a VideoAnalyticsConfiguration.
The default metadata configurations retain `Analytics=false`. Three fixed modules (`ObjectTracker`, `LicensePlateRecognition`, `FaceRecognition`) produce `tt:VideoAnalytics/tt:Frame` with UTC time,
numeric object identifiers, identity transformation, normalized bounding boxes,
centers and Human, Vehicle, LicensePlate or HumanFace classification, scoped to the originating module. LPR objects include
`tt:Appearance/tt:LicensePlateInfo` with PlateNumber, its Likelihood and optional
CountryCode. `Frame.Source` identifies the module name.
Media and Media2 include these modules in the analytics configuration; Analytics
supports `GetAnalyticsModules`, `GetSupportedAnalyticsModules` and
`GetSupportedMetadata`. The latter returns representative frames whose geometry
is in the top-left image quadrant. The module QName prefix `osrv` is bound to
`urn:onvif-server-emulator:analytics`. These are built-in, read-only modules with no
configurable parameters; module CRUD and options are not advertised.

Omitting `tt:AnalyticsEngineConfiguration` selects all available modules. An empty
element selects none. To select ObjectTracker explicitly, include this
element after `tt:SessionTimeout` in the metadata configuration:

```xml
<tt:AnalyticsEngineConfiguration xmlns:tt="http://www.onvif.org/ver10/schema"
    xmlns:osrv="urn:onvif-server-emulator:analytics">
  <tt:AnalyticsModule Name="ObjectTracker" Type="osrv:ObjectTracker">
    <tt:Parameters/>
  </tt:AnalyticsModule>
</tt:AnalyticsEngineConfiguration>
```

Each module uses its own QName in `urn:onvif-server-emulator:analytics`:
`osrv:ObjectTracker`, `osrv:LicensePlateRecognition`, `osrv:FaceRecognition`.
`GetSupportedMetadata` returns one MetadataInfo/SampleFrame per module; Type
filters to a single module. Frame.Source is the corresponding module Name.
These are implementation-defined analytics modules, not ONVIF event topics or
standardized module types. ONVIF's LicensePlateInfo and HumanFace are object
descriptor types; LicensePlateRecognition and FaceRecognition in Annex G are
rule types. The standard event topics remain RuleEngine/Recognition/LicensePlate
and RuleEngine/Recognition/Face. Line, Counting and Field rules consume the shared
ObjectTracker samples and retain their own standard rule event topics.

Only module selection is applied here; module parameters in this selector are
ignored. Unknown module names/types and duplicate selectors return ConfigModify. Multiple distinct modules may be selected. The persisted JSON
`Metadata[].AnalyticsModules` contains module names or an empty
list (PropertyTree writes the latter as `""`); an absent key selects all modules. Legacy persisted `SyntheticObjectTracker` selectors are accepted as an alias for all modules; responses serialize the current module names.
`Events` and `Analytics` are independent: omitting Events yields a scene-only
stream; enabling both yields scene frames and notifications. Event topic/content
filters do not filter scene frames. A profile without an analytics association
receives no scene frames. Existing sessions retain their settings until reconnect.

The Line Detector simulator supplies the same object samples to the scene buffer
and rule engine. A line-crossing event and its corresponding frame have identical
ObjectId and UTC time. IDs remain stable for an object's lifetime and are not
reused after deletion within a running simulator. Each frame describes the current
objects; deletion records remove objects previously delivered to that media
instance. The buffer retains 128 frames and at most 64 active objects per video
source and module. A slow reader that falls behind receives the latest full snapshot and a
diagnostic rather than an unbounded backlog. PLAY and synchronization resend the
latest scene snapshot as well as event property states.

The LPR simulator supplies plate observations to LicensePlateRecognition before applying
recognition rule filters or EventInterval. Scene observations therefore remain
available without an LPR rule and when a rule suppresses an event. A recognition
event and its corresponding scene frame share the sample UTC, plate number,
country, confidence and plate bounding box. Plate identities are tracked separately
from Human objects, by video source, country and plate number. The plate bounding
box uses the sample's width/height percentages, clipped to normalized image bounds.
Each plate has a Vehicle parent referenced by Object.Parent. The vehicle carries
Appearance/VehicleInfo (Type, Brand, Model), Behaviour/Speed in metres per second,
and Appearance/Color/ColorCluster/Color with X=R, Y=G, Z=B in [0,255] and an explicit
ONVIF RGB colorspace URI. These are synthetic scenario values shared with rule
samples, not measurements extracted from the video. The vehicle ImageRef points
to the existing full vehicle card; the plate Image/ImageRef remain plate previews.
PlateType and IssuingEntity are omitted because the simulator has no observations
for them. Like other synthetic objects, plates remain in the bounded current
snapshot until evicted; this does not simulate a vehicle leaving the scene.

LPR objects also carry `Appearance/ImageRef`, an absolute HTTP URL for the
synthetic plate PNG preview, built from the advertised HTTP endpoint. The
currently sampled plate additionally carries the same preview as base64 in
`Appearance/Image`; retained objects keep their references without repeating
their embedded bytes on every unrelated observation. `GetSupportedMetadata`
includes both fields for TEST123. Image assets are immutable and shared between
retained frames. This scene output is independent of the rule's `IncludeImage`
setting, which controls recognition event fields only. Scene image
HTTP routes at `/analytics/lpr/scene/plate*.png` provide these bytes without
authentication. Previews are generated once at startup using the existing
GStreamer pngdec, videoconvert, videoscale and pngenc plugins, fitting the plate
into a 240x120 canvas with aspect ratio preserved and borders added. This avoids
embedding the roughly megabyte-sized originals in RTP scene frames; recognition
events retain the original full-resolution images. These images depict
synthetic plates, rather than crops extracted from the live video.

This is an intentional simulation limitation: the module describes synthetic
object samples, not analysis of the video test pattern or arbitrary file input.
UTC is the simulator sample time; exact correspondence to an encoded video frame
is not implemented. Visual correlation requires a separate renderer/analyzer.
FaceRecognition publishes separate HumanFace objects and compact PNG previews
from the same recognition samples, with scene/event ObjectId and UTC correlation.
No biometric attributes or recognized-person identity descriptor are invented.
The optional event ParentObjectId is omitted when there is no tracked parent.
Motion-region event generators do not produce object scene frames. PTZ metadata remains unsupported.

ONVIF image semantics: Appearance/Image and ImageRef describe the detected object
(plate for LicensePlate, face for HumanFace, vehicle for Vehicle). PNG/JPEG image
data are permitted. Frame/SceneImage and SceneImageRef are the optional full-scene
fields; this simulator does not emit them. `FullImage` is a consumer product field,
not an ONVIF XML element. No change from object crops to full-scene images is
required by Analytics §5.4.13. This does not remove the simulator's lack of actual
video-frame correlation.

### Persistence and active sessions

Both SOAP services share the manager's authoritative profile tree. Writes use a
temporary file and rename in the same configuration directory, which therefore
must be writable. Temporary names are unique; failed writes restore profile state.
Metadata configuration validation precedes mutation. RTSP
receives immutable settings and compiled filters; streaming callbacks never read
the mutable JSON/profile tree.

Persisted changes refresh profile factories at the same URI. New RTSP connections
receive the updated track/filter selection. Existing prepared sessions keep their
previous snapshot until reconnect, including when an association is removed.
Use a fresh RTSP connection for the updated configuration.
Request the URI again and restart affected streams after configuration changes,
as required for reliable application by Media/Media2. Unchanged factories share
media within a profile; different profiles use separate factories even when they
share a video encoder. Deleting a profile removes its URI for new connections.
Encoder tokens are resolved through the Media/Media2 GetStreamUri configuration
catalogs to the existing high/low pipeline variants; token spelling is not fixed.
The two catalogs must agree when they map the same encoder. Source/encoder
emulation retains the existing hardcoded pipeline limitations.

## Transport and synchronization

- High stream uses pay0 for video, pay1 for audio and pay2 for metadata. Low uses
  pay0 for video and pay1 for metadata. The metadata payload type is 107; SDP uses
  application media, VND.ONVIF.METADATA and a 90 kHz clock.
- A client may SETUP metadata alone, video/audio alone, or the aggregate tracks.
  TCP interleaving and unicast UDP use GStreamer's existing transport and bounded
  RTP/RTCP port pool.
- Each prepared live media instance has an independent shared-source subscription.
  The bridge polls its bounded queue on the RTSP server's private GLib context.
  GStreamer appsrc is nonblocking; its queue is gated at 128 KiB before another
  bounded batch of up to sixteen notifications is accepted. One larger batch can
  exceed that gate, but publication never waits on RTSP client I/O. Source queue
  limits and dropped-message diagnostics are described in
  [shared event delivery](EVENT_DELIVERY.md).
- Each buffer contains one complete UTF-8 `tt:MetadataStream` document. GStreamer's
  ONVIF payloader fragments it into RTP packets with a 1200-byte MTU and marks
  only the final fragment. UTC occurrence timestamps inside notifications are
  preserved; RTP PTS uses the pipeline clock minus its base time.
  Namespace bindings from `event.config` are captured through the shared source,
  including custom topic/payload prefixes; they are not limited to default ONVIF
  prefixes.
- A schema-valid empty document primes media preparation when there are no
  notifications. Empty documents are also sent at approximately one-second
  intervals during silence, subject to backpressure. They are not synthetic events.
- PLAY for a session with its metadata track selected requests fresh Initialized
  property states. Shared media send that snapshot to all current consumers of
  the stream; this supplies late clients without restarting generators. PAUSE
  and resume retain the media lifecycle and request fresh state on resumed PLAY.
  Property snapshots do not invent initialization for transient notifications.
  Media and Media2 SetSynchronizationPoint request fresh filtered property state
  and send an upstream force-key-unit event to the profile's video encoder.
  Other profiles and PullPoint queues are independent.
- Metadata RTCP reserves 4000 bits/s for senders and 12000 bits/s for receivers.
  This avoids very long sender-report intervals caused by automatic bandwidth
  estimation on sparse XML traffic. The normal five-second minimum RTCP interval
  remains in effect; actual intervals are randomized by GStreamer.

Compression, multicast RTP, media recording and historical metadata replay remain
unsupported.

## Lifetime and cleanup

Factory/media callbacks own shared bridge state rather than the RTSP Server
object. Unprepare stops the bridge, disconnects appsrc handlers, destroys its
timer and closes its event subscription. Last-client teardown permits a fresh
media instance on reconnect. Active shared streams survive another client's
TEARDOWN. Expired RTSP sessions are cleaned every five seconds; abrupt client
disconnect can retain a session until its negotiated timeout.

RTSP server destruction stops the listener, metadata bridges, client connections
and session pool, then quits and joins its private GLib loop. It handles both
active prepared/playing media and stopping immediately after starting the worker.
This is subsystem cleanup, not a new complete application signal/restart model.
The Event service's remaining global state and optional detached HTTP listener
still limit full application restart/multi-server behavior.

## Contract and validation

The implementation uses the pinned ONVIF 24.12 set at commit
`d7f3684501c4a6d58f879e72e1e3322c120f95e6`: Streaming metadata RTP and metadata
RTSP session sections, Core event filtering/property synchronization, Media
metadata configuration and synchronization sections, Media2 generic configuration
operations, `wsdl/ver10/media/wsdl/media.wsdl`,
`wsdl/ver20/media/wsdl/media.wsdl`, `metadatastream.xsd`,
`common.xsd`, `onvif.xsd` and imported definitions. Scene descriptions use the same release's
Analytics specification, Analytics WSDL 20.12 at
`wsdl/ver20/analytics/wsdl/analytics.wsdl` (module queries and supported metadata),
and the scene Frame/Object/Appearance/ObjectTree types in `metadatastream.xsd`.
The license plate descriptor follows that release's Analytics §5.4.12 and
`tt:LicensePlateInfo` / `tt:StringLikelihood` in `metadatastream.xsd`, also used by
the existing recognition rule event. The same descriptor is documented in
[Analytics 26.06 §5.4.12](https://www.onvif.org/specs/srv/analytics/ONVIF-Analytics-Service-Spec.pdf);
schema validation continues to use the pinned 24.12 contract.
Object images follow Analytics §5.4.13 Image Data and the same release's
`Appearance/ImageRef` (`xs:anyURI`) and `Appearance/Image` (`xs:base64Binary`),
in schema order after LicensePlateInfo. The embedded format is PNG.
See
[the contract references/cache instructions](../experiments/rtsp_metadata/README.md)
and [the immutable release](https://github.com/onvif/specs/tree/d7f3684501c4a6d58f879e72e1e3322c120f95e6).
GStreamer API references are [appsrc](https://gstreamer.freedesktop.org/documentation/app/appsrc.html),
[RTSP client](https://gstreamer.freedesktop.org/documentation/gst-rtsp-server/rtsp-client.html)
and [metadata payloader](https://gstreamer.freedesktop.org/documentation/rsonvif/rtponvifmetadatapay.html).
The example Device service advertises Media/Media2 version 24.12 for this baseline.
These are implementation references, not an official conformance claim.

Build and run CTest in the metadata-enabled development image:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DENABLE_UNIT_TESTS=ON
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
```

The RTSP lifecycle test covers disabled/enabled SDP, quiet preparation, metadata
playback, active-client shutdown and immediate start/stop. Its enabled case reports
a skip in an environment without the metadata plugin; run it in `dev`
for positive coverage.

Against a source-built server with this switch enabled, default event settings
and disabled authentication, run:

```bash
python3 unit_tests/network_smoke.py --profile-uris
python3 unit_tests/event_delivery_smoke.py
python3 unit_tests/rtsp_metadata_smoke.py --contract /contract
python3 unit_tests/metadata_configuration_smoke.py --contract /contract
python3 unit_tests/scene_metadata_smoke.py --contract /contract
```

`/contract` is the pinned schema cache mounted into the test container.
Omit that argument for a standard-library-only receiver without XSD validation.
The configuration and scene scripts additionally use the validation-only
`xmlschema` Python package (tested with Debian python3-xmlschema 3.4.5). The `dev`
image includes this package together with `lxml` and GObject bindings. The validator
uses XSD 1.1 priority rules for the unchanged upstream schema: libxml2's XSD 1.0
checker rejects VideoSourceConfigurationOptionsExtension2 because of its
element/wildcard model.
The pinned WSDL/XSD files remain unchanged and outside the checkout. Network
imports are disabled and hash-pinned cached imports are used. This validation
choice is not a claim of complete ONVIF conformance.

`network_smoke.py` detects profile URI support from Media2 capabilities;
`--profile-uris` explicitly selects that mode. The configuration
smoke script requires writable disposable default configs; it restores the
settings and associations it changes. The transport smoke script attaches both
stock profiles, obtains their SOAP URIs and removes its associations afterwards.
Run scripts that change profiles/rules sequentially against the same instance.
The scene script also requires disposable default configs. It checks fixed module
queries and faults, scene-only and mixed streams, module selection, profile and
active-session isolation, and identical scene/event ObjectId and UTC in RTSP and
PullPoint. It also checks a nonempty supported LicensePlateInfo descriptor,
LPR scenes without rules over TCP/UDP, and matching plate number, country,
confidence, bounding box and UTC across scene frames, RTSP events and
PullPoint. Its Analytics schema loader explicitly includes `metadatastream.xsd`
alongside `onvif.xsd`, which share the same target namespace. Image checks fetch
the advertised URLs, verify PNG responses and compare them byte-for-byte with
decoded base64 in supported metadata and live TCP/UDP scenes.

All scripts accept explicit host/port options. The metadata script checks two
RTSP consumers plus two PullPoints, identical live events, late property state,
marker-delimited fragmented XML, 90 kHz SDP, monotonic timestamps, RTCP sender
reports, pause/resume, both live mounts, metadata-only and aggregate selection,
audio/video-only selection, and reconnect after last-client teardown. The same
script can exercise a configuration copy with a sufficiently long video/audio
file input. Use temporary configs and the environment-specific network settings
from BUILD.md/WSL2.md. Windows/LAN/VMS acceptance remains a separate check.

The restart check starts its own source-built server processes on separate
loopback ports using configuration copies. Run it independently:

```bash
python3 unit_tests/metadata_restart_smoke.py --binary build/main
```

It verifies URI availability before SOAP requests, custom encoder tokens,
persistence across process restart, ForcePersistence=false and disabled-transport
startup rejection. It does not provide full application restart support within
one process.

### Coverage and deployment acceptance

Unit coverage includes shared serialization, namespace bindings, filters,
subscription independence, configuration faults and rollback, immutable scene
snapshots, object ID lifetime and bounded reader recovery. Network checks cover
SDP, TCP/UDP fragmentation and MTU/marker behavior, RTCP, late join, silence,
synchronization, concurrent RTSP/PullPoint consumers, configuration persistence
and active-session snapshots. The scene check matches ObjectId and UTC across
scene frames and notifications received through both delivery methods.

Container tests do not establish Windows/LAN/VMS interoperability or sustained
load/backpressure behavior. For a target deployment, follow [WSL2.md](WSL2.md),
verify both TCP and UDP reception in a metadata-aware client, and compare metadata
notifications with an independent PullPoint subscriber. Verify unicast and
multicast discovery on the target host: an isolated container pass does not
establish host-network discovery behavior. A video player displaying
video alone is insufficient metadata validation. Synthetic scenes have no visual
or exact encoded-frame correlation; compression, historical metadata replay and
multicast metadata remain unsupported. These checks are not an official ONVIF
conformance claim.
