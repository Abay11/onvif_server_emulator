# How to Build OnvifServerEmulator
## Getting the Source

This project is [hosted on GitHub](https://github.com/Abay11/onvif_server_emulator). You can clone this project directly using this command:
```
git clone https://github.com/Abay11/onvif_server_emulator.git
```

# Dependencies

To build OnvifServerEmulator:

* cmake 3.16 or later to generate build files
* C++ compiler with decent C++20 support
* Boost libraries 1.72 or later:
	- system
	- date_time
	- regex
	- thread
	- asio
	- property-tree
	- signals2
	- test (optional, for unit-test purposes)
* GStreamer libraries and plugins 1.16 or later:
	- core
	- pango plugin
	- plugins-base
	- plugins-good
	- plugins-ugly
	- plugins-bad
	- x264

# Building

### Supported platforms

Builds target Linux only. On Windows use WSL2 and the Linux container. For
Windows-host and LAN clients, including WS-Discovery and UDP media, follow the
[WSL2 mirrored networking and firewall guide](docs/WSL2.md).

### Building on Linux

**Step 0.** Update package indexes:

```bash
sudo apt update
```

**Step 1.** Install toolchain and build tools:

```bash
sudo apt install -y build-essential cmake pkg-config git
```

**Step 2.** Install GStreamer libraries:

For Debian Trixie (and later):
```bash
sudo apt install -y \
  libgstreamer1.0-dev \
  libgstreamer-plugins-base1.0-dev \
  libgstreamer-plugins-bad1.0-dev \
  gstreamer1.0-rtsp \
  libgstrtspserver-1.0-dev \
  gstreamer1.0-plugins-good \
  gstreamer1.0-plugins-ugly \
  gstreamer1.0-x \
  gstreamer1.0-libav \
  gstreamer1.0-tools
```

**Step 3.** Install Boost libraries:

```bash
sudo apt install -y \
  libboost-system-dev libboost-date-time-dev libboost-regex-dev \
  libboost-thread-dev libboost-filesystem-dev \
  libboost-test-dev libboost-serialization-dev
```

**Step 4.** Clone repository (if not already) and create build directory:

```bash
git clone https://github.com/Abay11/onvif_server_emulator.git
cd onvif_server_emulator
mkdir -p build && cd build
```

**Step 5.** Configure and build:

```bash
cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo ..
cmake --build . -- -j$(nproc)
```

**Step 6.** Run tests from the repository root (optional):

```bash
cmake -S . -B build -DENABLE_UNIT_TESTS=ON
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
```

**Optional:** If your distro has different package names, substitute accordingly (e.g., Fedora/RHEL use `dnf`/`yum`).

### Building Docker

Initialize the bundled Simple-Web-Server submodule before building. The Docker
build context excludes `.git`, so CMake cannot download the submodule inside the
image:

```bash
git submodule update --init --recursive
```

Build the application image from the repository root:

```bash
docker build -f docker/Dockerfile -t osrv-image:latest .
```

The default `runtime` stage includes the executable, default `server_configs`,
fonts, and the GStreamer plugins used for live streaming and replay. Compiler,
headers, source files, and diagnostic tools stay in the build stages. It uses
Debian Trixie Slim and installs packages without recommendations.

For full network operation on native Linux, select the LAN IPv4 and interface:

```bash
docker run --rm --init --network host \
    -e ONVIF_ADVERTISED_IPV4=192.168.1.10 \
    -e ONVIF_DISCOVERY_INTERFACE_IPV4=192.168.1.10 \
    osrv-image:latest
```

Replace the example address with an IPv4 on the Docker Engine host. On Windows,
use **Docker Engine inside WSL2 mirrored**, with the
[documented Windows/Hyper-V firewall rules](docs/WSL2.md); Docker Desktop host
networking is not equivalent to this profile.

To customize configuration, mount the complete directory:

```bash
docker run --rm --init --network host \
    -e ONVIF_ADVERTISED_IPV4=192.168.1.10 \
    -e ONVIF_DISCOVERY_INTERFACE_IPV4=192.168.1.10 \
    -v "$(pwd)/server_configs:/app/server_configs:ro" \
    --name onvif-server-emul osrv-image:latest
```

For a local-only TCP test with manual device registration:

```bash
docker run --rm --init \
    -p 127.0.0.1:8080:8080 -p 127.0.0.1:8554:8554 \
    osrv-image:latest
```

In this last profile use `127.0.0.1` URLs and RTP interleaved over RTSP/TCP.
These two port mappings do not provide multicast discovery or UDP media to LAN
clients. Default UDP server media ports in the full profile are 50000–50199.

GStreamer diagnostics are opt-in; add `-e 'GST_DEBUG=*:3'` to `docker run` when
needed.

The build type defaults to `Debug` for a quick development build. To produce an
optimized executable:

```bash
docker build -f docker/Dockerfile \
    --build-arg CMAKE_BUILD_TYPE=Release -t osrv-image:release .
```

Parallel compilation defaults to the available CPU count. On machines with
limited RAM, add `--build-arg BUILD_JOBS=2` to reduce peak memory usage.

BuildKit caches the CMake build directory and ccache data on the current builder.
Cache mounts use exclusive access to prevent simultaneous builds from modifying
the same build tree. Clearing the builder cache removes these accelerators.

#### Reusable development environment

Build the `dev` target once. It includes the compiler, CMake, Git, ccache, and
`gst-inspect-1.0` / `gst-launch-1.0`, and does not copy or compile the project:

```bash
docker build -f docker/Dockerfile --target dev -t osrv-dev:latest .
```

Mount the repository to build interactively. Named volumes retain build output
and ccache data independently of the container:

```bash
docker run --rm -it \
    -v "$(pwd)":/workspace \
    -v osrv-dev-build:/workspace/build \
    -v osrv-dev-ccache:/root/.cache/ccache \
    osrv-dev:latest
```

Inside the container:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_CXX_COMPILER_LAUNCHER=ccache
cmake --build build --parallel "$(nproc)"
./build/main ./server_configs
```

Do not share the development build volume between concurrent build containers.
To run network services from this development container, add Linux host networking and the advertised/discovery environment variables
shown above (or the local-only TCP mappings for a limited test).

For onboarding across machines, publish the development image to your team's
registry after building it, then let developers pull it instead of reinstalling
the toolchain. Replace the registry and namespace with your own:

```bash
docker tag osrv-dev:latest registry.example.com/team/osrv-dev:trixie
docker push registry.example.com/team/osrv-dev:trixie
# On another machine:
docker pull registry.example.com/team/osrv-dev:trixie
```

Use the pulled image name in the development `docker run` command. Rebuild and
republish it when dependencies change. Publishing is a separate, explicit step;
a local Docker build does not upload an image.

### Network smoke test

With a running emulator, Python 3 can check the default SOAP service configuration,
returned URLs, snapshot retrieval, unicast and multicast discovery, and live video
and audio RTP over both TCP and UDP:

```bash
python3 unit_tests/network_smoke.py \
    --host 192.168.1.10 --advertised-ip 192.168.1.10 \
    --multicast-interface 192.168.1.20
```

Here `--host` is the emulator address and `--multicast-interface` is a local IPv4
on the **test client's** LAN adapter. Replace both example addresses. For a test
in the server's own Linux network namespace use `--host 127.0.0.1` and select the
interface joined by discovery. This automated test complements the Windows/VMS
acceptance steps in [the mirrored guide](docs/WSL2.md); it does not configure
firewalls. The test assumes authentication is disabled and creates a short-lived
PullPoint subscription.
