#!/usr/bin/env python3
"""Black-box checks against a running emulator with default service configuration.

Example (run in the same network namespace as the emulator):
  python3 unit_tests/network_smoke.py --advertised-ip 192.0.2.10
Requires Python 3 only. No firewall or host configuration is changed.
"""
import argparse
import http.client
import re
import socket
import uuid
import xml.etree.ElementTree as ET


def values(xml, name):
    return [node.text or "" for node in ET.fromstring(xml).iter()
            if node.tag.rsplit("}", 1)[-1] == name]


def soap(host, port, service, namespace, action, body=""):
    envelope = (f'<s:Envelope xmlns:s="http://www.w3.org/2003/05/soap-envelope" '
                f'xmlns:t="{namespace}"><s:Body><t:{action}>{body}</t:{action}>'
                '</s:Body></s:Envelope>')
    connection = http.client.HTTPConnection(host, port, timeout=10)
    try:
        connection.request("POST", f"/onvif/{service}_service", envelope,
                           {"Content-Type": "application/soap+xml"})
        response = connection.getresponse()
        data = response.read()
        assert response.status == 200, (action, response.status, data)
        assert not values(data, "Fault"), (action, data)
        return data
    finally:
        connection.close()


class Rtsp:
    def __init__(self, host, port):
        self.socket = socket.create_connection((host, port), timeout=15)
        self.socket.settimeout(15)
        self.buffer = b""
        self.sequence = 0

    def request(self, method, url, headers=""):
        self.sequence += 1
        self.socket.sendall((f"{method} {url} RTSP/1.0\r\nCSeq: {self.sequence}\r\n"
                             f"{headers}\r\n").encode())
        while b"\r\n\r\n" not in self.buffer:
            self.buffer += self.socket.recv(65536)
        raw, self.buffer = self.buffer.split(b"\r\n\r\n", 1)
        header = raw.decode()
        assert header.startswith("RTSP/1.0 200"), header
        length = re.search(r"Content-Length:\s*(\d+)", header, re.I)
        length = int(length.group(1)) if length else 0
        while len(self.buffer) < length:
            self.buffer += self.socket.recv(65536)
        body, self.buffer = self.buffer[:length], self.buffer[length:]
        return header, body


def udp_pair():
    for port in range(20000, 30000, 2):
        sockets = [socket.socket(socket.AF_INET, socket.SOCK_DGRAM) for _ in range(2)]
        try:
            for index, sock in enumerate(sockets):
                sock.bind(("0.0.0.0", port + index))
                sock.settimeout(15)
            return sockets, port
        except OSError:
            for sock in sockets:
                sock.close()
    raise RuntimeError("No free UDP client port pair")


