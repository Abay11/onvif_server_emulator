#!/usr/bin/env python3
"""Exercise RTSP/RTP over paired HTTP and HTTPS connections using isolated configs."""
import argparse
import base64
import concurrent.futures
import json
from pathlib import Path
import re
import shutil
import socket
import ssl
import subprocess
import sys
import tempfile
import time
import uuid
import xml.etree.ElementTree as ET
from urllib.parse import urlsplit

from https_smoke import request, values
from network_smoke import Rtsp

ROOT = Path(__file__).resolve().parents[1]
CONTEXT = ssl._create_unverified_context()  # Only for the isolated development certificate.

SCHEMAS = {}
FAULT_SCHEMA = None
SOAP = 'http://www.w3.org/2003/05/soap-envelope'
NAMESPACES = {'media': 'http://www.onvif.org/ver10/media/wsdl',
              'media2': 'http://www.onvif.org/ver20/media/wsdl',
              'device': 'http://www.onvif.org/ver10/device/wsdl',
              'replay': 'http://www.onvif.org/ver10/replay/wsdl'}


def soap(scheme, port, service, operation, body='', context=None, expected=200):
    payload = (f'<s:Envelope xmlns:s="{SOAP}" xmlns:t="{NAMESPACES[service]}">'
               f'<s:Body><t:{operation}>{body}</t:{operation}></s:Body></s:Envelope>')
    if service in SCHEMAS and expected == 200:
        SCHEMAS[service].validate(ET.tostring(ET.fromstring(payload).find(f'{{{SOAP}}}Body')[0]))
    status, _, data = request(scheme, port, f'/onvif/{service}_service', payload,
                              {'Content-Type': 'application/soap+xml'}, context)
    assert status == expected, (status, data)
    result = ET.fromstring(data)
    if expected == 200 and service in SCHEMAS:
        SCHEMAS[service].validate(ET.tostring(result.find(f'{{{SOAP}}}Body')[0]))
    elif expected != 200 and FAULT_SCHEMA is not None:
        from lxml import etree
        FAULT_SCHEMA.assertValid(etree.fromstring(data))
    return result


class TunnelSocket:
    def __init__(self, scheme, port, path):
        self.sockets = []
        try:
            for _ in range(2):
                stream = socket.create_connection(('127.0.0.1', port), timeout=15)
                if scheme == 'https':
                    stream = CONTEXT.wrap_socket(stream, server_hostname='localhost')
                self.sockets.append(stream)
            self.reader, self.writer = self.sockets
            cookie = uuid.uuid4().hex
            self.reader.sendall((f'GET {path} HTTP/1.0\r\nx-sessioncookie: {cookie}\r\n'
                                 'Accept: application/x-rtsp-tunnelled\r\n\r\n').encode())
            header = b''
            while b'\r\n\r\n' not in header:
                data = self.reader.recv(65536)
                assert data, 'Tunnel GET closed before response'
                header += data
            header, self.pending = header.split(b'\r\n\r\n', 1)
            assert header.startswith(b'HTTP/1.0 200') or header.startswith(b'HTTP/1.1 200'), header
            assert b'application/x-rtsp-tunnelled' in header.lower(), header
            assert b'x-server-ip-address:' not in header.lower(), header
            # Leave the POST body open. Include the first RTSP command in the same write
            # to exercise bytes already read along with the HTTP header.
            self.post = (f'POST {path} HTTP/1.0\r\nx-sessioncookie: {cookie}\r\n'
                         'Content-Type: application/x-rtsp-tunnelled\r\nContent-Length: 32767\r\n\r\n').encode()
        except BaseException:
            self.close()
            raise

    def sendall(self, data):
        encoded = base64.b64encode(data)
        packet, self.post = self.post + encoded, b''
        # Fragment the header and base64 across writes; neither boundary is a message boundary.
        self.writer.sendall(packet[:17])
        self.writer.sendall(packet[17:])

    def recv(self, size):
        if self.pending:
            data, self.pending = self.pending[:size], self.pending[size:]
            return data
        data = self.reader.recv(size)
        assert data, 'Tunnel closed unexpectedly'
        return data

    def close(self):
        for stream in self.sockets:
            stream.close()


