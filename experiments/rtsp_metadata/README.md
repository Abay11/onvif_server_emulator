# Stage 1: ONVIF RTSP metadata feasibility

This isolated experiment proves the GStreamer transport path before changing the
emulator's event handling or advertising metadata support. It uses Python's
GObject bindings to exercise the same `GstRTSPMediaFactory` and `appsrc` APIs
available to the C++ server. Production integration remains a later stage.

## Reproduce in the documented Linux development environment

From the repository root, check the Docker context as described in
[the WSL2 guide](../../docs/WSL2.md), then build the optional development target:

```bash
docker build -f docker/Dockerfile --target metadata-dev -t osrv-metadata-dev:stage1 .
```

Stage 3 reuses this plugin in the final application `runtime` image and adds an
opt-in live transport; see [RTSP metadata](../../docs/RTSP_METADATA.md).
`metadata-plugin-build` compiles only `gst-plugin-onvif` using upstream's
Cargo lock file. `metadata-runtime` installs the result on the application's base
image, without a compiler or Rust runtime. `metadata-dev` adds inspection and
validation tools. All targets use the same Debian/GStreamer libraries.

Prepare the reference schemas once, with network access, in a dedicated volume:

```bash
docker run --rm \
  -v "$(pwd)":/workspace:ro \
  -v osrv-metadata-contract:/contract \
  osrv-metadata-dev:stage1 \
  python3 experiments/rtsp_metadata/contract.py --prepare /contract
```

Then run the schema compilation and complete network experiment offline:

```bash
docker run --rm --network none \
  -v "$(pwd)":/workspace:ro \
  -v osrv-metadata-contract:/contract:ro \
  osrv-metadata-dev:stage1 \
  python3 experiments/rtsp_metadata/contract.py /contract

docker run --rm --network none \
  -v "$(pwd)":/workspace:ro \
  -v osrv-metadata-contract:/contract:ro \
  osrv-metadata-dev:stage1 \
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
docker build -f docker/Dockerfile --target metadata-runtime -t osrv-metadata-runtime:stage1 .
docker run --rm --network none \
  -v "$(pwd)":/workspace:ro -v osrv-metadata-probe:/probe \
  osrv-metadata-dev:stage1 bash -lc \
  'cc experiments/rtsp_metadata/runtime_probe.c -o /probe/probe $(pkg-config --cflags --libs gstreamer-1.0)'
docker run --rm --network none -v osrv-metadata-probe:/probe:ro \
  osrv-metadata-runtime:stage1 /probe/probe
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
or VMS acceptance. RTCP synchronization accuracy and loss recovery remain later
integration checks. No official ONVIF conformance claim follows from these tests.

## Pinned contract

Use the ONVIF **24.12 release set**, commit
`d7f3684501c4a6d58f879e72e1e3322c120f95e6`, for the initial metadata work. This
keeps Streaming, Core, Media, Media2, WSDL and schema references in one released
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

## Contract decisions for later stages

The following are independently written implementation requirements derived from
the pinned references; Stage 1 does not implement the corresponding SOAP API.

- The XML root is `tt:MetadataStream` with namespace
  `http://www.onvif.org/ver10/schema`. Use the XSD spelling; the narrative RTP
  section contains an inconsistent capital D. Notifications appear under Event
  as WS-Notification NotificationMessage elements, without a SOAP envelope.
- Use uncompressed UTF-8 XML and a dynamic RTP payload type. Close each input
  document and set the RTP marker only on its last fragment. The upstream
  payloader accepts one complete document per buffer with caps
  `application/x-onvif-metadata`; it does not validate the entire ONVIF schema.
- Keep UTC event occurrence times inside XML and use pipeline running time for
  buffer PTS. RTCP provides the media clock relationship.
- MetadataConfiguration Events absent means no event delivery; Events present
  without Filter selects all events. A Filter restricts the selection. Support
  topic filtering and advertise content-filter capabilities only when implemented.
- Property snapshots contain Initialized operations. Transient events must not
  be synthesized as properties. Media/Media2 `SetSynchronizationPoint` must
  update the streams associated with the requested profile; PullPoint retains
  its separate subscription-scoped `SetSynchronizationPoint`.
