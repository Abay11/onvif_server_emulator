# ONVIF Server Emulator

ONVIF Server Emulator is a C++20 application for Linux that emulates an IP camera
or network video recorder (NVR) for testing ONVIF clients. It provides SOAP
services over HTTP, media streaming over RTSP/RTP, and IPv4 WS-Discovery.

Full ONVIF functionality is not yet supported. The project is under active
development, with the goal of progressively expanding coverage to support the
full range of ONVIF features.
See the [architecture overview](docs/architecture.md) for implementation details
and current limitations.

## Run the Published Docker Image

The public image includes the application, default configuration and runtime
dependencies. No Git clone, compiler or local build is required. Use Docker Engine
on Linux with an x86-64 CPU (`linux/amd64`); the current release workflow does not
publish an ARM64 image. The Compose instructions below also require Docker Compose v2.

Download the image without signing in to GHCR:

```bash
docker pull ghcr.io/abay11/onvif_server_emulator:latest
```

### Linux: LAN access and discovery

Run with the Docker host's LAN IPv4 (replace `192.168.1.10` in both variables):

```bash
docker run -d --init --network host \
  -e ONVIF_ADVERTISED_IPV4=192.168.1.10 \
  -e ONVIF_DISCOVERY_INTERFACE_IPV4=192.168.1.10 \
  --name onvif-emulator \
  ghcr.io/abay11/onvif_server_emulator:latest
```

The discovery interface must exist on the Docker Engine host. Host networking
uses the host's ports directly; do not add `-p`. Default ports are TCP `8080`
(HTTP/SOAP) and `8554` (RTSP), UDP `3702` (discovery), and UDP `50000–50199`
(RTP/RTCP). Allow the required traffic through the host firewall. A separate
PullPoint listener, if enabled in configuration, also needs its configured port.

Connect an ONVIF client to `http://192.168.1.10:8080/onvif/device_service` or use
automatic discovery. In VLC, test
`rtsp://192.168.1.10:8554/Live&HighStream` (quote the URL in a shell).
Check HTTP with `curl --fail http://192.168.1.10:8080/snapshot.jpeg -o snapshot.jpeg`.
The bundled configuration has authentication disabled.

```bash
docker logs -f onvif-emulator
# Stop and remove the container when finished:
docker stop onvif-emulator
docker rm onvif-emulator
```

### Windows, macOS and local TCP testing

For Windows clients and LAN discovery, use **Docker Engine inside WSL2 mirrored
networking** and follow [the Windows/WSL2 guide](docs/WSL2.md), including firewall
setup. Docker Desktop's WSL integration is a different network topology.

For a limited local test on Linux or Docker Desktop, use:

```bash
docker run -d --init \
  -p 127.0.0.1:8080:8080 -p 127.0.0.1:8554:8554 \
  --name onvif-emulator \
  ghcr.io/abay11/onvif_server_emulator:latest
```

Manually add `http://127.0.0.1:8080/onvif/device_service` to the client and select
RTP interleaved over RTSP/TCP, for example
`vlc --rtsp-tcp 'rtsp://127.0.0.1:8554/Live&HighStream'`.
These TCP mappings do not provide LAN discovery or UDP media. On macOS, full
WS-Discovery/UDP operation through Docker Desktop is not a validated deployment
profile. Apple Silicon also requires amd64 emulation (`--platform linux/amd64`);
native ARM64 images and their operation are not currently provided by the workflow.

### Docker Compose

Download the raw [compose.yaml](compose.yaml) into an otherwise empty directory, or use
the copy in this repository. From that directory, on Linux or inside the configured
WSL2 distribution:

```bash
export ONVIF_ADVERTISED_IPV4=192.168.1.10
export ONVIF_DISCOVERY_INTERFACE_IPV4=192.168.1.10
docker compose pull
docker compose up -d
docker compose logs -f
# Stop and remove the deployment:
docker compose down
```

