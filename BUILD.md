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
vcpkg install --triplet x64-windows gstreamer gstreamer[plugins-base] gstreamer[plugins-good] \
	gstreamer[plugins-ugly] gstreamer[plugins-bad] gstreamer[pango] gstreamer[x264] gstreamer[x265] gst-rtsp-server
```

Make sure the installation was completed successfully with command: 
```
vcpkg list | findstr "gst"
``` 
All required GStreamer dependencies should be listed.



**Step 2.** For proper work GStreamer applications require to point binaries and plugin directories.

Add the `GST_PLUGIN_PATH` variable system environment. Usually install it here:
```
C:\your_vcpkg_dir\packages\gstreamer_x64-windows\bin
```
Add GStreamer binaries to PATH. Usually it installed here:
```
C:\your_vcpkg_dir\installed\x64-windows\bin
```


**Step 3.** Install Boost libs.

```
vcpkg install boost-system boost-date-time boost-asio boost-regex boost-thread signals2 --triplet x64-window`
```

Optional:
```
vcpkg install boost-test --triplet x64-window
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
It also possible to use `cmake-gui` and configure all there.

*TODO BUILD ON LINUX*
