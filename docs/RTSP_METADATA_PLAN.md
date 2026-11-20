# RTSP Metadata Implementation Plan

## Goal and recommended approach

Deliver the emulator's notifications concurrently through the existing PullPoint
API and an ONVIF metadata RTP track controlled through RTSP. Keep GStreamer and
add an application-fed metadata branch to the live media factories. Reuse event
generation and notification serialization across both delivery paths.

Use GStreamer's `appsrc` and `rtponvifmetadatapay` if the latter can be packaged
with the documented development and runtime images. This is the preferred path:
GStreamer continues to own SDP generation, RTP fragmentation, RTCP and transports.
The metadata payloader belongs to the Rust ONVIF plugin family; installing the
existing C/C++ bad plugins does not establish that it is available.

There are two different descriptions to account for:

- RTSP stream description: SDP returned by DESCRIBE, including the metadata track.
- Analytics scene description: timed object/frame data under VideoAnalytics in
  the metadata XML. This requires a producer beyond the existing notification
  generators and is planned separately below.

## Initial repository findings

- `RtspServer.cpp` creates shared high and low live factories. High has video and
  audio, including a file-input variant; low has video only. Replay is separate.
- `NotificationMessage` is currently declared in `pull_point.h`. Its data model
  already includes topics, UTC timestamps, property operations, simple items and
  structured element items, including recognition data.
- `serialize_notification_messages()` combines notification serialization with
  PullMessages response fields. Extract a reusable notification serializer.
- Generators expose Boost.Signals2 connections. They can feed both delivery
  paths without duplicating simulation or converting SOAP back into events.
- `CreatePullPoint()` currently uses a fixed `s0` identifier and stops/restarts
  generators for every subscription. These behaviors must be corrected for
  independent concurrent consumers.
- The Event service owns its manager as file-level state. Introduce an explicit
  shared event-source dependency for RTSP rather than exposing that global.
- Profiles have empty Metadata token slots, but metadata configuration handling
  and RTSP integration are not implemented. The manager and `ProfilesConfig()`
  are separate configuration views; changes must not leave either stale.
- Shared RTSP mount points are not automatically equivalent to arbitrary mutable
  ONVIF profiles. URI selection must preserve each profile's metadata settings.

## Stage 1: Contract and GStreamer feasibility

Status: completed. The pinned upstream plugin builds and loads in the optional
development and runtime images. Offline receivers passed TCP/UDP, fragmentation,
schema validation, quiet startup, late-join and video-only checks. See the
[experiment, contract baseline and results](../experiments/rtsp_metadata/README.md).

1. Select a released ONVIF baseline and pin exact specification revisions and
   WSDL/XSD commits, including imports. Record required operations, capabilities,
   faults, filtering and synchronization semantics before implementation.
2. Verify the active Docker context and use the documented `dev` image. Inspect
   `appsrc` and `rtponvifmetadatapay` there, including caps and plugin versions.
3. Prefer a compatible distribution package. If unavailable, build a pinned
   upstream ONVIF plugin in a container build stage and ship its required runtime
   artifacts and license notices. Check its GStreamer/Rust requirements first.
4. Build a small disposable RTSP prototype with a static valid metadata document.
   Prove DESCRIBE, SETUP and PLAY over TCP and UDP, including a quiet event source,
   fragmentation of a large document and a second client joining later.
5. Check that a metadata branch cannot hold up media preparation or DESCRIBE while
   waiting for its first event. Establish valid initialization behavior.

Exit criterion: an actual receiver reconstructs schema-valid ONVIF metadata from
the container's RTSP stream. Do not start by implementing custom RTP transport.
If packaging the upstream plugin is materially harder than expected, evaluate a
small independently implemented `GstRTPBasePayload` subclass as a fallback,
retaining GStreamer's RTSP server and RTP infrastructure.

## Stage 2: Shared event delivery and XML

Status: implemented. Shared source injection, immutable bounded consumer queues,
reusable notification/metadata serialization and independent PullPoint delivery
are covered by the SharedEvents suite. See [Shared Event Delivery](EVENT_DELIVERY.md)
for behavior, references, validation and remaining subscription simulation limits.

1. Move the transport-independent notification model and single-notification XML
   serializer out of PullPoint code. Keep PullMessages envelope/timing fields in
   the PullPoint adapter.
2. Add a shared event-source interface supporting scoped subscriptions and
   property-state snapshots. Inject it through `Server` into the Event service
   and RTSP server. Keep existing generator registration extensibility.
