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

### Building on Windows

For Windows it is recommended to use [vcpkg](https://vcpkg.io/en/getting-started.html).

After vcpkg installation, install other dependencies. In this example we will use x64 packages.

**Step 0.** Install [CMake](https://cmake.org/download/) and add it to PATH.

**Step 1.** Install `GStreamer` libs and plugins:

```
vcpkg install --triplet x64-windows gstreamer gstreamer[plugins-base] gstreamer[plugins-good] gstreamer[plugins-ugly] gstreamer[plugins-bad] gstreamer[pango] gstreamer[x264] gstreamer[x265] gst-rtsp-server
```

Make sure the installation was completed successfully with command:
```
vcpkg list | findstr "gst"
```
All required GStreamer dependencies should be listed.

**Step 2.** For proper work GStreamer applications require to point binaries and plugin directories.

Add the `GST_PLUGIN_PATH` variable to system environment. Typical install locations:

`C:\your_vcpkg_dir\packages\gstreamer_x64-windows\bin`  
or  
`C:\your_vcpkg_dir\packages\gstreamer_x64-windows\plugins\gstreamer`

Add GStreamer binaries to PATH. Typically:
```
C:\your_vcpkg_dir\installed\x64-windows\bin
```

**Step 3.** Install Boost libs.

```
vcpkg install boost-system boost-date-time boost-asio boost-regex boost-thread boost-signals2 boost-property-tree --triplet x64-windows
```

Optional:
```
vcpkg install boost-test --triplet x64-windows
```

**Step 4.** Configure and build
```
cd <path_to_repository_clone>
mkdir x64_build
cd x64_build
cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_TOOLCHAIN_FILE=C:/your_vcpkg_dir/scripts/buildsystems/vcpkg.cmake \
	-DVCPKG_TARGET_TRIPLET=x64-windows -G "Visual Studio 17 2022" -A "x64" ..
cmake --build .
```
You can also use `cmake-gui` to configure the project interactively.

### Building on Linux

**Step 1.** Install libgstrtspserver-1.0-dev  
**Step 2.** Install libboost-system-dev libboost-dev libboost-thread-dev

### Building Docker

A simple Docker-based workflow is provided to build and run the project in a reproducible environment. The repository includes a Dockerfile at docker/Dockerfile.

- Build the image (from repository root):
```
docker build -f docker/Dockerfile -t osrv-image:latest .
```

- Run the container (example: map common RTSP/HTTP ports and mount config/log directories):
```
docker run --rm -it \
	-p 554:554 -p 8554:8554 -p 8080:8080 \
	-v "$(pwd)/config":/app/config \
	-v "$(pwd)/logs":/app/logs \
	--name onvif-server \
	osrv-image:latest
```
Adjust published ports and mount points to match your runtime configuration.