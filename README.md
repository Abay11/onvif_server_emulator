# 🌟About the project

Onvif Server Emulator is an emulator of an IP device (camera, NVR) compatible with standardized ONVIF interfaces, which can be used for tests purpoces.

# 🌐 Used 3rd party libraries

[Boost](https://www.boost.org/)

[GStreamer](https://github.com/GStreamer/gstreamer)

[Simple-Web-Server](https://gitlab.com/eidheim/Simple-Web-Server)

# ▶ Server execution

A configuration directory may be passed as command line argument.
As a default configuration directory is used "./server_configs".
Example: `./main ./configs`. Linux is the supported build platform.

On Windows, use the [WSL2 mirrored container and firewall guide](docs/WSL2.md)
for local VMS/VLC clients and LAN access, including WS-Discovery.


# ⚙️ Server configurations

Each ONVIF service is implemented as a separate instance. Each service may have its own configuration file in JSON format. Please note, that some configs may be hardcoded in the code, or may be not fully implemented yet. It's always recommended to check, if a config parameter actually affect to some behaviour.

## Common configs

"authentication" - current authentication method is choosen via this variable. Any values from "authenticationMethods" can be used.
"authenticationMethods" - enums available values. Here is they desctiption: "none" - authentication is not required; "ws-security" - only WS-Security; "digest" - only digest
"loggingLevel" - allowed values: ERROR, WARN, INFO, DEBUG, TRACE. Values list from highegt to lowest priority, i.e. if used level is INFO, all logs will be showed, except DEBUG and TRACE. If value is WARN - only errors and warnings messages will be showed.
"portForwardingSimulation" - this section in config is used to setup the server to return in url's specified http and rtsp ports, i.e. in that way the server actually will listen one ports but return another ports

## Device service configs

"DigitalInputs" - represent an array, with which amount of emulated digital input may be registered. For each component may be specified token, initial state and whether it should simulated events or not.

"DigitalInputsTopic" - specifies DI events' topic. Expected a three-tired value, i.e. the value should contains exactly three values which are separated by slashes.


#### GetServices method

"Enabled" - boolean value. Can be used to do not include specific service in the response list. If this flag is absent, default value will be true. For example, it will be usefull to enabling/disabling Media2 service.


## Event service configs

"ReadResponseFromFile" - boolean value, specifies whether GetEventProperties response should be read from a file or not.

#### PullPoint

"IgnoreClientsTimeout" - boolean value specifies whether ingore or not a timeout value from the PullMessages request. Currently this value is ignored and supposed always be true, i.e. it is hardcoded in the code. Actual timeout value equals to the value specified in "Timeout".

"Timeout" - PullMessages timeout in seconds.

"UseHttpServerPort" - `true` shares the main HTTP listener; `false` creates a separate listener on `PullPoint.Port`.

## Network endpoint configuration

`addresses.ipv4` is the socket bind address. `advertised.ipv4` is the concrete
IPv4 returned to clients by discovery, ONVIF, media, snapshots, replay and events.
It defaults to loopback; override it with `ONVIF_ADVERTISED_IPV4` for LAN clients.
Optional `advertised.http_port`, `rtsp_port`, and `pullpoint_port` override
client-visible ports; zero uses listening ports, with legacy forwarding settings
as fallback for HTTP/RTSP. These settings do not create port forwarding rules.
`rtp.port_min`/`port_max` constrain UDP server ports (default 50000–50199,
first even and last odd). See the [network guide](docs/WSL2.md) for examples.

## Discovery service configs

`enabled` controls IPv4 WS-Discovery Probe responses. `interface_ipv4` selects
one local multicast interface; empty uses the system default. The environment
variable `ONVIF_DISCOVERY_INTERFACE_IPV4` overrides it. The template is
`server_configs/responses/ProbeMatch.response`; its XAddrs is filled from the
advertised HTTP endpoint at startup.

 ## Recording Search

 #### Recordings descriptions

 "DataFrom" - The earliest point in time. If not specified is condidered data recorded from 24 hours ago.
 "DataUntil" - The most recent point in time. If not specified is condidered data recorded until now.

# 🛠 Building

Please review [BUILD.md](BUILD.md) for how to setup OnvifServerEmulator on your local machine for development and testing purposes.
