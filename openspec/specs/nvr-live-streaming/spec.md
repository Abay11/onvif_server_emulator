# nvr-live-streaming Specification

## Purpose

Deliver the media configured for each NVR profile while reducing redundant video encoding on the host.

## Requirements

### Requirement: Profile-specific live streams

Media and Media2 SHALL resolve each stream profile to its own stable live URI using the requested supported transport and the configured advertised endpoint. The endpoint SHALL deliver video matching the configured encoding parameters and the audio and metadata tracks attached to the profile.

#### Scenario: Inspect two streams with different configurations

- **WHEN** a client requests stream URIs for profiles with different resolutions and only one has an audio attachment
- **THEN** the returned URIs are distinct and the resulting media matches each profile's resolution and track composition

#### Scenario: Change the video settings of an existing profile

- **WHEN** changed settings are activated for an existing profile
- **THEN** its live URI still resolves and new sessions receive the updated settings

### Requirement: Reuse compatible encoded video

The emulator SHALL reuse one encoded video source for concurrent default synthetic H.264 streams with equivalent video settings. Sharing SHALL preserve each profile's tracks and availability for its active clients. Streams with incompatible effective input or encoding settings SHALL produce the independently configured video.

#### Scenario: Share video across profiles with different tracks

- **WHEN** clients open two default H.264 profiles with identical video settings and different audio or metadata tracks
- **THEN** runtime inspection shows one video encoder for both and each session receives only the tracks attached to its profile
- **AND** the remaining session continues receiving video when the other disconnects

#### Scenario: Split the encoder when settings differ

- **WHEN** two clients concurrently request profiles with different resolutions or effective encoding settings
- **THEN** each receives the configured video and shares encoding only with equivalent profiles
