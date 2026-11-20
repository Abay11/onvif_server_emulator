# Windows: Linux container in WSL2 mirrored networking

Native Windows builds are no longer supported. The full network deployment uses
Windows 11 22H2 or newer, an updated WSL2, and **Docker Engine installed directly
inside a Linux WSL distribution**. This profile supports clients on Windows and
other computers on the same LAN. Docker Desktop's WSL integration is a different
network topology and is not the supported full-discovery profile.

## 1. Configure Windows and WSL

In Windows PowerShell, inspect/update WSL:

```powershell
wsl --version
wsl --update
wsl --list --verbose
Get-NetIPConfiguration
Get-NetConnectionProfile
```

Choose the physical Ethernet/Wi-Fi interface used by your clients. In the examples,
its Windows IPv4 is `192.168.1.10` and the trusted LAN subnet is `192.168.1.0/24`.
Replace both throughout. Reserve the address with DHCP or update the configuration
and firewall rules when it changes. Do not select a Docker, VPN, or NAT adapter.

Merge these entries into `%UserProfile%\.wslconfig` (preserve other settings):

```ini
[wsl2]
networkingMode=mirrored
firewall=true

[experimental]
hostAddressLoopback=true
```

Apply with `wsl --shutdown`, then reopen your distribution. This command stops
all running WSL distributions and their containers. `hostAddressLoopback` allows
Windows clients to use the same Windows LAN IPv4 advertised to remote clients;
`localhost` alone cannot serve as a common address for LAN clients.

In WSL, confirm the LAN IPv4 is present:

```bash
wslinfo --networking-mode
ip -4 address
```

The networking mode must be `mirrored`. If `wslinfo` is unavailable, update WSL.

## 2. Install/select Docker Engine inside WSL

