#!/usr/bin/env python3
"""Isolated Stage 1 server. No emulator configuration or event state is used."""
import argparse
from datetime import datetime, timezone
import signal
import time

import gi
gi.require_version("Gst", "1.0")
gi.require_version("GstRtspServer", "1.0")
from gi.repository import GLib, Gst, GstRtspServer

TT = "http://www.onvif.org/ver10/schema"
WSNT = "http://docs.oasis-open.org/wsn/b-2"
TOPICS = "http://www.onvif.org/ver10/topics"
EMPTY = f'<tt:MetadataStream xmlns:tt="{TT}"/>'.encode()


def notification(sequence):
    # Independently authored simulation payload, deliberately larger than MTU.
    now = datetime.now(timezone.utc).isoformat(timespec="milliseconds").replace("+00:00", "Z")
    return (f'<tt:MetadataStream xmlns:tt="{TT}" xmlns:wsnt="{WSNT}" '
            f'xmlns:tns1="{TOPICS}"><tt:Event><wsnt:NotificationMessage>'
            '<wsnt:Topic Dialect="http://www.onvif.org/ver10/tev/topicExpression/ConcreteSet">'
            'tns1:Device/Trigger/DigitalInput</wsnt:Topic><wsnt:Message>'
            f'<tt:Message UtcTime="{now}" PropertyOperation="Changed">'
            '<tt:Source><tt:SimpleItem Name="InputToken" Value="PrototypeInput"/></tt:Source>'
            '<tt:Data><tt:SimpleItem Name="LogicalState" Value="true"/>'
            f'<tt:SimpleItem Name="Sequence" Value="{sequence}"/>'
            f'<tt:SimpleItem Name="Padding" Value="{"x" * 16384}"/>'
            '</tt:Data></tt:Message></wsnt:Message></wsnt:NotificationMessage>'
            '</tt:Event></tt:MetadataStream>').encode()


class Factory(GstRtspServer.RTSPMediaFactory):
    def __init__(self, with_video):
        super().__init__()
        video = ("videotestsrc is-live=true ! video/x-raw,width=160,height=120,framerate=5/1 "
                 "! x264enc tune=zerolatency speed-preset=ultrafast ! rtph264pay name=pay0 pt=96 "
                 if with_video else "")
        index = 1 if with_video else 0
        self.set_launch(f'( {video} appsrc name=metadata_src is-live=true format=time '
                        'do-timestamp=true block=false max-bytes=65536 '
                        'caps=application/x-onvif-metadata '
                        f'! rtponvifmetadatapay name=pay{index} pt=107 mtu=1200 )')
        self.set_shared(True)
        self.connect("media-configure", self.configure)

    def configure(self, factory, media):
        source = media.get_element().get_by_name("metadata_src")
        state = {"initialized": False, "start": time.monotonic(), "sequence": 0}

        def push(data):
            buffer = Gst.Buffer.new_wrapped(data)
            return source.emit("push-buffer", buffer) == Gst.FlowReturn.OK

        def need_data(src, length):
            # Preroll must not wait for a notification. An empty document carries
            # no invented event and is valid under the metadata schema.
            if not state["initialized"]:
                state["initialized"] = True
                push(EMPTY)

        connection = source.connect("need-data", need_data)

        def tick():
            # Leave events silent for three seconds after media construction.
            # Also bound the queue when media is paused or flushing.
            if source.get_property("current-level-bytes") < 32768:
                if time.monotonic() - state["start"] < 3:
                    push(EMPTY)
                else:
                    state["sequence"] += 1
                    push(notification(state["sequence"]))
            return GLib.SOURCE_CONTINUE

        timer = GLib.timeout_add(1000, tick)

        def unprepared(unused):
            GLib.source_remove(timer)
            source.disconnect(connection)

        media.connect("unprepared", unprepared)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="18554")
    args = parser.parse_args()
    Gst.init(None)
    for plugin in ("appsrc", "rtponvifmetadatapay", "x264enc"):
        if not Gst.ElementFactory.find(plugin):
            raise SystemExit(f"Missing GStreamer element: {plugin}")
    server = GstRtspServer.RTSPServer()
    server.set_address("127.0.0.1")
    server.set_service(args.port)
    mounts = server.get_mount_points()
    mounts.add_factory("/metadata", Factory(True))
    mounts.add_factory("/metadata-only", Factory(False))
    attach = server.attach(None)
    if not attach:
        raise SystemExit("Cannot bind prototype RTSP listener")
    loop = GLib.MainLoop()
    GLib.unix_signal_add(GLib.PRIORITY_DEFAULT, signal.SIGTERM, loop.quit)
    GLib.unix_signal_add(GLib.PRIORITY_DEFAULT, signal.SIGINT, loop.quit)
    print(f"READY rtsp://127.0.0.1:{args.port}/metadata", flush=True)
    try:
        loop.run()
    finally:
        GLib.source_remove(attach)
        server.get_session_pool().filter(lambda *unused: GstRtspServer.RTSPFilterResult.REMOVE)


if __name__ == "__main__":
    main()