3. Start generators once. Give each PullPoint a unique reference and independent
   subscription lifetime. Connecting or disconnecting a consumer must not reset
   simulation timers or consume another consumer's notifications.
4. Serialize notifications as `tt:MetadataStream/tt:Event/wsnt:NotificationMessage`,
   with namespace bindings covering topics and structured payloads. Use complete
   UTF-8 XML documents per event or bounded batch initially.
5. Execute generator state and subscription operations on the event executor.
   Pass immutable data through bounded queues to the streaming side. Never block
   event generation on a slow RTSP consumer. Define overflow diagnostics and
   recovery, including property-state resynchronization where applicable.

Exit criterion: deterministic tests deliver the same event identity, timestamp
and payload to PullPoint and metadata subscribers independently. Cover property
initialization, transient events and structured recognition payloads.

## Stage 3: Integrate live RTSP metadata

Status: implemented and validated in the source-built development environment
and standard runtime image. Implementation uses an opt-in
`rtsp_metadata.enabled` transport switch until
Stage 4 supplies metadata configuration/profile control. This documented staging
deviation enables both fixed live mounts without advertising SOAP metadata support.
See [live RTSP metadata](RTSP_METADATA.md) for behavior and validation.

Conceptual additional branch; Stage 1 determines exact caps and properties:

```text
appsrc name=metadata_src is-live=true format=time
  ! rtponvifmetadatapay name=payN pt=107
```

1. Add the branch to high, low and file-input live pipelines when their profile
   includes metadata. Use consecutive payloader names: high already uses pay0
   and pay1; low uses pay0. Pick a non-conflicting dynamic payload type.
2. Connect the source during media configuration. Timestamp buffers using pipeline
   running time; retain event occurrence time as UTC inside XML.
3. Let the payloader/server produce the metadata SDP media section and RTP/RTCP.
   Verify application media type, ONVIF encoding name, 90 kHz clock, document-end
   marker behavior and packets that cross the configured MTU.
4. Implement safe media unprepare/reprepare, subscription disconnect, buffer
   ownership, flushing and shutdown. Account for existing incomplete RTSP shutdown.
5. Handle shared factories explicitly: a late client and synchronization request
   must receive the required current property state even while others are playing.
   Verify whether a shared snapshot suffices under the selected contract before
   choosing per-session media. Test metadata-only track SETUP and playback too.

Exit criterion: PullPoint and multiple RTSP clients receive live notifications
simultaneously; clients selecting video/audio alone continue working normally.
Replay metadata is outside this live-stream milestone.

## Stage 4: ONVIF configuration and discoverability

Status: implemented. Media/Media2 metadata configuration, profile associations,
namespace-aware topic and ItemFilter filtering, profile-specific stable RTSP URIs,
immutable active-session settings, persistence and media synchronization are
covered by unit and SOAP/RTP checks. See [RTSP metadata](RTSP_METADATA.md) for
supported values, operations, lifecycle and validation limits.

1. Add metadata configuration storage, JSON schema, defaults and copied fixtures.
   Use authoritative configuration snapshots for RTSP rather than accessing the
   mutable profile tree from streaming threads.
2. Implement required metadata get/list/options/set and compatible-configuration
   operations for Media and Media2, plus each service's profile attachment/removal
   mechanisms. Serialize metadata references in profile responses.
3. Implement Events enabled/disabled and the supported topic/content filter
   semantics. Validate dialects and expressions; return specified faults for
   unsupported inputs instead of silently ignoring filters.
4. Implement the applicable media synchronization-point operation alongside
   PullPoint SetSynchronizationPoint. Snapshot property events without inventing
   initialization events for transient notifications.
5. Resolve profiles that share an existing RTSP mount but have different metadata
   settings. Reuse a factory only for compatible settings; otherwise return a
   stable profile-specific URI built through `RtspAddress()`.
6. Define and test when configuration changes affect active and new sessions,
   according to the selected specification. Ensure persistence and both current
   profile views agree. Test invalid tokens and configuration associations.
7. Update only the capabilities actually implemented. Event metadata alone does
   not establish support for analytics scene descriptions or all metadata types.

Exit criterion: a client can discover and configure metadata through SOAP,
retrieve the matching RTSP URI, and receive the configured events without
creating a PullPoint subscription. PullPoint remains independently usable.

## Stage 5: Analytics scene description

