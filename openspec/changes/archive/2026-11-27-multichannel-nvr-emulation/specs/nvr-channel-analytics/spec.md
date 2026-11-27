# Spec Delta

## Purpose

Allow NVR channels to simulate independently configured analytics engines and detectors and identify their output to clients.

## ADDED Requirements

### Requirement: Per-channel analytics inventory

Each channel SHALL support a configurable set of analytics modules and detector types, which can be empty. Analytics queries addressed by configuration token SHALL report the supported and configured items for that configuration. Rule mutations SHALL enforce the allowed types and dependencies of the addressed configuration.

#### Scenario: Advertise different analytics capabilities

- **WHEN** a client queries the analytics configurations of a motion-only channel and a license-plate recognition channel
- **THEN** supported rule and module lists reflect each configuration's capabilities and configured lists contain its own items

#### Scenario: Submit an unavailable rule type

- **WHEN** a client tries to create a license-plate recognition rule on the motion-only channel
- **THEN** the service returns the applicable fault and preserves both channels' rules

### Requirement: Independent detector operation

The admin WebUI SHALL allow administrators to select a channel, edit its rules using its source geometry, manage detector enablement and automatic generation settings, and trigger an enabled detector once. Detector controls, alarms, counts, and object history SHALL be isolated by source and, where applicable, analytics configuration and rule.

#### Scenario: Run the same detector type on one channel

- **WHEN** two channels have enabled instances of the same detector type and an administrator runs the first manually
- **THEN** only the first channel's selected detector runs and only its event and detector state can change as a result

#### Scenario: Disable automatic generation independently

- **WHEN** an administrator disables automatic generation for a detector on one channel
- **THEN** the detector on the other channel continues according to its own settings

### Requirement: Source identity in analytics output

Analytics notifications SHALL identify the originating channel through the applicable ONVIF source fields. RTSP metadata configured for a profile SHALL respect its source, analytics configuration and filters. Property synchronization SHALL preserve source identity for each reported property.

#### Scenario: Share H.264 without mixing analytics

- **WHEN** two live profiles share encoded video and a detector on the first channel publishes an event and scene
- **THEN** PullPoint identifies the first channel as the source and the second channel's metadata stream contains neither the first channel's event nor its scene