def stream(scheme, port, path, audio=True):
    client = Rtsp.__new__(Rtsp)
    client.socket = TunnelSocket(scheme, port, path)
    client.buffer, client.sequence = b'', 0
    try:
        uri = f'rtsp://127.0.0.1:{port}{path}'
        client.request('OPTIONS', uri)
        _, sdp = client.request('DESCRIBE', uri, 'Accept: application/sdp\r\n')
        assert b'm=video' in sdp and (not audio or b'm=audio' in sdp), sdp
        tracks = [track.decode().strip() for track in re.findall(rb'^a=control:(.+)\r?$', sdp, re.M)
                  if track.strip() != b'*']
        session = None
        for index, track in enumerate(tracks):
            headers = f'Transport: RTP/AVP/TCP;unicast;interleaved={2*index}-{2*index+1}\r\n'
            if session:
                headers += f'Session: {session}\r\n'
            response, _ = client.request('SETUP', track if '://' in track else uri + '/' + track, headers)
            session = re.search(r'Session:\s*([^;\r\n]+)', response, re.I).group(1)
        client.request('PLAY', uri, f'Session: {session}\r\n')
        channels, deadline = set(), time.monotonic() + 20
        expected = set(range(0, 2 * len(tracks), 2))
        while not expected.issubset(channels):
            assert time.monotonic() < deadline, channels
            while len(client.buffer) < 4:
                client.buffer += client.socket.recv(65536)
            assert client.buffer[0] == ord('$'), client.buffer[:50]
            channel, size = client.buffer[1], int.from_bytes(client.buffer[2:4], 'big')
            while len(client.buffer) < size + 4:
                client.buffer += client.socket.recv(65536)
            if channel in expected:
                assert size >= 12 and client.buffer[4] >> 6 == 2
                channels.add(channel)
            client.buffer = client.buffer[size+4:]
        print(f'PASS {scheme.upper()} tunnel: {len(tracks)} RTP tracks, fragmented POST and read-ahead')
    finally:
        client.socket.close()


def setup(protocol):
    return ('<t:StreamSetup xmlns:tt="http://www.onvif.org/ver10/schema"><tt:Stream>RTP-Unicast</tt:Stream>'
            f'<tt:Transport><tt:Protocol>{protocol}</tt:Protocol></tt:Transport></t:StreamSetup>')


