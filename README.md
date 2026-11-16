# ONVIF Server Emulator

ONVIF Server Emulator is a C++20 application for Linux that emulates an IP camera
or network video recorder (NVR) for testing ONVIF clients. It provides SOAP
services over HTTP, media streaming over RTSP/RTP, and IPv4 WS-Discovery.

Full ONVIF functionality is not yet supported. The project is under active
development, with the goal of progressively expanding coverage to support the
full range of ONVIF features.
See the [architecture overview](docs/architecture.md) for implementation details
and current limitations.

## Third-Party Libraries

- [Boost](https://www.boost.org/)
- [GStreamer](https://github.com/GStreamer/gstreamer)
- [Simple-Web-Server](https://gitlab.com/eidheim/Simple-Web-Server)

## Building

See [BUILD.md](BUILD.md) for dependencies, Linux and Docker build instructions,
and testing procedures.

## Running the Server

Run the application from the repository root after building it:

```bash
./build/main ./server_configs
```

The optional first command-line argument specifies the configuration directory.
If omitted, the application uses `./server_configs`, relative to the current
working directory. A standard CMake build does not copy configuration files
beside the executable.

Linux is the supported build platform. On Windows, follow the
[WSL2 mirrored networking and firewall guide](docs/WSL2.md) for local VMS/VLC
clients and LAN access, including WS-Discovery.

## Configuration

Configuration files use JSON and have case-sensitive `lower_snake_case.config`
filenames (lowercase letters, with underscores between words and no spaces).
Shared settings are stored in `common.config`; service-specific settings are
stored in files such as `device.config` and `event.config`.

Existing configuration directories must use the same names: `device.config`,
`device_io.config`, `imaging.config`, `media.config`, `media2.config`, `ptz.config`,
`recording_search.config`, and `replay_control.config`. Rename older capitalized
filenames and replace spaces with underscores before running this version.

Optional [JSON Schema validation](schemas/README.md) checks edited files before
startup. Schemas cover every bundled `.config` file and can also be used by
editors or a future GUI configurator. The checker reports structural errors;
cross-file token references and topology consistency need separate validation.

Configuration support varies by operation. Some settings are hardcoded or only
partially implemented, so verify the relevant handler before relying on a
parameter to change server behavior. In particular, media settings returned by
SOAP operations do not necessarily change the RTSP pipelines.

### Common Settings (`common.config`)

- `authentication`: selects the authentication mode. The configuration lists
  `none`, `ws-security`, `digest`, and `digest/ws-security`, but the shared SOAP
  dispatcher and Event service check credentials only in `digest` mode.
  WS-Security and the combined mode are not implemented in those dispatchers.
- `authenticationMethods`: lists authentication mode names in the sample
  configuration; the list does not establish implementation support.
- `loggingLevel`: sets the logging threshold. Available levels, from highest to
  lowest severity, are `ERROR`, `WARN`, `INFO`, `DEBUG`, and `TRACE`. For example,
  `INFO` includes errors, warnings, and informational messages; `WARN` includes
  only errors and warnings.
- `loggingOutput`: selects `console` or `file`. File output is written to
  `OnvifServer.log` in the current working directory.
- `portForwardingSimulation`: overrides HTTP and RTSP ports in URLs returned to
  clients when `enabled_for_http` or `enabled_for_rtsp` is `true`. It does not
  change the listening ports or create port forwarding rules. Explicit
  `advertised` port settings take precedence.

### Network Endpoints (`common.config`)

`addresses.ipv4` specifies the socket bind address; `addresses.http_port` and
`addresses.rtsp_port` specify the listening ports. The defaults are `0.0.0.0`,
`8080`, and `8554`, respectively.

`advertised.ipv4` specifies the concrete IPv4 address used in URLs returned to
clients for discovery, SOAP services, media, snapshots, replay, and events. It
uses `127.0.0.1` by default. For LAN clients, set it to a reachable server address
or override it with the `ONVIF_ADVERTISED_IPV4` environment variable.

`advertised.http_port` and `advertised.rtsp_port` override the ports returned to
clients. A value of `0` uses the corresponding listening port unless a legacy
`portForwardingSimulation` override is enabled.

When PullPoint uses the main HTTP listener, subscriptions use the advertised HTTP
endpoint. For a separate listener, `advertised.pullpoint_port` overrides the
client-visible port; `0` uses `PullPoint.Port` from `event.config`.

`rtp.port_min` and `rtp.port_max` define the UDP RTP/RTCP port range. The default
range is `50000–50199`; the first port must be positive and even, and the last
port must be larger and odd.

Advertised endpoint settings do not configure NAT or firewall rules. See the
[networking guide](docs/WSL2.md) for deployment examples.

### Device Service (`device.config`)

`DigitalInputs` is an array of emulated digital inputs. Each entry specifies a
`Token`, an `InitialState`, and a `GenerateEvent` flag that controls simulated
event generation for that input.

Each entry in `GetServices` can include an `Enabled` flag. Setting it to `false`
excludes the service from the `GetServices` response; the default is `true`.
This controls service advertisement and does not disable the service handler.

### Event Service (`event.config`)

`GetEventProperties.ReadResponseFromFile` controls how the `GetEventProperties`
response is produced. If `true`, the response is read from `ResponseFilePath`
within the `GetEventProperties` section, relative to the configuration directory.
If `false`, the response is generated from the event settings.

`DigitalInputsAlarm.Topic` specifies the digital input event topic, for example,
`tns1:Device/Trigger/DigitalInput`.

The `PullPoint` section contains the following settings:

- `IgnoreClientsTimeout`: currently ignored by the implementation. The server
  always uses the configured `Timeout` instead of the client's `PullMessages`
  timeout.
- `Timeout`: sets the server-side `PullMessages` wait timeout in seconds.
- `UseHttpServerPort`: `true` shares the main HTTP listener; `false` creates a
  separate listener on `PullPoint.Port`.
- `Port`: sets the separate listener's port when `UseHttpServerPort` is `false`.
  The default is `5550`.

### Discovery Service (`discovery.config`)

`enabled` controls IPv4 WS-Discovery Probe responses. `interface_ipv4` selects a
local multicast interface; an empty value uses the system default. The
`ONVIF_DISCOVERY_INTERFACE_IPV4` environment variable overrides this setting.

At startup, the server fills the `XAddrs` field in
`server_configs/responses/ProbeMatch.response` with the advertised HTTP endpoint.
Hello, Bye, Resolve, and IPv6 discovery are not implemented.

### Recording Search (`recording_search.config`)

The `Recordings` array describes emulated recordings. Each entry requires a
`Token`, a `VideoTrack` token, and the following time fields:

- `DateFrom`: the earliest recording time. An empty string uses the current UTC
  time minus 24 hours.
- `DateUntil`: the latest recording time. An empty string uses the current UTC
  time.

Both date keys must be present, even when their values are empty. Nonempty values
use the basic ISO date-time format, for example, `20210523T000000`, and represent
UTC times. These settings describe a simulated archive; the emulator does not
record live video to disk.
