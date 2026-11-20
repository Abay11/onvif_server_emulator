# ONVIF RTSP metadata transport checks

This isolated harness checks the GStreamer transport path independently of the
emulator. It uses Python's GObject bindings to exercise the same
`GstRTSPMediaFactory` and `appsrc` APIs as the C++ server. For application
configuration and integration checks, see [RTSP metadata](../../docs/RTSP_METADATA.md).

## Reproduce in the documented Linux development environment

From the repository root, check the Docker context as described in
[the WSL2 guide](../../docs/WSL2.md), then build the development image:

```bash
docker build -f docker/Dockerfile --target dev -t osrv-dev:latest .
```

The final application `runtime` image reuses this plugin for live metadata,
which is enabled by default; see [RTSP metadata](../../docs/RTSP_METADATA.md).
`metadata-plugin-build` compiles only `gst-plugin-onvif` using upstream's
Cargo lock file. `metadata-runtime` installs the result on the application's base
image, without a compiler or Rust runtime. `dev` adds inspection and
validation tools. All targets use the same Debian/GStreamer libraries.

Prepare the reference schemas once, with network access, in a dedicated volume:

```bash
docker run --rm \
  -v "$(pwd)":/workspace:ro \
  -v osrv-metadata-contract:/contract \
  osrv-dev:latest \
  python3 experiments/rtsp_metadata/contract.py --prepare /contract
```

Then run the schema compilation and complete network experiment offline:

```bash
docker run --rm --network none \
  -v "$(pwd)":/workspace:ro \
  -v osrv-metadata-contract:/contract:ro \
  osrv-dev:latest \
  python3 experiments/rtsp_metadata/contract.py /contract

docker run --rm --network none \
  -v "$(pwd)":/workspace:ro \
  -v osrv-metadata-contract:/contract:ro \
  osrv-dev:latest \
  python3 experiments/rtsp_metadata/check.py --contract /contract
```

The check starts and stops its own server. All traffic uses loopback inside the
container; no host ports, host networking, emulator configs or LAN firewall changes
are needed. `--network none` retains container loopback. The default internal
RTSP port is 18554. Use `--port` to select another unused port.

For manual inspection, run `server.py` in the same container/network namespace
as the client. The two paths are `/metadata` (H.264 plus metadata) and
`/metadata-only` (metadata alone). Only loopback connections are accepted.

To check the plugin in the smaller runtime image independently of the development
packages, compile the tiny probe into a separate volume and run it there:

```bash
docker build -f docker/Dockerfile --target metadata-runtime -t osrv-metadata-runtime:latest .
docker run --rm --network none \
  -v "$(pwd)":/workspace:ro -v osrv-metadata-probe:/probe \
  osrv-dev:latest bash -lc \
  'cc experiments/rtsp_metadata/runtime_probe.c -o /probe/probe $(pkg-config --cflags --libs gstreamer-1.0)'
docker run --rm --network none -v osrv-metadata-probe:/probe:ro \
  osrv-metadata-runtime:latest /probe/probe
```

## What is tested

- DESCRIBE returns a metadata application track, ONVIF encoding name, dynamic
  payload type and 90 kHz clock before the first notification is generated.
- The source sends schema-valid empty documents during the initial three seconds
  of event silence. This allows preparation and negotiation without fabricated
  events. Silence is per media instance; it is not an ONVIF timing requirement.
- SETUP and PLAY deliver actual RTP over interleaved TCP and unicast UDP.
- The receiver reconstructs a complete 16 KB event, checks contiguous packet
  sequence numbers, consistent per-document RTP timestamps, the 1200-byte MTU
  and marker-delimited XML boundaries, and validates the received XML against
  the pinned metadata XSD and its imports.
- A second client joins a shared live factory while the first stays connected;
  both continue receiving complete documents. The receiver can discard the
  initial incomplete document when it joins between fragment boundaries.
- Both metadata in an aggregate video session and a metadata-only factory work.
  A client that SETUPs only the video track also receives video.
- TEARDOWN and SIGTERM allow the prototype to exit. Timer/connection cleanup is
  scoped to media instances.

