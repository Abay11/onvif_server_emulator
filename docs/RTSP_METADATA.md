# Live RTSP event metadata (Stage 3)

The GStreamer RTSP server can deliver the shared event source as an ONVIF XML
metadata RTP track alongside live video/audio. PullPoint subscribers and RTSP
clients receive independent copies of the same generated notifications.

## Enable the transport

In a configuration copy, set the optional `common.config` setting:

```json
"rtsp_metadata": { "enabled": true }
```

Restart the server. The default is false for compatibility with configurations
that have not enabled this transport. Enabled startup requires the
`rtponvifmetadatapay` GStreamer plugin; a missing plugin produces a startup error.
Normal Docker runtime builds include the pinned plugin, source and notices from
Stage 1. For source builds/tests use the `metadata-dev` target:

```bash
docker build -f docker/Dockerfile --target metadata-dev -t osrv-metadata-dev:stage3 .
```

Follow [BUILD.md](../BUILD.md) for repository/build mounts, CMake and network
settings. The ordinary `dev` target still supports builds and runs with metadata
disabled; it does not include the optional payloader.

The metadata track is present on `/Live&HighStream` and `/Live&LowStream`, including
the high stream's file-input variant. Use the existing RTSP URI and discover its
application track through DESCRIBE. No separate metadata URI is required. Replay
at `/Recording0` is unchanged and does not contain historical event metadata.

### Intentional Stage 3 simulation scope

This switch enables event metadata on both fixed live mounts. It is a transport
integration setting, independent of the currently empty profile Metadata slots.
Until Stage 4 implements metadata configurations and profile associations, it is
an intentional control-plane simulation deviation: SOAP profile responses do not
configure or advertise these additional tracks. Do not interpret this setting as
full ONVIF Media/Media2 metadata-configuration support. Metadata-capable raw RTSP
clients can select the application track; a VMS that requires SOAP metadata
configuration first may need Stage 4 before it uses the track.

## Transport and synchronization

- High stream uses pay0 for video, pay1 for audio and pay2 for metadata. Low uses
  pay0 for video and pay1 for metadata. The metadata payload type is 107; SDP uses
  application media, VND.ONVIF.METADATA and a 90 kHz clock.
- A client may SETUP metadata alone, video/audio alone, or the aggregate tracks.
  TCP interleaving and unicast UDP use GStreamer's existing transport and bounded
  RTP/RTCP port pool.
- Each prepared live media instance has an independent shared-source subscription.
  The bridge polls its bounded queue on the RTSP server's private GLib context.
  GStreamer appsrc is nonblocking; its queue is gated at 128 KiB before another
  bounded batch of up to sixteen notifications is accepted. One larger batch can
  exceed that gate, but publication never waits on RTSP client I/O. Source queue
  limits and dropped-message diagnostics are described in
  [shared event delivery](EVENT_DELIVERY.md).
- Each buffer contains one complete UTF-8 `tt:MetadataStream` document. GStreamer's
  ONVIF payloader fragments it into RTP packets with a 1200-byte MTU and marks
  only the final fragment. UTC occurrence timestamps inside notifications are
  preserved; RTP PTS uses the pipeline clock minus its base time.
  Namespace bindings from `event.config` are captured through the shared source,
  including custom topic/payload prefixes; they are not limited to default ONVIF
  prefixes.
- A schema-valid empty document primes media preparation when there are no
  notifications. Empty documents are also sent at approximately one-second
  intervals during silence, subject to backpressure. They are not synthetic events.
- PLAY for a session with its metadata track selected requests fresh Initialized
  property states. Shared media send that snapshot to all current consumers of
  the stream; this supplies late clients without restarting generators. PAUSE
  and resume retain the media lifecycle and request fresh state on resumed PLAY.
  Property snapshots do not invent initialization for transient notifications.
  The SOAP media synchronization operation remains Stage 4 work.
- Metadata RTCP reserves 4000 bits/s for senders and 12000 bits/s for receivers.
  This avoids very long sender-report intervals caused by automatic bandwidth
  estimation on sparse XML traffic. The normal five-second minimum RTCP interval
  remains in effect; actual intervals are randomized by GStreamer.

No analytics scene/frame description, compression, multicast RTP, media recording
or historical metadata replay is introduced by this stage.

## Lifetime and cleanup

