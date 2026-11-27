# Tasks

## 1. Channel topology and configuration

- [x] 1.1 Document the pinned ONVIF service and schema contracts used by the change. Check affected operation names, token scopes, access classes and faults against those definitions before editing handlers.
- [x] 1.2 Add the explicit channel document, ownership indexes and revisioned save. Ship a default one-channel topology and remove the `multichannelSimulation` config setting. Verify the bundled topology, 32 channels, rejected out-of-range counts, stable tokens and invalid references using schema and loader tests. Document the new configuration format.
- [x] 1.3 Integrate active topology and persistent profile mutations with the media manager. Verify ownership for incomplete profiles, stable identities after reload, and consistency of Media and Media2 representations.
- [x] 1.4 Replace cloned profiles and first-source assumptions in affected Media, Media2, DeviceIO, Imaging, PTZ and snapshot paths. Check source and capability listings, valid and unknown profile queries, cross-channel attachment rejection and a channel with no audio. Validate representative SOAP responses and faults against the pinned schemas where practical.
- [x] 1.5 Add the Channels editor to the existing admin UI. Verify template expansion, per-channel components and streams, stale-save handling and activation after restart in browser smoke tests. Document the saved and active views.

## 2. Live RTSP delivery

- [x] 2.1 Prototype encoded-video fan-out using existing GStreamer components. Demonstrate one H.264 encoder for two different RTSP mounts, a late join that decodes, and continued playback after either mount disconnects.
- [x] 2.2 Implement stable profile mounts, supported encoder parameter mapping and independent track composition. Verify actual SDP, decoded resolution and tracks, Media and Media2 URI consistency, and supported TCP, UDP and HTTP tunnel requests.
- [x] 2.3 Integrate the on-demand producer pool and queue lifecycle. Verify same-key reuse with different track combinations, different-key separation, slow-consumer isolation and producer cleanup. Document supported inputs and settings and measure encoder counts and CPU against independent pipelines.

## 3. Analytics and detectors

- [x] 3.1 Add channel-specific analytics capability and configured inventory lookups. Verify supported rules and modules, configured rules and modules, correct source geometry, and SOAP faults for unsupported types or mismatched configuration tokens.
- [x] 3.2 Scope detector scheduling, controls, manual steps, and runtime state by source. Verify independent triggers, timing, counts and alarms for two channels using the same detector type.
- [x] 3.3 Carry source identity through PullPoint, property synchronization, the journal and per-profile RTSP metadata. Verify sample events and scenes never cross channels when video is shared, and check representative XML payloads against the applicable schemas.
- [x] 3.4 Extend the Detectors panel with channel selection. Verify rule edits, manual runs, and automatic generation controls affect only the selected source in browser tests. Document channel-specific controls and how to verify emitted events.

## 4. Embedded recording

- [x] 4.1 Add the persisted archive store. Verify an empty archive on first startup, atomic starts and stops, concurrent duplicate requests, restart with active jobs, and historical entries for removed channels with a controlled clock and copied fixtures.
- [x] 4.2 Register the Recording Control service and implement the recording/job operations and capabilities needed by the supported simulation. Verify job transitions, token lookup, stable track/source bindings, authorization and SOAP faults.
- [x] 4.3 Resolve Search and Replay against the shared archive. Verify source scoping, paginated search, unknown tokens, recording-specific URI resolution, and synthetic replay before and after a gap. Document supported recording operations and simulation limits.
- [x] 4.4 Add the authenticated Recordings panel with source and stream selection, job status and start/stop controls. Verify that WebUI actions and SOAP queries observe the same state and that unselected channels remain untouched. Document the recording workflow.

## 5. Integrated NVR verification

- [x] 5.1 Build affected targets and run CTest in the documented WSL/Linux development workflow. Run `python3 unit_tests/network_smoke.py` against a source-built running server with copied configuration and record transport or environment limits.
- [x] 5.2 Run an end-to-end 32-channel scenario with mixed components, multiple streams, concurrent clients, shared video with isolated detector output, and selected-channel recording followed by search and replay. Record encoder counts, RTP port usage, CPU and any unverified client or transport cases.