The extra Sequence and Padding fields are test data for observing fragmentation;
this is not an advertised device topic definition. The prototype does not
implement PullPoint, metadata configuration SOAP operations, filtering, property
synchronization, analytics scene descriptions, authentication, replay, multicast
or VMS acceptance. RTCP synchronization accuracy and loss recovery are outside
this isolated harness; use the application integration checks for broader
coverage. No official ONVIF conformance claim follows from these tests.

## Pinned contract

Use the ONVIF **24.12 release set**, commit
`d7f3684501c4a6d58f879e72e1e3322c120f95e6`, for metadata transport and configuration.
This keeps Streaming, Core, Media, Media2, WSDL and schema references in one released
set instead of combining independently changing website links.

All paths below are relative to
[that immutable release](https://github.com/onvif/specs/tree/d7f3684501c4a6d58f879e72e1e3322c120f95e6):

| Contract | Reference |
| --- | --- |
| RTP framing, XML container, SDP | `doc/Streaming.xml`, metadata RTP and RTSP metadata-session sections |
| Event filtering and property synchronization | `doc/Core.xml`, Event Handling sections |
| Media configuration and profile attachment | `doc/Media.xml` |
| Media2 configuration and profile attachment | `doc/Media2.xml` |
| Media operations/actions | `wsdl/ver10/media/wsdl/media.wsdl` |
| Media2 operations/actions | `wsdl/ver20/media/wsdl/media.wsdl` |
| PullPoint operations/actions | `wsdl/ver10/events/wsdl/event.wsdl` |
| Metadata container and frame/event types | `wsdl/ver10/schema/metadatastream.xsd` |
| Configuration and common message types | `wsdl/ver10/schema/onvif.xsd`, `common.xsd` |

Local ONVIF imports resolve within this pinned repository. External schema/WSDL
resources are fetched unmodified using `imports.lock.json`, which records the
canonical URL, HTTPS download URL and SHA-256 of each resource. A custom XML
resolver preserves canonical base URLs and supplies cached bytes without editing
third-party schemas. Unexpected remote dependencies and hash changes fail closed.
The schema check requires the pinned commit and an unchanged tracked WSDL tree.

## Application contract

The application uses `appsrc` with caps `application/x-onvif-metadata` and one
complete uncompressed UTF-8 `tt:MetadataStream` document per input buffer. The
payloader handles RTP fragmentation; it does not validate XML against the ONVIF
schemas. UTC occurrence times stay in XML and pipeline running time supplies PTS.
GStreamer provides SDP and RTP/RTCP transport.

For implemented SOAP operations, event filters, synchronization, profile
associations, scene selection and fault behavior, see
[RTSP metadata](../../docs/RTSP_METADATA.md) and
[shared event delivery](../../docs/EVENT_DELIVERY.md). The isolated harness does
not replace the source-built application's CTest and network checks.

## Plugin provenance and notices

The upstream plugin is GStreamer gst-plugins-rs **0.13.7**, annotated tag resolving
to commit `cdec77cad2d9b7348d800507bc6eeec5039717eb`:
[source](https://github.com/GStreamer/gst-plugins-rs/tree/cdec77cad2d9b7348d800507bc6eeec5039717eb/net/onvif).
This release declares Rust 1.71+ and GStreamer 1.20+ for the ONVIF crate. The
Debian Trixie build uses Rust 1.85 and GStreamer 1.26.2. Future upgrades must retain
a compatible lock file/toolchain combination.

The ONVIF plugin is MPL-2.0. The development and runtime images include the unmodified upstream
source archive (with Cargo.lock), plugin license and collected dependency notices
under `/usr/local/share/osrv-metadata/`. The Cargo lock file identifies dependency
versions and source revisions. The `.so` is dynamically loaded by GStreamer; no
Rust compiler is needed at runtime. Preserve these materials when deriving an
image that distributes this plugin.

ONVIF schemas remain outside this repository/image and retain their file-specific
notices. External schemas also remain in the validation cache with their original
bytes/notices. Check the applicable ONVIF, OASIS and W3C terms before separately
redistributing that cache. Root repository licenses do not override per-file terms.
