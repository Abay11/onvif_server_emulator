1. Install GStreamer 1.16.3 runtime and add it to PATH: https://gstreamer.freedesktop.org/data/pkg/windows/1.16.3/

2. Open server_configs/common.config and specify services ports.  IPv4 address should be match your NIC address.

3. For proper work it's also required to copy gstrtponvif.dll to your GStreamer root directory(x86_64/lib/gstreamer-1.0)