Status: implemented. The fixed read-only `SyntheticObjectTracker` module emits
schema-validated scene frames from the Line Detector simulator's object samples.
Scene-only and mixed event/scene streams, analytics/module selection, profile
isolation and immutable active sessions are covered by unit and SOAP/RTP checks.
The network check verifies identical ObjectId and UTC across a scene frame,
RTSP line-crossing notification and PullPoint notification. See
[scene configuration and simulation limits](RTSP_METADATA.md#synthetic-scene-description).
Windows/LAN/VMS acceptance remains part of Stage 6.

If "stream description" means scene/object information, include this stage in
the requested delivery. SDP support is already covered by Stage 3.

1. Expose immutable synthetic object/frame samples from the analytics simulator.
   Produce VideoAnalytics Frame data with UTC time, stable object identifiers and
   schema-defined geometry/transformation and classification where supported.
2. Make scene frames and rule notifications derive from the same simulation
   samples. Do not reconstruct trajectories from notification text.
3. Respect the metadata configuration's analytics selection. Add PTZ metadata only
   as a separately enabled producer with truthful capabilities.
4. Document that object samples are synthetic: the present video test patterns
   and arbitrary file input are not analyzed or visually correlated automatically.
   Visually matching objects would require a further simulation/rendering change.

Exit criterion: a metadata-aware receiver parses timed scene frames, and their
object identifiers/times agree with the associated simulator notifications.

## Stage 6: Integration acceptance and documentation

- Unit checks: shared serializer, namespace correctness, XML escaping, filter
  semantics, snapshots, subscription independence and configuration faults.
- Schema checks: validate XML against the pinned schemas and imports. Keep
  semantic checks separate; well-formed XML is insufficient.
- RTP checks: SDP track discovery, TCP/UDP reconstruction, marker/timestamp/MTU
  behavior, RTCP reports and large embedded image events.
- Lifecycle checks: event silence, late join, two RTSP clients plus two PullPoints,
  slow consumers, configuration changes, TEARDOWN, reconnect and shutdown.
- Extend `unit_tests/network_smoke.py` to select metadata tracks dynamically and
  compare a deterministic notification received by both delivery methods.
- In the documented development container run CMake with unit tests enabled,
  `cmake --build build --parallel 2`, and `ctest --test-dir build --output-on-failure`.
  Run the network smoke script against a source-built server using copied configs.
- Verify Windows/LAN operation and a metadata-capable target VMS using the WSL2
  guide. A video player displaying video is insufficient metadata validation.
- Update architecture, configuration, build dependencies and receiving examples.

Stages 1–4 and 6 deliver event metadata plus SDP. Stage 5 additionally delivers
scene descriptions. Compression, historical metadata replay and multicast RTP
are subsequent features, with unsupported behavior documented and unadvertised.

## References and planning validation

Sources consulted during planning:

- [ONVIF Streaming](https://www.onvif.org/specs/stream/ONVIF-Streaming-Spec.pdf),
  served revision 26.06: metadata RTP and SDP sections 5.1.2.1.1 and 5.2.2.4.
- [ONVIF Media](https://www.onvif.org/specs/srv/media/ONVIF-Media-Service-Spec.pdf),
  served revision 24.12, and
  [Media2](https://www.onvif.org/specs/srv/media/ONVIF-Media2-Service-Spec.pdf),
  served revision 26.06.
- [Media WSDL](https://www.onvif.org/ver10/media/wsdl/media.wsdl),
  [Media2 WSDL](https://www.onvif.org/ver20/media/wsdl/media.wsdl), and
  [Events WSDL](https://www.onvif.org/ver10/events/wsdl/event.wsdl).
- [Metadata schema](https://www.onvif.org/ver10/schema/metadatastream.xsd) and
  [ONVIF schema](https://www.onvif.org/ver10/schema/onvif.xsd). The official
  repository development copies inspected declare revision 25.12; these are
  research references, not a pinned implementation baseline. Stage 1 must select
  and document a compatible released set rather than silently combining them.
- [GStreamer ONVIF payloader](https://gstreamer.freedesktop.org/documentation/rsonvif/rtponvifmetadatapay.html)
  and [appsrc](https://gstreamer.freedesktop.org/documentation/app/appsrc.html).

No third-party specification text, schema files or implementation code is copied
into this plan. Preserve applicable notices and record provenance if schemas or
plugins are later included for validation/distribution.

Planning inspection found local GStreamer 1.26.2, but
`gst-inspect-1.0 rtponvifmetadatapay` reported no such element. Docker context
`default` targets `unix:///var/run/docker.sock`; sandbox access to the daemon was
denied, so development-image plugin availability remains unverified. No application
build, CTest, network test or metadata prototype was run for this planning change.