Install Docker Engine using the official instructions
for your distribution, for example [Ubuntu](https://docs.docker.com/engine/install/ubuntu/).
Install the Compose plugin too if you want to use either Compose example below.
Start it with `sudo systemctl enable --now docker` (requires systemd in WSL).
Use `sudo docker` below if your user does not have Docker socket access.

Disable Docker Desktop integration for this distribution to avoid CLI/socket
conflicts. Check the actual daemon before proceeding:

```bash
docker context ls
docker context inspect default
docker info --format '{{.Name}} / {{.OperatingSystem}}'
```

Use the local Engine socket, typically `unix:///var/run/docker.sock`, with the
Linux distribution as the server OS. A `desktop-linux` context or Docker Desktop
server is not this profile. Remove stale `DOCKER_HOST`/`DOCKER_CONTEXT` overrides
if they select another daemon. Merely running `docker` from a WSL terminal does
not establish which daemon runs the container.

## 3. Download and run

Run these commands in WSL after completing the networking and Docker Engine setup:

```bash
docker pull ghcr.io/abay11/onvif_server_emulator:latest
docker run -d --init --network host \
  -e ONVIF_ADVERTISED_IPV4=192.168.1.10 \
  -e ONVIF_DISCOVERY_INTERFACE_IPV4=192.168.1.10 \
  --name onvif-emulator \
  ghcr.io/abay11/onvif_server_emulator:latest
docker logs -f onvif-emulator
```

Replace both IPv4 values with the mirrored LAN IPv4 checked in step 1. The public
image includes the default configuration and requires no registry login, source
checkout or compiler. The release workflow currently publishes `linux/amd64`;
Windows on ARM is not a validated deployment profile.

Alternatively, download the root [compose.yaml](../compose.yaml) into a local WSL
directory and follow [the published-image Compose commands](../README.md#docker-compose).
It pulls the image without building it. See
[updates and rollback](../README.md#updates-and-rollback) before changing versions.

To stop and remove the `docker run` deployment:

```bash
docker stop onvif-emulator
docker rm onvif-emulator
```

Host networking shares the WSL distribution's network namespace. Do not combine
it with `-p`; only one emulator can occupy these ports at a time. No privileged
container or additional Linux capabilities are required. If Linux firewall rules
are enabled in your distribution, allow the same inbound ports there as well.
These requirements apply to both downloaded and locally built images. Choose
one deployment method; do not run the published and source-built containers together.

### Optional: build from source

From the repository root in WSL:

```bash
git submodule update --init --recursive
export ONVIF_ADVERTISED_IPV4=192.168.1.10
export ONVIF_DISCOVERY_INTERFACE_IPV4=192.168.1.10
docker compose -f docker/compose.mirrored.yaml up --build -d
docker compose -f docker/compose.mirrored.yaml logs -f
```

Or build the image as described in [BUILD.md](../BUILD.md) and run:

```bash
docker run --rm --init --network host \
  -e ONVIF_ADVERTISED_IPV4=192.168.1.10 \
  -e ONVIF_DISCOVERY_INTERFACE_IPV4=192.168.1.10 \
  --name onvif-emulator osrv-image:latest
```

## 4. Configure the firewall

Download the raw [Configure-OnvifFirewall.ps1](../scripts/windows/Configure-OnvifFirewall.ps1)
to Windows; a repository clone is not required. Run **Windows PowerShell as
Administrator** from the directory containing the downloaded script:

```powershell
.\Configure-OnvifFirewall.ps1 `
  -LanSubnet 192.168.1.0/24 -HostIPv4 192.168.1.10 -WhatIf

.\Configure-OnvifFirewall.ps1 `
  -LanSubnet 192.168.1.0/24 -HostIPv4 192.168.1.10
```

The script creates matching Windows Defender and WSL Hyper-V firewall rules,
limited to the **Private** network profile and the specified subnet, host IPv4,
and loopback. Ensure your trusted physical LAN is classified as Private. Do not
change an untrusted/public network to Private just to run the emulator.
The script leaves default firewall policies and other applications' rules alone.
Rerunning replaces only its three named rules in each firewall.
If using a source checkout instead, the script is at
`scripts/windows/Configure-OnvifFirewall.ps1` relative to the repository root.

| Traffic | Default local server ports | Purpose |
| --- | --- | --- |
| TCP inbound | 8080, 8554 | ONVIF HTTP, snapshots, events; RTSP and interleaved RTP/RTCP |
| UDP inbound/outbound | 3702 | Discovery multicast reception and unicast ProbeMatch replies |
| UDP inbound/outbound | 50000–50199 | Server RTP/RTCP endpoints for UDP streaming |

Outbound UDP rules allow remote client ports chosen during discovery/RTSP; those
ports need not equal the server ports. A Windows VMS/VLC receiving UDP media may
also require its own inbound application rule on the client. The server script
does not open arbitrary client ports or configure other computers' firewalls.

For custom listening ports, rerun with matching parameters:

```powershell
.\Configure-OnvifFirewall.ps1 `
  -LanSubnet 192.168.1.0/24 -HostIPv4 192.168.1.10 `
  -HttpPort 8080 -RtspPort 8554 -RtpPortMin 50000 -RtpPortMax 50199
```

If `PullPoint.UseHttpServerPort` is false, also pass `-PullPointPort 5550` (or your
configured port). Use **listening** ports here, not advertised port overrides.

Inspect rules and effective WSL settings:

```powershell
Get-NetFirewallRule -Group 'ONVIF Emulator'
Get-NetFirewallHyperVRule | Where-Object Name -Like 'OnvifEmulator-*'
Get-NetFirewallHyperVVMSetting -PolicyStore ActiveStore `
  -Name '{40E0AC32-46A5-438A-A0B2-2B479E8F2E90}'
```

Remove this deployment's rules:

```powershell
.\Configure-OnvifFirewall.ps1 -Remove
```

Corporate policy can prevent local allow rules or loopback traffic. Ask the
administrator to permit the scoped rules if effective policy blocks them; do not
disable the firewall or set WSL's default inbound action to Allow. The shipped
configuration has authentication disabled and example credentials: enable a
supported authentication mode and change credentials before sharing it on a LAN.

## 5. Addresses, configuration and files

`server_configs/common.config` separates socket binding from advertised endpoints:

```json
{
  "addresses": {"ipv4":"0.0.0.0", "http_port":"8080", "rtsp_port":"8554"},
  "advertised": {"ipv4":"192.168.1.10", "http_port":0, "rtsp_port":0, "pullpoint_port":0},
  "rtp": {"port_min":50000, "port_max":50199}
}
```

This is a fragment: merge into the complete existing config. `addresses` controls
socket listeners. `advertised.ipv4` must be a concrete IPv4 reachable by **every**
client; wildcard, multicast and broadcast addresses are rejected. It defaults to
`127.0.0.1` for local-only use and is overridden by `ONVIF_ADVERTISED_IPV4`.
Advertised port `0` means use the corresponding listening port (or the legacy
`portForwardingSimulation` override for HTTP/RTSP). Nonzero advertised ports take
precedence. A separate PullPoint listener uses `advertised.pullpoint_port` or its
configured listening port; shared HTTP PullPoint uses the advertised HTTP port.
In mirrored host networking, leave all advertised port overrides at zero.

`server_configs/discovery.config`:

```json
{"enabled":true, "interface_ipv4":"192.168.1.10"}
```

`ONVIF_DISCOVERY_INTERFACE_IPV4` overrides the interface setting. It must be a
local IPv4 visible in `ip -4 address` inside WSL; an empty setting selects the
kernel's default multicast interface. A bind/join failure prevents startup instead
of silently running without discovery. Always select it explicitly for the
mirrored profile, especially with VPN/multiple adapters. Set `enabled` to false
for deliberate operation without discovery. The ProbeMatch XML remains a template
for identity/types/scopes; its XAddrs is replaced at startup with the advertised
HTTP endpoint. IPv6 discovery, WS-Discovery Hello/Bye/Resolve, and multicast RTP
are not added by this deployment; discovery currently answers IPv4 Probes.

For a downloaded image, first [extract the bundled configuration](../README.md#custom-configuration-and-persistence)
without cloning the repository. Mount the **complete** directory to
`/app/server_configs` when customizing it. Use a writable mount to persist SOAP
profile changes, or add `:ro` when those operations are not needed. Changes made
by editing files can be applied through ONVIF
[`SystemReboot`](SYSTEM_REBOOT.md), which restarts the application inside the same
container. Do not edit only the ProbeMatch
XAddr or use a container's `172.x` address as the public endpoint.

For file streaming, mount media separately:

```bash
docker run --rm --init --network host \
  -e ONVIF_ADVERTISED_IPV4=192.168.1.10 \
  -e ONVIF_DISCOVERY_INTERFACE_IPV4=192.168.1.10 \
  -v "$(pwd)/server_configs:/app/server_configs:ro" \
  -v /mnt/c/Videos:/media:ro \
  ghcr.io/abay11/onvif_server_emulator:latest
```

Set `fileStreaming.enabled=true` and `fileStreaming.filePath=/media/video.mp4`
in `common.config`. Paths are Linux paths **inside the container** and are case
sensitive. Windows paths such as `C:\Videos\video.mp4` and WSL paths not mounted
into the container cannot be used there. Clients access HTTP/RTSP URLs and do not
need filesystem access to media. Snapshots are served from
`server_configs/rs/snapshot.jpeg`.

## 6. Verify from Windows and another LAN computer

With `192.168.1.10` replaced by your actual address:

```powershell
Test-NetConnection 192.168.1.10 -Port 8080
Test-NetConnection 192.168.1.10 -Port 8554
```

Use these URLs, including the `&` in stream names:

- ONVIF device service: `http://192.168.1.10:8080/onvif/device_service`
- Snapshot: `http://192.168.1.10:8080/snapshot.jpeg`
- RTSP main/substream: `rtsp://192.168.1.10:8554/Live&HighStream` and `/Live&LowStream`
- Replay: `rtsp://192.168.1.10:8554/Recording0`

In VLC test TCP first (`vlc --rtsp-tcp "rtsp://192.168.1.10:8554/Live&HighStream"`),
then test UDP separately. In your ONVIF VMS, verify:

1. Automatic discovery, with manual device entries removed.
2. GetCapabilities/GetServices XAddrs, Media and Media2 stream/snapshot URLs,
   replay URI and PullPoint subscription reference all contain the selected LAN IPv4.
3. Live video **and audio**, snapshot retrieval, event subscription/PullMessages,
   and the emulator's replay behavior.
4. UDP streaming including RTCP, multiple simultaneous clients and both streams.
5. Repeat after WSL restart, with the actual VPN and Ethernet/Wi-Fi configuration.

An HTTP GET on the device service may return 400: SOAP POST is expected. This
alone is not a failed connectivity test. An open RTSP TCP port alone does not
prove UDP media works. Replay here is the project's synthetic replay emulation,
not storage of live footage.

For discovery diagnosis inside WSL:

```bash
sudo tcpdump -ni any 'udp port 3702'
sudo ss -lntup
```

Verify incoming Probe to `239.255.255.250:3702`, outgoing unicast ProbeMatch to
the client's source port, then the client's HTTP request to the advertised XAddr.
A missing Probe suggests interface/firewall/VPN/access-point multicast filtering;
a received Probe with no working device usually requires checking the reply and
advertised URL. Multicast does not automatically cross VLANs or isolated Wi-Fi
segments. Test Windows-host discovery separately from discovery on another PC.
Microsoft's mirrored excluded-port list mentions **TCP** 3702; this emulator
uses **UDP** 3702.

The repository's Linux/container tests do not certify a particular Windows,
WSL, VPN or VMS installation. Complete this acceptance check on the actual
Windows machine before treating the migration as functionally equivalent.

## References

- [Microsoft: WSL networking](https://learn.microsoft.com/en-us/windows/wsl/networking)
- [Microsoft: WSL settings](https://learn.microsoft.com/en-us/windows/wsl/wsl-config)
- [Microsoft: WSL troubleshooting](https://learn.microsoft.com/en-us/windows/wsl/troubleshooting)
- [Microsoft: Hyper-V firewall](https://learn.microsoft.com/en-us/windows/security/operating-system-security/network-security/windows-firewall/hyper-v-firewall)
- [Docker: host networking and Desktop limitations](https://docs.docker.com/engine/network/drivers/host/)
