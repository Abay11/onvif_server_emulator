# NVR channel topology

`server_configs/channels.config` holds the saved channel topology. The
`channels` array contains between one and 32 entries. Each entry contains
a stable channel `token`, a display `name`, a physical `videoSource` with
its own token, width, height and frame rate, and a `media` catalog.

Each catalog follows the structure of the media profiles schema. A stream
is defined by an entry in `MediaProfiles` referencing its video source and
encoder configurations by token. The source configuration's `SourceToken`
references the physical video source within the channel. Audio source
configurations similarly reference that channel's physical audio sources.
Leave the audio catalogs empty and omit the audio associations for a
video-only channel. Profile and configuration tokens must be unique within
their respective catalogs across the topology. Keep existing tokens when
renaming a channel.

The configuration API validates the entire document and rejects saves with
a stale revision. A successful save stages the document for restart.

The server reads this document at startup and combines the channel catalogs into
one active media catalog. Media and Media2 use the same configuration identities.
The two video encoder descriptions must agree on the stream settings they share.
The **Configuration** catalog and active profile table group entries by
running channel. Use **NVR Channels** in the sidebar to change the saved
channel count, clone channels from a template, and edit components or streams
before restart.

The bundled channel and newly cloned configurations have display names such as
`CH01 - Main stream` and `CH02 - Video encoder 1`. You can edit the display
names in **Configuration**. New cloned configurations also get a token with
the type, channel number, and a unique suffix, such as
`VideoSourceConfig_CH02_1_…`. Names can change while the tokens assigned to
existing profiles and configurations stay stable.

Media profile changes made over SOAP persist to the topology. A new profile
without any attached configuration is stored in `unassignedProfiles`. Its first
attachment selects the owner channel. Removing the attachment leaves the
ownership in place. When a saved draft and a live mutation change the same
resource, the live mutation returns a configuration conflict. A successful
mutation invalidates editor revisions based on older file contents.

See the [pinned ONVIF contract](NVR_PROTOCOL.md) for the affected operations and
schemas.

## Live delivery

Media and Media2 return the same stable path under `/onvif/profile/`, keyed
by the profile token. RTSP supports UDP and TCP and can use the configured HTTP
or HTTPS tunnel transport. Supported H.264 settings include Baseline, Main
and High profiles, even dimensions from 16 to 4096 pixels, integral frame rates
from 1 to 120 fps, and configurable bitrate and GOP. Pipeline suitability at
the upper bounds depends on the host. The primary bundled stream is 1280×720,
and the secondary stream is 640×480. Existing direct RTSP aliases remain
available for clients configured by hand.

Matching synthetic video settings share an on-demand H.264 encoder across
different RTSP mounts. Each mount handles its own audio and metadata tracks.
The producer stops after its last media instance disconnects. Consumers that
fill their encoded video queue wait for a keyframe to resume. File input is
global through `fileStreaming` and uses an independent decoder and encoder
per active profile. Recording replay uses the saved track configuration.

## Detectors

Choose a video source in **Detectors** to edit its rules and timer settings.
`Analytics` entries may specify `supportedRules`, `supportedSceneModules`,
and `sceneModules`. Omitted lists use the built-in inventory. Empty lists
select an empty inventory. Each analytics configuration supports up to four
rules. Per-source timing and enablement overrides are stored in `event.config`
under `ChannelDetectors`. Motion alarms, cell motion, and sound detection
use the selected source's component inventory and each has independent
automatic and manual controls. Digital inputs remain device-wide.

Use a PullPoint subscription or the journal to verify the source and rule
associated with an event. An RTSP profile's metadata uses its own video source
and analytics configuration. Sharing H.264 between profiles preserves this
routing.

## Recording

In **Recording Storage**, select a stream and start simulation. The first start
creates the channel's recording and job. The job retains its selected profile
and tokens. Stopping closes its current interval. A later start opens a new
interval. A graceful restart closes active intervals and reopens them for
jobs whose source profile is still available. Removing a channel preserves
its recording history. An unclean process exit conservatively closes an open
interval at its last durable start timestamp on recovery.

The archive is stored as `recording_archive.json` in the configuration
directory. Its intervals and track descriptors are metadata. The emulator
generates synthetic replay media on demand. The Recording Control service
exposes recording and track configuration, job listing, configuration and state,
and `SetRecordingJobMode`. Search supports recording summaries, information,
and token-scoped paginated recording sessions. Recording information XPath,
event and media-attribute searches, dynamic SOAP recording and track creation,
exports, and retention editing are outside the simulation. Replay supports
forward playback within one contiguous archived interval per PLAY request.
A clock range spanning a gap ends at the first interval boundary. Request the
next interval separately. Metadata replay produces synthetic empty metadata
frames for the selected track. Replay uses real-time pacing. Its RTP
timestamp extension carries the synthesized archive time. Gap and termination
flags, accelerated delivery, and frame-type selection are outside the
simulation.

The recording path uses the advertised RTSP or tunnel endpoint. See the
[GStreamer RTSP range API](https://gstreamer.freedesktop.org/documentation/rtsplib/gstrtsptransport.html#gst_rtsp_range_get_times).

## Validation in WSL

The source build ran in the local Docker development image. An isolated
container network allowed both unicast and multicast WS-Discovery on
UDP 3702. The host-network run timed out waiting for a ProbeMatch.

The HTTP and HTTPS tunnel matrix delivered both live and archived RTP with
video, audio, and optional metadata. The replay checks used an absolute UTC
range and the `onvif-replay` feature tag. Representative SOAP responses,
faults, and event and scene metadata were checked against the pinned schemas.

The 32-channel scenario exposed 64 profiles. Thirty-two concurrent UDP
video sessions shared one encoder and reported 64 RTP/RTCP ports
(`50000–50063`). That run used 84 user CPU ticks over 8.98 seconds, including
connection setup. It also verified selective recording, paginated search,
playback in both archived intervals, and rejection of a gap-only range.

In a separate two-client TCP decode test, two 1280×720 streams used one
encoder and 0.86 server CPU seconds over 7.20 wall seconds. Changing one
GOP to force independent encoding used two encoders and 1.38 server CPU
seconds over 7.20 wall seconds. The secondary stream also decoded at
640×480 with its bundled settings. A stalled TCP reader did not interrupt
the other mount. These measurements describe the tested workload in the
development container. Windows-native discovery, LAN operation, and VMS
interoperability still require deployment checks.