Factory/media callbacks own shared bridge state rather than the RTSP Server
object. Unprepare stops the bridge, disconnects appsrc handlers, destroys its
timer and closes its event subscription. Last-client teardown permits a fresh
media instance on reconnect. Active shared streams survive another client's
TEARDOWN. Expired RTSP sessions are cleaned every five seconds; abrupt client
disconnect can retain a session until its negotiated timeout.

RTSP server destruction stops the listener, metadata bridges, client connections
and session pool, then quits and joins its private GLib loop. It handles both
active prepared/playing media and stopping immediately after starting the worker.
This is subsystem cleanup, not a new complete application signal/restart model.
The Event service's remaining global state and optional detached HTTP listener
still limit full application restart/multi-server behavior.

## Contract and validation

The implementation uses the pinned ONVIF 24.12 set at commit
`d7f3684501c4a6d58f879e72e1e3322c120f95e6`: Streaming metadata RTP and metadata
RTSP session sections, Core property synchronization, `metadatastream.xsd`,
`common.xsd`, `onvif.xsd` and imported definitions. See
[the Stage 1 references/cache instructions](../experiments/rtsp_metadata/README.md)
and [the immutable release](https://github.com/onvif/specs/tree/d7f3684501c4a6d58f879e72e1e3322c120f95e6).
GStreamer API references are [appsrc](https://gstreamer.freedesktop.org/documentation/app/appsrc.html),
[RTSP client](https://gstreamer.freedesktop.org/documentation/gst-rtsp-server/rtsp-client.html)
and [metadata payloader](https://gstreamer.freedesktop.org/documentation/rsonvif/rtponvifmetadatapay.html).
These are implementation references, not an official conformance claim.

Build and run CTest in the metadata-enabled development image:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DENABLE_UNIT_TESTS=ON
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
```

The RTSP lifecycle test covers disabled/enabled SDP, quiet preparation, metadata
playback, active-client shutdown and immediate start/stop. Its enabled case reports
a skip in an environment without the metadata plugin; run it in `metadata-dev`
for positive coverage.

Against a source-built server with this switch enabled, default event settings
and disabled authentication, run:

```bash
python3 unit_tests/network_smoke.py
python3 unit_tests/event_delivery_smoke.py
python3 unit_tests/rtsp_metadata_smoke.py --contract /contract
```

`/contract` is the Stage 1 pinned schema cache mounted into the test container.
Omit that argument for a standard-library-only receiver without XSD validation.
All scripts accept explicit host/port options. The metadata script checks two
RTSP consumers plus two PullPoints, identical live events, late property state,
marker-delimited fragmented XML, 90 kHz SDP, monotonic timestamps, RTCP sender
reports, pause/resume, both live mounts, metadata-only and aggregate selection,
audio/video-only selection, and reconnect after last-client teardown. The same
script can exercise a configuration copy with a sufficiently long video/audio
file input. Use temporary configs and the environment-specific network settings
from BUILD.md/WSL2.md. Windows/LAN/VMS acceptance remains a separate check.

### Recorded Stage 3 results

Validation ran on the local Debian 13 Docker Engine (`default` context), using
GStreamer 1.26.2 and the pinned ONVIF plugin 0.13.7:

- CMake built the affected library, application and tests with two parallel jobs;
  the full CTest suite passed, including the enabled lifecycle/active playback case.
- All shipped JSON configurations and twelve configuration-schema tests passed.
- The existing network smoke script, independent PullPoint script and new metadata
  script passed against source-built live pipelines. The metadata receiver used
  the offline pinned schemas for each reconstructed XML document.
- The same checks passed with file input and in the freshly built standard
  `osrv-metadata:stage3` runtime image using a generated I420/H.264 plus AAC MP4.
  The runtime server log contained no ERROR, GStreamer CRITICAL or encoder errors.
- With metadata disabled, the original development image (without the payloader)
  passed its network/PullPoint checks. Enabling metadata there failed cleanly with
  the missing-plugin message. HTTP and RTSP bind failures also exited cleanly.
- `git diff --check`, Python syntax and local documentation links passed.
- A custom topic namespace configured in `event.config` was verified in actual
  received RTP XML, alongside its source-context regression test.

All network runs used isolated container namespaces and configuration copies;
no host listening ports or source configurations were changed by the tests.
The temporary runtime verification container was stopped and removed. Long-running
backpressure stress, Windows/LAN/VMS behavior, and the Stage 4 configuration
contract remain outside this validation.
