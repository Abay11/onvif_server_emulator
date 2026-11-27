# Design

## Context

Media and Media2 clone or normalize multichannel tokens in individual handlers. The media manager already stores profile associations, while Media and Media2 currently read separate encoder descriptors. RTSP launches fixed H.264 pipelines and creates factories for metadata-enabled profiles. Analytics state already has some source-aware paths, but the supported-type lists and detector controls are global. Search and Replay load their own views of the configured synthetic archive.

## Goals / Non-Goals

Use one ownership model across administration, SOAP, streaming, analytics and recording. Keep the running topology coherent while a replacement is being edited. Use embedded synthetic media and persisted archive metadata. Physical capture, disk video recording, remote camera provisioning and hot topology reload are outside this change.

## Decisions

### Keep an explicit topology with stable identities

Persist one validated topology document with one record per channel. It stores stable channel and video source identity, component inventory, and stream/profile definitions. The channel count is derived from the list. WebUI templates copy configuration values and allocate new identities. One document lets the existing revision and atomic-save pattern validate a complete edit without coordinating a transaction across separate channel files.

Project the topology into the existing media manager and keep profile and configuration changes under that authority. Use one effective encoder configuration to produce each supported Media or Media2 representation and the RTSP settings. Validate ownership on both WebUI saves and native SOAP mutations. For SOAP-created incomplete profiles, resolve ownership on the first successful channel-bound attachment and keep it stable thereafter. Derive profile limits and capabilities from supported operations and active topology. Audit DeviceIO, Imaging, PTZ, and snapshot handling for lookups that assume the first source.

The WebUI stages topology changes through the existing authenticated configuration and restart paths. Show active and saved revisions separately. Preserve the identities of retained channels when resizing, identify affected profiles before removing channels, and invalidate stale drafts after a conflicting save. Channel removal closes active recording intervals at activation and preserves historical archive entries with their source descriptor.

### Share encoded video and retain per-profile RTSP delivery

Use a stable RTSP mount per stream profile and resolve it through the existing transport and advertised-address helpers. Create an on-demand producer pool keyed by effective video input, codec, dimensions, frame rate, rate control, codec profile, GOP and other output-affecting settings. Fan out encoded access units before payloading. Each RTSP factory can then add its own audio and metadata tracks. GStreamer's default shared-media cache keys on the full URL, which by itself cannot share encoders across those mounts. Sharing an entire RTSP media object would couple their track composition. [GStreamer RTSP Media Factory](https://gstreamer.freedesktop.org/documentation/gst-rtsp-server/rtsp-media-factory.html)

Prototype the encoded-buffer handoff with existing GStreamer components and verify bounded queues, timestamps, codec initialization and keyframes for late joiners, disconnect isolation, and cleanup after the last consumer. Keep existing encoder formats subject to actual pipeline support, and reject configurations that the emulator cannot produce. Measure producer and encoder counts and CPU with concurrent readers before claiming resource savings.

### Scope analytics at configuration, sampling and delivery

Associate supported and installed modules and detector types with each channel's analytics configuration. Reuse existing rule validation, scene models, and detector controls, resolving the available inventory before validating mutations. Key detector settings and manual runs by channel and detector, and key rule state by analytics configuration and rule as applicable. Dispatch synthetic samples on the existing event worker with an explicit source to avoid a separate worker per channel. Use the same channel-aware controls for generation, synchronization, journal and subscriber delivery. Enforce source and configuration selection before applying RTSP event filters and emitting scenes. Keep device-wide hardware signals, such as existing device-level digital inputs, at device scope.

### Use a single persisted simulated archive

Share one synchronized archive between WebUI, Recording Control, Search and Replay. Use a channel's selected media profile as the recording source and persist the source descriptor and track settings required for historical search and replay. First WebUI start creates one managed recording and job per channel. Serialize state changes so retries or simultaneous callers cannot duplicate jobs or leave open intervals. Stop closes the open UTC interval and changes mode to Idle. Normal restart closes active intervals before shutdown and resumes enabled jobs with new intervals at startup. Archive persistence stores metadata and time ranges.

Keep recording history addressable by recording token and resolve replay to its own URI. Use a stable search-session snapshot and respect source/recording filters, result limits, and expiration. Enforce archived intervals during replay so pauses and downtime never appear as captured media. Adapt existing synthetic replay machinery to the recorded track settings. Register Recording Control through the existing service factory, observe its access classes and faults, and derive advertisement and capabilities from the supported operations. Keep WebUI and SOAP mode changes on the same state transition.

### Protocol references

Use the service definitions and imported schemas listed by the [ONVIF Network Interface Specifications](https://www.onvif.org/profiles/specifications/). Start with Media 24.12, Media2 26.06 and Analytics 26.06 and their respective `media.wsdl` and `analytics.wsdl` contracts. Use Recording Control 26.06 and the Recording Search and Replay Control specifications from the same release set, with `recording.wsdl`, `search.wsdl`, `replay.wsdl` and applicable Streaming definitions. Check affected DeviceIO, Imaging, PTZ and Event operations against their service WSDLs and specifications. Pin the chosen ONVIF release sources and record the versions and imports actually used, including `onvif.xsd`, `common.xsd`, `rules.xsd` and `metadatastream.xsd`. Keep additional simulation limits documented with the affected behavior.

## Risks / Trade-offs

- A slow RTSP client or a late joiner can disrupt shared delivery. Bound each consumer queue and verify decoder recovery and timestamps under concurrent sessions.
- Concurrent edits or recording operations can diverge across WebUI and SOAP. Use the existing revision checks for configuration and serialize archive updates with persistence before publication.
- A 32-channel topology can exceed the configured UDP RTP/RTCP pool when clients request several tracks or streams simultaneously. Verify the documented 32-channel workload and record the port-pool requirements for concurrent UDP streams.

## Deployment

Ship a valid explicit one-channel configuration as the default. Start with the updated configuration directory and an empty archive, then configure additional channels in the WebUI. Remove the `multichannelSimulation` shorthand and the profile-token rewriting that supported it. Clients obtain stream addresses from the new profiles through ONVIF discovery and Media or Media2 queries.
