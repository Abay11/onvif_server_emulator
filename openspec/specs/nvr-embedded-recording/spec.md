# nvr-embedded-recording Specification

## Purpose

Let administrators simulate an embedded NVR archive for selected channels and let ONVIF clients observe it consistently.

## Requirements

### Requirement: Selective recording control

The admin WebUI SHALL allow administrators to select channels and their recording streams, display job state, and start or stop simulation without restart. On first start, the device SHALL create a Recording and RecordingJob for the selected channel. Repeated starts and stops SHALL be idempotent.

#### Scenario: Enable recording on selected channels

- **WHEN** an administrator starts recording on two of three unconfigured channels
- **THEN** there is one active recording job and one recording for each selected channel and no job or recording is created for the third

#### Scenario: Stop and resume recording

- **WHEN** an administrator stops an active job and later restarts it
- **THEN** the job is Idle during the gap, preserves its job and recording tokens on restart, and opens a new simulated time interval

### Requirement: Consistent ONVIF archive

Recording Control, Recording Search and Replay Control SHALL refer to the same recordings, jobs, sources, tracks and UTC intervals. A stopped recording SHALL retain its searchable archive. Job mode changes supported by SOAP SHALL be reflected in the WebUI.

#### Scenario: Search and replay a stopped channel

- **WHEN** a client searches for a source with a stopped recording and requests replay for the returned token
- **THEN** search reports the matching source and archive bounds, and replay resolves to the recording's synthetic media

#### Scenario: Filter the recordings by source

- **WHEN** a client searches for one recorded channel after recording two channels
- **THEN** the results contain recordings for the requested source

#### Scenario: Request an unknown recording

- **WHEN** a client submits an unknown recording token to GetReplayUri
- **THEN** the device returns the applicable recording fault

### Requirement: Simulated recording intervals

The emulator SHALL retain simulated UTC intervals and recording identities across a normal application restart. Replay SHALL simulate media only for intervals that are available for the requested recording. Recording jobs SHALL operate without writing video files.

#### Scenario: Replay across a recording gap

- **WHEN** a client requests replay across two recorded intervals separated by a period in which the job was Idle
- **THEN** the gap is not presented as recorded media