def check_rtsp(args, transport):
    client = Rtsp(args.host, args.rtsp_port)
    receivers = []
    try:
        url = f"rtsp://{args.host}:{args.rtsp_port}/Live&HighStream"
        _, sdp = client.request("DESCRIBE", url, "Accept: application/sdp\r\n")
        assert b"m=video" in sdp and b"m=audio" in sdp, sdp
        controls = re.findall(rb"^a=control:(.+)\r?$", sdp, re.M)
        tracks = [item.decode().strip() for item in controls if item.strip() != b"*"]
        assert tracks, sdp
        session = None
        rtp_receivers = []
        for index, track in enumerate(tracks):
            if transport == "tcp":
                setup = f"RTP/AVP/TCP;unicast;interleaved={index * 2}-{index * 2 + 1}"
            else:
                pair, port = udp_pair()
                receivers.extend(pair)
                rtp_receivers.append(pair[0])
                setup = f"RTP/AVP;unicast;client_port={port}-{port + 1}"
            track_url = url + "/" + track if "://" not in track else track
            headers = f"Transport: {setup}\r\n"
            if session:
                headers += f"Session: {session}\r\n"
            header, _ = client.request("SETUP", track_url, headers)
            session = re.search(r"Session:\s*([^;\r\n]+)", header, re.I).group(1)
            if transport == "udp":
                ports = re.search(r"server_port=(\d+)-(\d+)", header)
                assert ports, header
                assert args.rtp_min <= int(ports.group(1)) < int(ports.group(2)) <= args.rtp_max, header
        client.request("PLAY", url, f"Session: {session}\r\n")
        if transport == "udp":
            for receiver in rtp_receivers:
                data, _ = receiver.recvfrom(65536)
                assert len(data) > 12 and data[0] >> 6 == 2, data[:12]
        else:
            channels = set()
            while not set(range(0, len(tracks) * 2, 2)).issubset(channels):
                while len(client.buffer) < 4:
                    client.buffer += client.socket.recv(65536)
                assert client.buffer[0] == ord("$"), client.buffer[:20]
                length = int.from_bytes(client.buffer[2:4], "big")
                while len(client.buffer) < length + 4:
                    client.buffer += client.socket.recv(65536)
                channels.add(client.buffer[1])
                client.buffer = client.buffer[length + 4:]
        print(f"PASS RTSP + live video and audio RTP/{transport.upper()}")
    finally:
        client.socket.close()
        for receiver in receivers:
            receiver.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--http-port", type=int, default=8080)
    parser.add_argument("--rtsp-port", type=int, default=8554)
    parser.add_argument("--advertised-ip", default="127.0.0.1")
    parser.add_argument("--advertised-http-port", type=int, default=8080)
    parser.add_argument("--advertised-rtsp-port", type=int, default=8554)
    parser.add_argument("--advertised-pullpoint-port", type=int, default=0)
    parser.add_argument("--rtp-min", type=int, default=50000)
    parser.add_argument("--rtp-max", type=int, default=50199)
    parser.add_argument("--multicast-interface", default="127.0.0.1")
    args = parser.parse_args()
    http_base = f"http://{args.advertised_ip}:{args.advertised_http_port}/"
    rtsp_base = f"rtsp://{args.advertised_ip}:{args.advertised_rtsp_port}/"
    device_ns = "http://www.onvif.org/ver10/device/wsdl"
    for action, body in [("GetCapabilities", "<t:Category>All</t:Category>"),
                         ("GetServices", "<t:IncludeCapability>false</t:IncludeCapability>")]:
        addresses = values(soap(args.host, args.http_port, "device", device_ns, action, body), "XAddr")
        assert addresses and all(addr.startswith(http_base) for addr in addresses), addresses
        print("PASS", action, "advertised XAddrs")
    for service, ns in [("media", "http://www.onvif.org/ver10/media/wsdl"),
                        ("media2", "http://www.onvif.org/ver20/media/wsdl")]:
        body = "<t:ProfileToken>ProfileToken0</t:ProfileToken>"
        for action, expected in [("GetStreamUri", rtsp_base + "Live&HighStream"),
                                 ("GetSnapshotUri", http_base + "snapshot.jpeg")]:
            assert values(soap(args.host, args.http_port, service, ns, action, body), "Uri") == [expected]
            print("PASS", service, action)
    assert values(soap(args.host, args.http_port, "replay", "http://www.onvif.org/ver10/replay/wsdl",
                       "GetReplayUri", "<t:RecordingToken>RecordingToken0</t:RecordingToken>"), "Uri") == [rtsp_base + "Recording0"]
    event = soap(args.host, args.http_port, "event", "http://www.onvif.org/ver10/events/wsdl",
                 "CreatePullPointSubscription")
    pullpoint_base = (f"http://{args.advertised_ip}:{args.advertised_pullpoint_port}/"
                      if args.advertised_pullpoint_port else http_base)
    assert any(addr.startswith(pullpoint_base) for addr in values(event, "Address")), event
    print("PASS replay and PullPoint advertised URLs")
    connection = http.client.HTTPConnection(args.host, args.http_port, timeout=5)
    connection.request("GET", "/snapshot.jpeg")
    response = connection.getresponse()
    assert response.status == 200 and response.read().startswith(b"\xff\xd8")
    connection.close()
    print("PASS snapshot download")
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as discovery:
        discovery.bind((args.multicast_interface, 0))
        discovery.settimeout(5)
        discovery.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_IF,
                             socket.inet_aton(args.multicast_interface))
        # Malformed and empty datagrams must not stop discovery.
        for malformed in (b"", b"not XML", b"<Envelope/>"):
            discovery.sendto(malformed, (args.host, 3702))
        for destination in (args.host, "239.255.255.250"):
            identifier = f"urn:uuid:{uuid.uuid4()}"
            probe = (f'<s:Envelope xmlns:s="http://www.w3.org/2003/05/soap-envelope" '
                     'xmlns:a="http://schemas.xmlsoap.org/ws/2004/08/addressing" '
                     'xmlns:d="http://schemas.xmlsoap.org/ws/2005/04/discovery">'
                     f'<s:Header><a:MessageID>{identifier}</a:MessageID>'
                     '<a:Action>http://schemas.xmlsoap.org/ws/2005/04/discovery/Probe</a:Action>'
                     '</s:Header><s:Body><d:Probe/></s:Body></s:Envelope>')
            discovery.sendto(probe.encode(), (destination, 3702))
            reply, _ = discovery.recvfrom(65536)
            assert values(reply, "RelatesTo") == [identifier], reply
            assert values(reply, "XAddrs") == [http_base + "onvif/device_service"], reply
            print("PASS discovery", destination)
    for transport in ("tcp", "udp"):
        check_rtsp(args, transport)


if __name__ == "__main__":
    main()
