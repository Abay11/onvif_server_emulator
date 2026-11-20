#!/usr/bin/env python3
"""Check live RTSP event metadata against a running emulator with metadata enabled."""
import argparse
from contextlib import ExitStack
from pathlib import Path
import re
import select
import socket
import struct
import sys
import time
from urllib.parse import urljoin
import xml.etree.ElementTree as ET

from event_delivery_smoke import call, pull, A, E, W, T


class Receiver:
    def __init__(self, host, port, path, transport, mode="metadata", validate=None):
        self.url = f"rtsp://{host}:{port}/{path}"
        self.socket = socket.create_connection((host, port), timeout=10)
        self.socket.settimeout(10)
        self.buffer = b""
        self.sequence = 0
        self.session = None
        self.transport = transport
        self.sockets = {}
        self.tracks = {}
        self.validate = validate
        self.parts = []
        self.doc_timestamp = None
        self.previous_sequence = None
        self.fragmented = False
        self.rtcp = False
        self.seen_media = set()
        self.last_timestamp = None
        self.timestamp_changes = 0
        started = time.monotonic()
        header, sdp = self.request("DESCRIBE", self.url, "Accept: application/sdp\r\n")
        assert time.monotonic() - started < 8, "DESCRIBE waited for an event"
        sections = re.split(r"(?m)^m=", sdp.decode())[1:]
        metadata = [section for section in sections if section.startswith("application ")]
        assert len(metadata) == 1, sdp
        mapping = re.search(r"a=rtpmap:(\d+) VND.ONVIF.METADATA/90000", metadata[0], re.I)
        assert mapping, metadata
        self.metadata_payload = int(mapping.group(1))
        assert 96 <= self.metadata_payload <= 127
        selected = metadata if mode == "metadata" else [s for s in sections if not s.startswith("application ")]
        if mode == "all": selected = sections
        for index, section in enumerate(selected):
            media = section.split()[0]
            control = re.search(r"(?m)^a=control:([^\r\n]+)", section).group(1)
            base = re.search(r"Content-Base:\s*([^\r\n]+)", header, re.I)
            url = urljoin(base.group(1) if base else self.url + "/", control)
            if transport == "tcp":
                setup = f"RTP/AVP/TCP;unicast;interleaved={index * 2}-{index * 2 + 1}"
            else:
                for port in range(24000, 34000, 2):
                    pair = [socket.socket(socket.AF_INET, socket.SOCK_DGRAM) for _ in range(2)]
                    try:
                        for offset, sock in enumerate(pair):
                            sock.bind(("0.0.0.0", port + offset))
                        break
                    except OSError:
                        for sock in pair: sock.close()
                else: raise RuntimeError("No free UDP port pair")
                self.sockets[pair[0]] = (media, False)
                self.sockets[pair[1]] = (media, True)
                setup = f"RTP/AVP;unicast;client_port={port}-{port + 1}"
            self.tracks[index * 2] = (media, False)
            self.tracks[index * 2 + 1] = (media, True)
            response, _ = self.request("SETUP", url, f"Transport: {setup}\r\n")
            self.session = re.search(r"Session:\s*([^;\r\n]+)", response, re.I).group(1)
            if transport == "udp": assert re.search(r"server_port=\d+-\d+", response), response
        self.request("PLAY", self.url)

    def read(self, count):
        while len(self.buffer) < count:
            chunk = self.socket.recv(65536)
            if not chunk: raise EOFError("RTSP connection closed")
            self.buffer += chunk
        result, self.buffer = self.buffer[:count], self.buffer[count:]
        return result

    def frame(self):
        header = self.read(4)
        assert header[0] == ord("$"), header
        return header[1], self.read(int.from_bytes(header[2:], "big"))

    def request(self, method, url, headers=""):
        self.sequence += 1
        session = f"Session: {self.session}\r\n" if self.session else ""
        self.socket.sendall((f"{method} {url} RTSP/1.0\r\nCSeq: {self.sequence}\r\n"
                             f"{session}{headers}\r\n").encode())
        while True:
            if not self.buffer:
                self.buffer = self.socket.recv(65536)
                if not self.buffer: raise EOFError("RTSP response missing")
            if self.buffer.startswith(b"$"):
                self.frame()
                continue
            while b"\r\n\r\n" not in self.buffer:
                data = self.socket.recv(65536)
                if not data: raise EOFError("RTSP response truncated")
                self.buffer += data
            raw, self.buffer = self.buffer.split(b"\r\n\r\n", 1)
            header = raw.decode()
            assert header.startswith("RTSP/1.0 200"), header
            length = re.search(r"Content-Length:\s*(\d+)", header, re.I)
            return header, self.read(int(length.group(1)) if length else 0)

    def packet(self):
        if self.transport == "tcp":
            channel, data = self.frame()
            media, rtcp = self.tracks[channel]
        else:
            ready, _, _ = select.select(list(self.sockets), [], [], 10)
            if not ready: raise TimeoutError("No UDP RTP/RTCP")
            # Drain control traffic even when accumulated RTP is also readable.
            selected = next((sock for sock in ready if self.sockets[sock][1]), ready[0])
            media, rtcp = self.sockets[selected]
            data = selected.recv(65536)
        if rtcp:
            # RTCP can contain a compound report. Check the sender clock fields.
            offset = 0
            while offset + 4 <= len(data):
                length = (int.from_bytes(data[offset + 2:offset + 4], "big") + 1) * 4
                assert length >= 4 and offset + length <= len(data)
                if media == "application" and data[offset + 1] == 200:
                    assert length >= 28
                    ntp_seconds = int.from_bytes(data[offset + 8:offset + 12], "big")
                    assert ntp_seconds > 2208988800
                    self.rtcp = True
                offset += length
            return None
        assert len(data) >= 12 and data[0] >> 6 == 2
        self.seen_media.add(media)
        if media != "application": return None
        assert len(data) <= 1200 and data[1] & 127 == self.metadata_payload
        assert data[0] & 0x3F == 0, "Unexpected metadata CSRC, padding or extension"
        sequence, timestamp = struct.unpack("!HI", data[2:8])
        if self.previous_sequence is not None:
            assert sequence == (self.previous_sequence + 1) % 65536, "Metadata RTP loss/reordering"
        self.previous_sequence = sequence
        if self.doc_timestamp is None: self.doc_timestamp = timestamp
        assert self.doc_timestamp == timestamp, "Timestamp changed inside one XML document"
        self.parts.append(data[12:])
        if not data[1] & 128: return None
        raw = b"".join(self.parts)
        fragments = len(self.parts)
        self.parts, self.doc_timestamp = [], None
        # A shared stream may be joined halfway through a document.
        if not raw.lstrip().startswith((b"<?xml", b"<tt:MetadataStream")): return None
        root = ET.fromstring(raw)
        assert root.tag == f"{{{T}}}MetadataStream"
        if self.validate: self.validate(raw)
        self.fragmented |= fragments > 1
        if self.last_timestamp is not None:
            delta = (timestamp - self.last_timestamp) % (2 ** 32)
            assert delta < 90000 * 15, "Metadata timestamps moved backwards or jumped"
            if delta: self.timestamp_changes += 1
        self.last_timestamp = timestamp
        return root

    def document(self, initialized=False, changed=False):
        deadline = time.monotonic() + 15
        while time.monotonic() < deadline:
            root = self.packet()
            if root is None: continue
            messages = root.findall(f".//{{{W}}}NotificationMessage")
            if not initialized and not changed: return root
            for message in messages:
                operation = message.find(f".//{{{T}}}Message").get("PropertyOperation")
                if (initialized and operation == "Initialized") or (changed and operation != "Initialized"):
                    return root
        raise TimeoutError("No expected metadata document")

    def close(self):
        try:
            if self.session: self.request("TEARDOWN", self.url)
        finally:
            self.socket.close()
            for sock in self.sockets: sock.close()


