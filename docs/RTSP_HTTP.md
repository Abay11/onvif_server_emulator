# RTSP and RTP over HTTP/HTTPS

The main HTTP and HTTPS listeners also accept RTSP-over-HTTP tunnels. With the
bundled configuration these use TCP 8080 and 8443. There are no additional ports
or settings: `http.enabled` and `https.enabled` control their availability.
HTTPS uses the same TLS 1.2+ context and certificate as SOAP and WebUI; see
[HTTPS certificates and trust](HTTPS.md#certificates).

The client opens two fresh HTTP connections, GET and POST, with the same
`x-sessioncookie`. The content type is `application/x-rtsp-tunnelled`. Commands
and interleaved RTP/RTCP sent in POST are base64 encoded; GET carries the RTSP
responses and interleaved RTP/RTCP. Both connections must use the same transport
and endpoint. Use a unique cookie for each session. This is continuous streaming,
so POST does not wait for its declared Content-Length before commands are handled.
Live streams, profile-specific streams (including metadata tracks), and the
synthetic `/Recording0` replay mount use the existing RTSP factories.

## ONVIF URI selection

Send the following transport in a normal GetStreamUri request, followed by the
profile token:

- Media (`ver10`): `StreamSetup/Stream=RTP-Unicast` and
  `StreamSetup/Transport/Protocol=HTTP` (Stream and Transport children use the
  `http://www.onvif.org/ver10/schema` namespace).
- Media2 (`ver20`): `Protocol=RtspOverHttp`.
- Replay Control (`ver10`): GetReplayUri with the same StreamSetup as Media,
  followed by a recording token from Recording Search (bundled token: `Recording_0`).
  Unknown tokens return InvalidArgVal/NoRecording. As before, all configured
  recordings map to the single synthetic replay mount rather than stored footage.

A SOAP request received over HTTP returns an `http://` stream URI on the
advertised HTTP port. A request over HTTPS returns an `https://` stream URI on
the advertised HTTPS port. This follows the web service transport used by that
request, independently of `advertised.web_scheme`. Bind and advertised ports
remain distinct; configure forwarding for the advertised port when using NAT.
`RTSP`/`UDP` (Media) and `RTSP`/`RtspUnicast` (Media2) continue to return the
regular `rtsp://` endpoint. Unsupported transports return
Sender/InvalidArgVal/InvalidStreamSetup; `HTTPS` is not an ONVIF Protocol value.
Media's deprecated `TCP` value retains its previous RTSP behavior. Multicast,
SRTP and the deprecated nested Tunnel option remain unsupported.

A returned HTTP URL is an RTSP tunnel endpoint, not a video file or HLS playlist.
Clients must support this transport explicitly. For example, GStreamer's rtspsrc
uses `rtsph://` and `rtspsh://` as client-side selectors for HTTP and HTTPS:

```bash
gst-launch-1.0 rtspsrc location='rtsph://127.0.0.1:8080/Live&HighStream' ! fakesink
gst-launch-1.0 rtspsrc location='rtspsh://127.0.0.1:8443/Live&HighStream' tls-validation-flags=0 ! fakesink
```

Disabling certificate validation above is only for the generated development
certificate. Configure a trusted certificate for normal client verification.
Existing RTSP authentication behavior applies: the RTSP server currently has no
credential enforcement, including inside tunnels. SOAP Digest/WS-Security and
WebUI sign-in do not protect media connections. HTTPS encrypts the media transport.

## Implementation and validation

The shared HTTP stream adapter inspects the first header before ordinary HTTP
body buffering. A bounded asynchronous bridge forwards the original header and
any bytes read ahead to the existing GStreamer RTSP listener on the configured
local bind address/RTSP port. GStreamer pairs the two connections, decodes POST
base64, and handles RTSP sessions and media. HTTPS terminates in the existing
web listener; the internal RTSP hop remains local to the server. The bridge
omits GStreamer's optional x-server-ip-address response header so remote clients
retain the original web endpoint instead of reconnecting to the backend address.
RTSP journal entries see the internal connection peer. Backpressure
limits each pump to 16 KiB. Disconnect/error closes both ends of that connection;
main listener shutdown cancels tracked tunnels. New libraries and codecs are not
introduced. The HTTP listener must be able to connect to its local RTSP listener.

Run in the [documented development container](../BUILD.md#reusable-development-environment):

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DENABLE_UNIT_TESTS=ON
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
python3 unit_tests/rtsp_http_smoke.py --binary build/main --network-smoke --gstreamer
python3 unit_tests/https_smoke.py --binary build/main
```

The tunnel matrix uses disposable configuration copies with SOAP authentication
disabled. It checks HTTP/HTTPS simultaneously, each listener alone, metadata
on/off, Media/Media2 URI selection, unsupported transport faults, fragmented and
open-ended POST, and video/audio/metadata RTP reception. `--gstreamer` also
checks a native rtspsrc receiver. `--contract /contract` validates requests,
responses and SOAP faults against the existing pinned ONVIF 24.12 schema cache
(prepared as described in [the metadata experiment](../experiments/rtsp_metadata/README.md#pinned-contract)).
The transport types used here are unchanged between that cache and the referenced
service versions. The optional network smoke also
checks ordinary TCP/UDP streaming and discovery. Windows/WSL networking and VMS
acceptance still require the actual deployment checks in [WSL2.md](WSL2.md).

## Contract references

Checked against [ONVIF Streaming 26.06](https://www.onvif.org/specs/stream/ONVIF-Streaming-Spec.pdf)
§5.1.1.5 and its RTSP-over-HTTP reference; [Media 24.12](https://www.onvif.org/specs/srv/media/ONVIF-Media-Service-Spec.pdf)
§5.15.1; [Media2 25.12](https://www.onvif.org/specs/2512/ONVIF-Media2-Service-Spec-v2512.pdf)
§5.4.1; and [Replay Control 21.12](https://www.onvif.org/specs/srv/replay/ONVIF-ReplayControl-Service-Spec.pdf)
§5.1 (GetReplayUri). Wire types and operation namespaces come from
[Media ver10 WSDL](https://www.onvif.org/ver10/media/wsdl/media.wsdl) (schema version 21.06),
[Media2 ver20 WSDL](https://www.onvif.org/ver20/media/wsdl/media.wsdl) (schema version 26.06),
[Replay ver10 WSDL](https://www.onvif.org/ver10/replay.wsdl), and
[ONVIF ver10 XSD](https://www.onvif.org/ver10/schema/onvif.xsd), especially
StreamSetup, Transport, TransportProtocol and MediaUri. The web transport selects
HTTP versus HTTPS because neither Media's HTTP nor Media2's RtspOverHttp parameter
has a separate TLS selector. No WebSocket, direct RTSPS/SRTP or conformance claim
is added. Specification and schema files are referenced, not redistributed.
