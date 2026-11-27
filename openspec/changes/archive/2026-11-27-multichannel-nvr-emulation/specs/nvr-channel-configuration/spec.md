# Spec Delta

## Purpose

Expose a single NVR with independently configurable inputs and consistent source ownership across ONVIF services.

## ADDED Requirements

### Requirement: Persistent channel topology

The device SHALL support 1–32 channels. Each channel SHALL have a distinct video source, its own component inventory and ONVIF configuration entities, and one or more configurable video streams with distinct media profiles. Identities in the explicit topology SHALL survive restart, renaming and edits to other channels.

#### Scenario: Start with the bundled configuration

- **WHEN** the emulator starts with the bundled configuration
- **THEN** it exposes one channel defined by the explicit topology

#### Scenario: Configure heterogeneous inputs

- **WHEN** the administrator configures 32 channels, adds two streams with different resolutions to the first channel, and enables audio only on that channel
- **THEN** the running device exposes 32 distinct video sources and the configured profiles, with audio available only on the first channel

#### Scenario: Reject an invalid channel count

- **WHEN** an administrator submits zero or more than 32 channels
- **THEN** the configuration is rejected and the saved and active topologies are preserved

### Requirement: Channel-scoped ONVIF compatibility

The device SHALL use channel ownership and operation-specific compatibility rules for Media and Media2 profile queries and associations and PTZ compatibility queries. Requests without a profile selector SHALL retain the scope defined by the operation. Source and capability listings SHALL reflect the active topology.

#### Scenario: Query compatibility on a channel without audio

- **WHEN** a client queries compatible audio configurations using a valid profile from a video-only channel
- **THEN** the response contains no compatible audio configurations, even if a different channel supports audio

#### Scenario: Reject a cross-channel association

- **WHEN** a client attempts to attach a configuration owned by another channel to a valid media profile
- **THEN** the device returns the applicable SOAP fault and preserves the profile

#### Scenario: Query an unknown profile

- **WHEN** a client supplies an unknown profile token to a compatible-configuration query
- **THEN** the device returns the operation's unknown-profile fault

### Requirement: Channel administration

The admin WebUI SHALL allow administrators to select the channel count, populate new channels from a template, and edit each channel's components and stream list. Topology saves SHALL validate the entire configuration, reject stale revisions, and indicate that restart is required to activate changes.

#### Scenario: Review a saved topology before activation

- **WHEN** an administrator saves new channels and customizes one of their streams
- **THEN** the UI shows the saved configuration as pending restart while the running ONVIF device continues to expose its active topology