def identity(messages):
    return {ET.tostring(message) for message in messages
            if message.find(f".//{{{T}}}Message").get("PropertyOperation") != "Initialized"}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--http-port", type=int, default=8080)
    parser.add_argument("--rtsp-port", type=int, default=8554)
    parser.add_argument("--contract", type=Path)
    args = parser.parse_args()
    validate = None
    if args.contract:
        from lxml import etree
        sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "experiments/rtsp_metadata"))
        from contract import load_schema
        schema = load_schema(args.contract)
        validate = lambda data: schema.assertValid(etree.fromstring(data))
    endpoint = f"http://{args.host}:{args.http_port}/onvif/event_service"
    subscriptions = []
    try:
        with ExitStack() as stack:
            create = lambda path, transport, mode="metadata": Receiver(
                args.host, args.rtsp_port, path, transport, mode, validate)
            first = create("Live&HighStream", "tcp")
            stack.callback(first.close)
            first.document(initialized=True)
            late = create("Live&HighStream", "udp")
            stack.callback(late.close)
            late.document(initialized=True)
            for _ in range(2):
                subscriptions.append(call(endpoint, "CreatePullPointSubscription").find(f".//{{{A}}}Address").text)
            seen = [set(), set(), set(), set()]
            for _ in range(5):
                for index, subscription in enumerate(subscriptions): seen[index].update(identity(pull(subscription)))
                for index, receiver in enumerate((first, late), 2):
                    seen[index].update(identity(receiver.document(changed=True).findall(f".//{{{W}}}NotificationMessage")))
                if set.intersection(*seen): break
            assert set.intersection(*seen), "No identical notification reached both RTSP and PullPoint clients"
            assert first.fragmented or late.fragmented, "No multi-packet metadata document received"
            deadline = time.monotonic() + 15
            while not (first.rtcp and late.rtcp) and time.monotonic() < deadline:
                first.document()
                late.document()
            assert first.rtcp and late.rtcp, f"Metadata RTCP sender reports missing: TCP={first.rtcp}, UDP={late.rtcp}"
            assert first.timestamp_changes and late.timestamp_changes
            first.request("PAUSE", first.url)
            late.document(changed=True)
            first.parts, first.doc_timestamp, first.previous_sequence = [], None, None
            first.request("PLAY", first.url)
            first.document(initialized=True)
            print("PASS shared high stream: TCP/UDP, late property state, fragmented XML, RTCP, "
                  "monotonic timestamps, pause/resume and two concurrent PullPoints", flush=True)
            for path in ("Live&HighStream", "Live&LowStream"):
                for transport in ("tcp", "udp"):
                    receiver = create(path, transport, "all")
                    try:
                        receiver.document(initialized=True)
                        expected = {"application", "video"} | ({"audio"} if path == "Live&HighStream" else set())
                        end = time.monotonic() + 10
                        while not expected <= receiver.seen_media and time.monotonic() < end: receiver.packet()
                        assert expected <= receiver.seen_media
                        print(f"PASS {path} aggregate {transport}", flush=True)
                    finally: receiver.close()
                    receiver = create(path, transport, "av")
                    try:
                        end = time.monotonic() + 10
                        expected.discard("application")
                        while not expected <= receiver.seen_media and time.monotonic() < end: receiver.packet()
                        assert expected <= receiver.seen_media
                        print(f"PASS {path} audio/video selection {transport}", flush=True)
                    finally: receiver.close()
        # Last-client teardown must permit a newly constructed media pipeline.
        for path in ("Live&HighStream", "Live&LowStream"):
            receiver = create(path, "tcp")
            try: receiver.document(initialized=True)
            finally: receiver.close()
        print("PASS reconnect after last-client teardown", flush=True)
    finally:
        for subscription in subscriptions: call(subscription, "Unsubscribe")


if __name__ == "__main__":
    main()
