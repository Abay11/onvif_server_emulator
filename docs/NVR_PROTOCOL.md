# Multichannel NVR protocol references

The normative baseline is the ONVIF June 2026 release at commit
`68ee1b540a40f848c9599eba2c55b87547c588d6`. Individual documents retain their
own version numbers within that release. The paths below are relative to the
pinned source tree. [ONVIF release](https://github.com/onvif/specs/tree/68ee1b540a40f848c9599eba2c55b87547c588d6)

| Service specification | Version | WSDL under `wsdl/` |
| --- | --- | --- |
| Core, Device Management and Events | 26.06 | `ver10/device/wsdl/devicemgmt.wsdl`, `ver10/events/wsdl/event.wsdl` |
| Media | 24.12 | `ver10/media/wsdl/media.wsdl` |
| Media2 | 26.06 | `ver20/media/wsdl/media.wsdl` |
| Device I/O | 22.06 | `ver10/deviceio.wsdl` |
| Imaging | 22.06 | `ver20/imaging/wsdl/imaging.wsdl` |
| PTZ | 26.06 | `ver20/ptz/wsdl/ptz.wsdl` |
| Analytics | 26.06 | `ver20/analytics/wsdl/analytics.wsdl` |
| Recording Control | 26.06 | `ver10/recording.wsdl` |
| Recording Search | 26.06 | `ver10/search.wsdl` |
| Replay Control | 21.12 | `ver10/replay.wsdl` |
| Streaming | 26.06 | RTSP/RTP requirements in `doc/Streaming.xml` |

The common types come from `wsdl/ver10/schema/onvif.xsd` and its included
`common.xsd`. Event and metadata messages use the WSDL's WS-Addressing and
WS-Notification imports and `wsdl/ver10/schema/metadatastream.xsd`. The latter
includes `common.xsd` and imports `humanface.xsd` and `humanbody.xsd` from
`wsdl/ver20/analytics/`, which also contains `rules.xsd`. Resolve the external
SOAP, XML MIME, XOP and WS-Notification imports when loading the schemas.

## Operation checks

- Media `GetProfile` and `GetCompatible*Configurations` require a
  `ProfileToken`. Media2 `GetProfiles` uses optional `Token` and `Type`
  selectors, while Media2 `Get*Configurations` uses optional
  `ConfigurationToken` and `ProfileToken`. With neither selector the
  configuration query lists its entire catalog. Profile queries use
  `READ_MEDIA` and report `NoProfile` for an unknown selector. Attachments
  use `ACTUATE` and distinguish unknown configurations (`NoConfig`) from
  incompatible associations (`Receiver/Action/ConfigurationConflict`).
- Media `GetStreamUri` takes `StreamSetup` and `ProfileToken` and wraps
  the URI in `MediaUri`. Media2 takes `Protocol` and `ProfileToken` and
  returns `Uri` directly. Snapshot requests use `ProfileToken` and the
  same service-specific response shapes. These operations use `READ_MEDIA`.
- Media and Device I/O source lists use `READ_MEDIA`. Device I/O returns
  repeated `Token` elements for `GetVideoSources` and `GetAudioSources`.
  Imaging `GetImagingSettings` uses `READ_MEDIA` and takes a physical
  `VideoSourceToken`, with `NoSource` for unknown sources and
  `NoImagingForSource` for sources without imaging support. PTZ
  `GetCompatibleConfigurations` uses `READ_MEDIA` and a media profile
  token. Its response contains zero or more `PTZConfiguration` elements.
- Analytics `GetSupportedRules`, `GetRules`,
  `GetSupportedAnalyticsModules` and `GetAnalyticsModules` select a
  configuration with `ConfigurationToken`. These queries use `READ_MEDIA`.
  `CreateRules` uses `ACTUATE` and distinguishes `NoConfig`,
  `InvalidRule`, `RuleAlreadyExistent` and `ConfigurationConflict`.
- Event `SetSynchronizationPoint` acts on the subscription endpoint and has
  an empty body. `PullMessages` returns `CurrentTime` and `TerminationTime`
  before its notification elements. Media and Media2 `SetSynchronizationPoint`
  instead select a media profile by token. Use the advertised event topic and
  its message description to choose source fields for each notification.
- Recording Control list and state queries use `READ_MEDIA`.
  `SetRecordingJobMode` uses `ACTUATE`, takes `JobToken` then `Mode`, and
  distinguishes `NoRecordingJob` from `BadMode`. A local media profile is
  identified by a source reference with type
  `http://www.onvif.org/ver10/schema/Profile` and the profile token. The
  recording source descriptor may also carry the physical `VideoSourceToken`.
- `FindRecordings` takes `Scope`, optional `MaxMatches`, and
  `KeepAliveTime`. `GetRecordingSearchResults` takes a `SearchToken` and
  optional result counts and wait time. An unknown search session produces
  `InvalidToken`. Replay `GetReplayUri` takes `StreamSetup` and
  `RecordingToken`, with `NoRecording` for unknown archive identities and
  `InvalidStreamSetup` for unsupported transport selections. Search and replay
  use `READ_MEDIA` for these operations.