This Compose file uses the published image and host networking with the ports
listed above. It does not build source code. Run either the Compose deployment or
the equivalent `docker run` deployment: both use the same host ports.

### Custom configuration and persistence

Extract the complete bundled configuration into a new local directory:

```bash
docker create --name onvif-config-export ghcr.io/abay11/onvif_server_emulator:latest
docker cp onvif-config-export:/app/server_configs ./server_configs
docker rm onvif-config-export
```

Edit the JSON `.config` files described below, then add this option **before the
image name** in the `docker run` command:

```bash
--mount "type=bind,src=$(pwd)/server_configs,dst=/app/server_configs"
```

For Compose, uncomment the `volumes` block in `compose.yaml`; its relative path
is resolved from the Compose file's directory. Mount the complete directory,
including `responses` and `rs`. Restart the container after editing configuration.
The environment variables in the examples override the corresponding IPv4 settings.

The application must be able to write `media_profiles.config` to persist SOAP
profile changes. A read-only mount is suitable only when those operations are not
needed. Without a mount, changes inside the container are lost when it is removed.
See [the file streaming example](docs/WSL2.md#5-addresses-configuration-and-files)
for an additional media mount.

### Updates and rollback

Before updating, save the current immutable image reference:

```bash
docker image inspect --format '{{index .RepoDigests 0}}' \
  ghcr.io/abay11/onvif_server_emulator:latest
```

Keep the printed `ghcr.io/abay11/onvif_server_emulator@sha256:...` reference and a
backup of your mounted configuration. Then pull the new image and recreate the
container with the same network, environment and mount options:

```bash
docker pull ghcr.io/abay11/onvif_server_emulator:latest
docker stop onvif-emulator
docker rm onvif-emulator
# Repeat your docker run command above.
```

For Compose, use `docker compose pull` followed by `docker compose up -d`.
Pulling alone does not update an existing container, and restarting it does not
switch images. To roll back, use the saved digest instead of `:latest` in
`docker run`, or set `ONVIF_IMAGE` to that full reference and run
`docker compose up -d`. Restore the matching configuration backup if necessary.
Published version tags can also be selected instead of `latest`; use a tag that
exists on the [package page](https://github.com/Abay11/onvif_server_emulator/pkgs/container/onvif_server_emulator).

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

### RTSP event metadata (`common.config`)

ONVIF event metadata configuration and streaming are enabled by default
(`rtsp_metadata.enabled=true`, including when the setting is omitted). Set it to
`false` to disable this feature. List/configure metadata through Media or
Media2, attach a metadata configuration to a profile, then use that profile's
`GetStreamUri` result. Clients can select metadata alone or with video/audio;
PullPoint subscriptions receive notifications independently. Normal Docker
runtime builds include the required GStreamer payloader.

The example metadata catalog has two configurations with events enabled and no
profile associations. Supported controls include event enable/disable, topic and
ItemFilter content filters, and media synchronization points. See
[live RTSP metadata](docs/RTSP_METADATA.md) for SOAP operations, filter examples,
persistence/session behavior and validation commands. Analytics scene frames,
PTZ metadata, compression and multicast metadata are unsupported.

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

## Analytics Rules

The Analytics Rules interface supports Motion Region Detector (polygon regions,
arming and sensitivity), Line Detector (polylines and crossing direction),
LineCounting (crossing counts, multiple polylines, report intervals and daily resets),
Field Detector (object presence inside a normalized polygon), License Plate
Recognition (Annex G.3, synthetic plates, region/snap line, repeat intervals,
embedded/downloadable test images and five cycling vehicle datasets, including a motorcycle), and Face Recognition
(Annex G.2, three synthetic identities, reference images, bounding boxes,
object IDs and embedded/downloadable test portraits).
They generate PullPoint events using deterministic synthetic input.
See [Analytics Rules](docs/ANALYTICS.md) for SOAP operations,
configuration, examples and limitations. This feature does not claim Profile M
certification or analyze live video.
