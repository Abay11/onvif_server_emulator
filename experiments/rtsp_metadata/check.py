#!/usr/bin/env python3
"""Receive actual RTP packets from the isolated metadata prototype."""
import argparse
from contextlib import ExitStack
from pathlib import Path
import re
import socket
import struct
import subprocess
import sys
import time
from urllib.parse import urljoin

from lxml import etree

from server import TT


class Client:
    def __init__(self, port, path, transport):
        self.url = f"rtsp://127.0.0.1:{port}/{path}"
        self.sock = socket.create_connection(("127.0.0.1", port), timeout=8)
        self.sock.settimeout(8)
        self.buffer = b""
        self.sequence = 0
        self.session = None
        self.receivers = []
        self.transport = transport
        self.payload_type = None
        self.channel = None
        self.rtp_socket = None

    def close(self):
        try:
            if self.session:
                self.request("TEARDOWN", self.url)
        finally:
            self.sock.close()
            for receiver in self.receivers:
                receiver.close()

    def read(self, count):
        while len(self.buffer) < count:
            chunk = self.sock.recv(65536)
            if not chunk:
                raise EOFError("RTSP connection closed")
            self.buffer += chunk
        result, self.buffer = self.buffer[:count], self.buffer[count:]
        return result

    def frame(self):
        prefix = self.read(4)
        assert prefix[0] == ord("$"), prefix
        return prefix[1], self.read(int.from_bytes(prefix[2:], "big"))

    def request(self, method, url, headers=""):
        self.sequence += 1
        session = f"Session: {self.session}\r\n" if self.session else ""
        self.sock.sendall((f"{method} {url} RTSP/1.0\r\nCSeq: {self.sequence}\r\n"
                           f"{session}{headers}\r\n").encode())
        # Playing sessions can interleave RTP before a command's response.
        while True:
            if not self.buffer:
                self.buffer = self.sock.recv(65536)
                if not self.buffer:
                    raise EOFError("RTSP response missing")
            if self.buffer.startswith(b"$"):
                self.frame()
                continue
            while b"\r\n\r\n" not in self.buffer:
                chunk = self.sock.recv(65536)
                if not chunk:
                    raise EOFError("RTSP response truncated")
                self.buffer += chunk
            raw, self.buffer = self.buffer.split(b"\r\n\r\n", 1)
            text = raw.decode()
            assert text.startswith("RTSP/1.0 200"), text
            length = re.search(r"Content-Length:\s*(\d+)", text, re.I)
            return text, self.read(int(length.group(1)) if length else 0)

    def start(self, video_only=False):
        started = time.monotonic()
        _, body = self.request("DESCRIBE", self.url, "Accept: application/sdp\r\n")
        elapsed = time.monotonic() - started
        assert elapsed < 2.5, f"DESCRIBE waited {elapsed:.2f}s for an event"
        sections = re.split(r"(?m)^m=", body.decode())[1:]
        metadata = [s for s in sections if s.startswith("application ")]
        assert len(metadata) == 1, body
        match = re.search(r"a=rtpmap:(\d+) VND.ONVIF.METADATA/90000", metadata[0], re.I)
        assert match, metadata
        self.payload_type = int(match.group(1))
        assert 96 <= self.payload_type <= 127
        section = next(s for s in sections if s.startswith("video ")) if video_only else metadata[0]
        control = re.search(r"(?m)^a=control:([^\r\n]+)", section).group(1)
        track_url = urljoin(self.url + "/", control)
        if self.transport == "tcp":
            transport = "RTP/AVP/TCP;unicast;interleaved=0-1"
            self.channel = 0
        else:
            for port in range(22000, 32000, 2):
                pair = [socket.socket(socket.AF_INET, socket.SOCK_DGRAM) for _ in range(2)]
                try:
                    for index, receiver in enumerate(pair):
                        receiver.bind(("127.0.0.1", port + index))
                        receiver.settimeout(8)
                    self.receivers = pair
                    self.rtp_socket = pair[0]
                    break
                except OSError:
                    for receiver in pair:
                        receiver.close()
            else:
                raise RuntimeError("No free UDP port pair")
            transport = f"RTP/AVP;unicast;client_port={port}-{port + 1}"
        headers, _ = self.request("SETUP", track_url, f"Transport: {transport}\r\n")
        self.session = re.search(r"Session:\s*([^;\r\n]+)", headers, re.I).group(1)
        if self.transport == "udp":
            assert re.search(r"server_port=\d+-\d+", headers), headers
        self.request("PLAY", self.url)
        return elapsed

    def packet(self):
        if self.transport == "udp":
            return self.rtp_socket.recv(65536)
        while True:
            channel, data = self.frame()
            if channel == self.channel:
                return data

    def document(self, schema, require_empty=False):
        chunks = []
        timestamp = None
        previous_sequence = None
        first_document = True
        deadline = time.monotonic() + 12
        while time.monotonic() < deadline:
            packet = self.packet()
            assert 12 <= len(packet) <= 1200
            assert packet[0] >> 6 == 2 and packet[1] & 127 == self.payload_type
            assert packet[0] & 0x3F == 0, "Unexpected RTP extension, padding or CSRC"
            sequence, current_timestamp = struct.unpack("!HI", packet[2:8])
            if previous_sequence is not None:
                assert sequence == (previous_sequence + 1) % 65536, "RTP packet loss/reordering"
            previous_sequence = sequence
            if timestamp is None:
                timestamp = current_timestamp
            assert timestamp == current_timestamp, "Timestamp changed within one XML document"
            chunks.append(packet[12:])
            if not packet[1] & 128:
                continue
            data = b"".join(chunks)
            count = len(chunks)
            chunks, timestamp = [], None
            # Shared streams may be joined in the middle of a document.
            if first_document and not data.startswith(b"<tt:MetadataStream"):
                first_document = False
                continue
            first_document = False
            tree = etree.fromstring(data)
            schema.assertValid(tree)
            assert tree.tag == f"{{{TT}}}MetadataStream"
            events = tree.findall(f"{{{TT}}}Event")
            if not events:
                if require_empty:
                    return None
                continue
            assert not require_empty, "Expected quiet initialization before notifications"
            items = {node.get("Name"): node.get("Value")
                     for node in tree.iter(f"{{{TT}}}SimpleItem")}
            assert items["InputToken"] == "PrototypeInput"
            assert items["LogicalState"] == "true"
            assert items["Padding"] == "x" * 16384
            assert count > 1, "Large document was not fragmented"
            message = next(tree.iter(f"{{{TT}}}Message"))
            assert message.get("UtcTime").endswith("Z")
            assert message.get("PropertyOperation") == "Changed"
            return int(items["Sequence"])
        raise TimeoutError("No complete metadata document received")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--contract", type=Path, required=True)
    parser.add_argument("--port", type=int, default=18554)
    args = parser.parse_args()
    from contract import load_schema
    schema = load_schema(args.contract)
    # Validate both the initialization and generated payload before networking.
    from server import EMPTY, notification
    schema.assertValid(etree.fromstring(EMPTY))
    schema.assertValid(etree.fromstring(notification(1)))
    process = subprocess.Popen([sys.executable, str(Path(__file__).with_name("server.py")),
                                "--port", str(args.port)])
    try:
        deadline = time.monotonic() + 10
        while True:
            try:
                with socket.create_connection(("127.0.0.1", args.port), timeout=0.2):
                    break
            except OSError:
                if process.poll() is not None or time.monotonic() >= deadline:
                    raise RuntimeError("Prototype did not start")
                time.sleep(0.1)
        for path in ("metadata", "metadata-only"):
            for transport in ("tcp", "udp"):
                with ExitStack() as stack:
                    first = Client(args.port, path, transport)
                    stack.callback(first.close)
                    elapsed = first.start()
                    first.document(schema, require_empty=True)
                    sequence = first.document(schema)
                    late = Client(args.port, path, transport)
                    stack.callback(late.close)
                    late.start()
                    late.document(schema)
                    assert first.document(schema) > sequence
                    print(f"PASS {path} {transport}: SDP {elapsed:.3f}s, quiet initialization, "
                          "fragmented schema-valid events, late join and first client continues", flush=True)
        for transport in ("tcp", "udp"):
            client = Client(args.port, "metadata", transport)
            try:
                client.start(video_only=True)
                assert client.packet()[0] >> 6 == 2
                print(f"PASS video-only SETUP {transport}", flush=True)
            finally:
                client.close()
    finally:
        process.terminate()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
            raise RuntimeError("Prototype failed to stop on SIGTERM")
        assert process.returncode == 0, f"Prototype exited with {process.returncode}"


if __name__ == "__main__":
    main()
