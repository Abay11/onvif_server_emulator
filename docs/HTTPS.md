# HTTP and HTTPS listeners

HTTP (8080) and HTTPS (8443) are enabled by default, including when the new
settings are absent in an older configuration. Both listeners serve the same
SOAP, Event/PullPoint, snapshot and administration routes, with the same
ONVIF authentication and mandatory WebUI administrator sign-in.

Merge these settings into `common.config`:

```json
{
  "addresses": {"ipv4": "0.0.0.0", "http_port": "8080", "https_port": "8443", "rtsp_port": "8554"},
  "http": {"enabled": true},
  "https": {"enabled": true, "certificate": "", "private_key": ""},
  "advertised": {"ipv4": "127.0.0.1", "web_scheme": "auto", "http_port": 0, "https_port": 0, "rtsp_port": 0, "pullpoint_port": 0}
}
```

At least one web listener must remain enabled. Ports must be in 1–65535;
HTTP and HTTPS must use distinct ports when both are enabled. Settings are read
at startup. In WebUI, open the separate **Network configuration** section, select the
allowed connections using the checkboxes and set their ports. This section also
contains bind/advertised addresses, RTP/RTCP ports, Discovery and event connections. Save, then restart the application. Reconnect to the new URL manually if
you disable the listener or change the port used by your browser; automatic
reconnection uses the existing browser origin. Active settings describe the
current process until restart.

## Certificates

TLS requires version 1.2 or newer. With empty certificate/key paths, the server
generates a fresh RSA-2048, SHA-256 self-signed certificate in memory on every
startup. It is valid for one year and includes localhost, 127.0.0.1 and the
advertised IPv4 in its subject alternative names. No shared private key is
shipped, and no write access to the configuration mount is required. Browsers
and clients will require an explicit trust exception for this development
certificate; its fingerprint changes after every restart.

For stable identity, supply a PEM certificate chain and its matching unencrypted
PEM private key using `https.certificate` and `https.private_key`. Configure both
paths together. Relative paths resolve against the configuration directory;
absolute paths refer to the Linux/container filesystem. Mount certificate files
into the container and restrict private-key filesystem access. Invalid/missing
files or a mismatching key abort startup when HTTPS is enabled. Certificate/key
file contents are never exposed by the configuration API; the editor saves paths.
The emulator does not implement certificate issuance or client-certificate login.

```bash
curl --insecure --fail https://127.0.0.1:8443/snapshot.jpeg -o snapshot.jpeg
# With a certificate trusted through your own CA:
curl --cacert /path/to/ca.pem https://camera.example:8443/snapshot.jpeg -o snapshot.jpeg
```

Open `https://127.0.0.1:8443/admin/` for HTTPS administration. HTTPS sign-in
responses set the session cookie's Secure attribute. Authentication and CSRF
checks apply on both transports; proxy forwarding headers do not determine
transport security.

## Advertised URLs and deployment

`advertised.web_scheme` selects the protocol for service, discovery, snapshot,
analytics image and subscription URLs:

- `auto` (default): HTTP when enabled, otherwise HTTPS.
- `http`: use HTTP; the HTTP listener must be enabled.
- `https`: use HTTPS, including while HTTP remains enabled; the HTTPS listener must be enabled.

Select **Advertised web protocol → HTTPS** in Network configuration to announce
HTTPS endpoints through Discovery while keeping both listeners available.
HTTPS URLs use `advertised.https_port` (zero selects `addresses.https_port`). Legacy HTTP port
forwarding simulation applies only to HTTP URLs. RTSP URLs are unchanged.
A separate PullPoint listener (`UseHttpServerPort=false`) uses the selected
advertised web protocol, with TLS and the same certificate settings for HTTPS.

Allow inbound TCP 8443 alongside the existing HTTP/RTSP ports. For Docker bridge
networking add `-p 127.0.0.1:8443:8443`; host networking needs no port mapping.
The Windows firewall helper accepts `-HttpsPort` (default 8443). It opens the
configured ports; it does not read the application listener enable flags.

The implementation shares one HTTP stream/handler type with optional TLS rather
than forwarding requests through a second HTTP hop. Remote client addresses,
Digest request targets, delayed responses and response-completion reboot guards
therefore retain their original behavior. Failure to bind an enabled main web
listener fails startup and stops the other main listener.

## Protocol scope and validation

Transport references: [ONVIF Core Specification 26.06](https://www.onvif.org/specs/core/ONVIF-Core-Specification.pdf),
sections 5.9.1 (authentication over HTTP/HTTPS), 7.3.2.3 (discovery addresses) and
8.1.2 (capability exchange); [Device Management WSDL, ver10](https://www.onvif.org/ver10/device/wsdl/devicemgmt.wsdl)
and its [ONVIF schema, ver10](https://www.onvif.org/ver10/schema/onvif.xsd).
This change adds transport and endpoint selection, and reports TLS1.2 support in
Device GetServiceCapabilities/GetServices Security attributes when HTTPS is enabled.
TLS1.0/TLS1.1 are reported as unsupported. SOAP request/response structures are unchanged. Existing service namespace/version references continue
to apply. TLS capability alone does not claim ONVIF conformance or implement the
optional ONVIF certificate-management or network-protocol configuration operations.

Run `python3 unit_tests/https_smoke.py --binary build/main` in the documented dev
container. It uses configuration copies to check both listeners, HTTPS-only and
HTTP-only startup, SOAP/snapshots, WebUI sign-in/configuration saves, advertised
URLs, TLS capability attributes, Digest authentication, save/restart with port changes,
TLS certificate verification, separate TLS PullPoint and invalid configuration rejection. Also run
CTest and `unit_tests/network_smoke.py` with the usual authentication-disabled
copy to check discovery and TCP/UDP media. Windows/WSL and VMS acceptance still
requires the actual deployment described in [WSL2.md](WSL2.md).
