# Proposal

## Why

The current multichannel mode duplicates the first channel's profiles in SOAP responses. NVR clients need independently configurable inputs to exercise heterogeneous cameras, source-scoped analytics, and selective recording.

## What Changes

- Configure 1–32 channels on one device. Give each channel persistent source and configuration identities, its own component inventory, and an editable list of video streams.
- Scope ONVIF profile compatibility, configuration mutations, and stream resolution to the owning channel. Return stable per-profile live RTSP URIs.
- Reuse compatible generated and encoded video across channels while keeping each stream's audio and metadata independent.
- Configure analytics modules, detector types, rules, automatic generation, and manual triggering per channel. Preserve source identity in ONVIF Events and RTSP metadata.
- Add simulated embedded recording for selected channels, with ONVIF Recording Control, Search, and Replay views of the same archive.
- Extend the admin WebUI with channel configuration, channel selection for detectors, and recording controls.
- **BREAKING**. Deploy the NVR with the new explicit channel configuration and a fresh simulated archive. Clients discover the new profile tokens and RTSP URLs.

## Capabilities

### New Capabilities

- `nvr-channel-configuration` covers channel topology, component ownership, ONVIF compatibility, and WebUI configuration.
- `nvr-live-streaming` covers per-profile RTSP endpoints and shared encoded video.
- `nvr-channel-analytics` covers channel-specific analytics, detectors, events, and metadata.
- `nvr-embedded-recording` covers simulated recording control, archive search, replay, and WebUI operations.

### Modified Capabilities

## Impact

Configuration schemas and validation, Media/Media2 and source-aware SOAP services, GStreamer RTSP, Analytics and Events, Recording Control/Search/Replay, and the embedded admin UI are affected. Recorded intervals and replay media are synthetic. Video files are not written to storage by recording jobs.