- Media requires get/list/options/set metadata operations, compatible configuration
  lookup, and AddMetadataConfiguration/RemoveMetadataConfiguration. Media2 uses
  its own list/options/set operations and AddConfiguration/RemoveConfiguration;
  do not invent Media1 operation names in Media2.
- Profile responses, configuration use counts, GetStreamUri and SDP must agree.
  Different metadata settings must not silently share one incompatible factory.
  Metadata changes may affect live streams immediately, but clients must obtain
  a new URI and restart streams for guaranteed application of changes. Honor the
  service-specific persistence rules.
- Validate token associations and configuration values. Media faults include
  InvalidArgVal/NoProfile, InvalidArgVal/NoConfig, InvalidArgVal/ConfigModify and
  Action/ConfigurationConflict as specified per operation. Media2 and filter
  faults must follow their own operation definitions, not a shared generic fault.
- Do not advertise compression, analytics scene output, PTZ metadata, multicast,
  or other optional features solely because the container can carry their XML.
- Media2's ConfigurationsSupported must include Metadata once that configuration
  type is operational. MaxContentFilterSize signals content-filter support and
  must not be set speculatively. Keep HTTP PullPoint capabilities independent of
  the RTP producer, and do not repurpose analytics SupportedMetadata as a generic
  event-transport capability.

## Plugin provenance and notices

The upstream plugin is GStreamer gst-plugins-rs **0.13.7**, annotated tag resolving
to commit `cdec77cad2d9b7348d800507bc6eeec5039717eb`:
[source](https://github.com/GStreamer/gst-plugins-rs/tree/cdec77cad2d9b7348d800507bc6eeec5039717eb/net/onvif).
This release declares Rust 1.71+ and GStreamer 1.20+ for the ONVIF crate. The
Debian Trixie build uses Rust 1.85 and GStreamer 1.26.2. Future upgrades must retain
a compatible lock file/toolchain combination.

The ONVIF plugin is MPL-2.0. The optional images include the unmodified upstream
source archive (with Cargo.lock), plugin license and collected dependency notices
under `/usr/local/share/osrv-metadata/`. The Cargo lock file identifies dependency
versions and source revisions. The `.so` is dynamically loaded by GStreamer; no
Rust compiler is needed at runtime. Preserve these materials when deriving an
image that distributes this plugin.

ONVIF schemas remain outside this repository/image and retain their file-specific
notices. External schemas also remain in the validation cache with their original
bytes/notices. Check the applicable ONVIF, OASIS and W3C terms before separately
redistributing that cache. Root repository licenses do not override per-file terms.

## Results

Validated with the local Debian 13 Docker Engine, context `default`, socket
`unix:///var/run/docker.sock`. The original development image had GStreamer
1.26.2, no metadata payloader and no matching package in its configured package
search. The pinned upstream build succeeded with `cargo build --locked --release
--jobs 2 -p gst-plugin-onvif`.

The following commands actually ran successfully:

- Docker builds of both optional `metadata-dev` and `metadata-runtime` targets.
- `gst-inspect-1.0 appsrc` and `gst-inspect-1.0 rtponvifmetadatapay` in the image.
- `contract.py --prepare /contract` using a fresh named Docker volume, followed
  by offline schema compilation with all imported bytes pinned and cached.
- `check.py --contract /contract` with networking disabled except loopback.
  Both `/metadata` and `/metadata-only` passed TCP and UDP delivery, quiet
  initialization, fragmented event reconstruction, XSD validation and late join.
  DESCRIBE took approximately 1–18 ms on this machine. Video-only track SETUP
  and reception passed over both transports. The process exited on SIGTERM.
- The compiled probe loaded `rsonvif 0.13.7-cdec77cad` in the runtime-only image.
- Python bytecode compilation and `git diff --check`.

The upstream `appsrc` plus metadata payloader path is feasible; a custom payloader
is unnecessary for the first implementation. Production event/configuration
integration remains Stage 2 onward. No application C++ sources changed, and no
application build, CTest or emulator `network_smoke.py` run was performed: that
script expects the full SOAP/discovery server, while this experiment deliberately
runs only the isolated RTSP prototype. Windows/LAN/VMS interoperability and the
other integration limits listed above remain unvalidated.