def exercise(scheme, port):
    paths = []
    for service in ('media', 'media2'):
        body = '<t:Protocol>RtspOverHttp</t:Protocol>' if service == 'media2' else setup('HTTP')
        result = soap(scheme, port, service, 'GetStreamUri', body + '<t:ProfileToken>ProfileToken0</t:ProfileToken>', context=CONTEXT)
        uri = values(result, 'Uri')[0]
        assert uri.startswith(f'{scheme}://127.0.0.1:{port}/'), uri
        paths.append(urlsplit(uri).path)
        invalid = '<t:Protocol>HTTPS</t:Protocol>' if service == 'media2' else setup('HTTPS')
        fault = soap(scheme, port, service, 'GetStreamUri', invalid + '<t:ProfileToken>ProfileToken0</t:ProfileToken>', context=CONTEXT, expected=400)
        assert 'ter:InvalidStreamSetup' in values(fault, 'Value')
        direct = '<t:Protocol>RTSP</t:Protocol>' if service == 'media2' else setup('RTSP')
        assert values(soap(scheme, port, service, 'GetStreamUri', direct + '<t:ProfileToken>ProfileToken0</t:ProfileToken>', context=CONTEXT), 'Uri')[0].startswith('rtsp://127.0.0.1:8554/')
    stream(scheme, port, paths[0])
    replay_body = setup('HTTP') + '<t:RecordingToken>Recording_0</t:RecordingToken>'
    replay = soap(scheme, port, 'replay', 'GetReplayUri', replay_body, context=CONTEXT)
    assert values(replay, 'Uri') == [f'{scheme}://127.0.0.1:{port}/Recording0']
    stream(scheme, port, '/Recording0', audio=False)
    fault = soap(scheme, port, 'replay', 'GetReplayUri',
                 setup('HTTP') + '<t:RecordingToken>missing</t:RecordingToken>', context=CONTEXT, expected=400)
    assert 'ter:NoRecording' in values(fault, 'Value')
    # Verify normal routes still work after disconnecting an active tunnel.
    soap(scheme, port, 'device', 'GetServices', context=CONTEXT)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', default='build/main')
    parser.add_argument('--network-smoke', action='store_true')
    parser.add_argument('--gstreamer', action='store_true', help='Also test the native rtspsrc HTTP/HTTPS client')
    parser.add_argument('--contract', type=Path, help='Prepared pinned ONVIF 24.12 schema cache')
    args = parser.parse_args()
    binary = str(Path(args.binary).resolve())
    if args.contract:
        global FAULT_SCHEMA
        sys.path.insert(0, str(ROOT / 'experiments/rtsp_metadata'))
        from contract import load_media_schema, load_service_schema, load_schema
        SCHEMAS['media'] = load_media_schema(args.contract, False)
        SCHEMAS['media2'] = load_media_schema(args.contract, True)
        SCHEMAS['replay'] = load_service_schema(args.contract, 'wsdl/ver10/replay.wsdl')
        FAULT_SCHEMA = load_schema(args.contract, resource_faults=True)
    for metadata, http, https in ((True, True, True), (False, True, True), (True, False, True), (True, True, False)):
        with tempfile.TemporaryDirectory(prefix='osrv-tunnel-') as directory:
            config = Path(directory) / 'configs'
            shutil.copytree(ROOT / 'server_configs', config)
            common = json.loads((config / 'common.config').read_text())
            common.update(authentication='none', rtsp_metadata={'enabled': metadata}, http={'enabled': http},
                          https={'enabled': https, 'certificate': '', 'private_key': ''})
            common['advertised']['ipv4'] = '127.0.0.1'
            (config / 'common.config').write_text(json.dumps(common))
            discovery = json.loads((config / 'discovery.config').read_text())
            discovery['interface_ipv4'] = '127.0.0.1'
            (config / 'discovery.config').write_text(json.dumps(discovery))
            if not metadata:
                profiles = json.loads((config / 'media_profiles.config').read_text())
                for profile in profiles['MediaProfiles']:
                    profile.pop('Metadata', None)
                (config / 'media_profiles.config').write_text(json.dumps(profiles))
            with open(Path(directory) / 'server.log', 'w+') as log:
                process = subprocess.Popen([binary, str(config)], stdout=log, stderr=log)
                try:
                    for _ in range(100):
                        if process.poll() is not None:
                            raise RuntimeError('Server exited during startup')
                        try:
                            with socket.create_connection(('127.0.0.1', 8080 if http else 8443), timeout=.2):
                                break
                        except OSError:
                            time.sleep(.1)
                    print(f'Matrix: metadata={metadata}, HTTP={http}, HTTPS={https}')
                    if metadata:
                        soap('http' if http else 'https', 8080 if http else 8443, 'media',
                             'AddMetadataConfiguration', '<t:ProfileToken>ProfileToken0</t:ProfileToken>'
                             '<t:ConfigurationToken>MetadataToken0</t:ConfigurationToken>', context=CONTEXT)
                    with concurrent.futures.ThreadPoolExecutor() as executor:
                        futures = [executor.submit(exercise, scheme, port)
                                   for scheme, port, enabled in (('http', 8080, http), ('https', 8443, https)) if enabled]
                        for future in futures:
                            future.result()
                    if args.gstreamer and metadata and http and https:
                        for selector, port in (('rtsph', 8080), ('rtspsh', 8443)):
                            subprocess.run(['gst-launch-1.0', '-q', 'rtspsrc',
                                            f'location={selector}://127.0.0.1:{port}/Live&HighStream',
                                            'tls-validation-flags=0', '!', 'application/x-rtp,media=video',
                                            '!', 'identity', 'eos-after=10', '!', 'fakesink', 'sync=false'],
                                           check=True, timeout=30)
                            print(f'PASS GStreamer rtspsrc {selector}')
                    if args.network_smoke and metadata and http and https:
                        subprocess.run(['python3', str(ROOT / 'unit_tests/network_smoke.py')], check=True, timeout=90)
                except BaseException:
                    log.seek(0)
                    print(log.read()[-16000:])
                    raise
                finally:
                    process.terminate()
                    process.wait(timeout=10)
    print('PASS RTSP HTTP/HTTPS transport matrix')


if __name__ == '__main__':
    main()
