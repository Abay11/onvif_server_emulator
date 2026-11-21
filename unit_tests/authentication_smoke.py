#!/usr/bin/env python3
"""Exercise HTTP Digest and WS-Security against source-built SOAP and PullPoint endpoints."""
import argparse
import base64
import datetime
import hashlib
import http.client
import json
from pathlib import Path
import re
import secrets
import shutil
import subprocess
import tempfile
import time
import xml.etree.ElementTree as ET
from xml.sax.saxutils import escape

SOAP = "http://www.w3.org/2003/05/soap-envelope"
DEVICE = "http://www.onvif.org/ver10/device/wsdl"
EVENT = "http://www.onvif.org/ver10/events/wsdl"
WSSE = "http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-wssecurity-secext-1.0.xsd"
WSU = "http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-wssecurity-utility-1.0.xsd"
PASSWORD = "http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-username-token-profile-1.0#"


def token(login="a", password="a1", text=False, created=None, nonce=None, namespace=WSSE):
    created = created or datetime.datetime.now(datetime.timezone.utc).isoformat(timespec="milliseconds").replace("+00:00", "Z")
    nonce = nonce or secrets.token_bytes(20)
    value = password if text else base64.b64encode(hashlib.sha1(nonce + created.encode() + password.encode()).digest()).decode()
    return (f'<w:Security xmlns:w="{namespace}" xmlns:u="{WSU}"><w:UsernameToken><w:Username>{login}</w:Username>'
            f'<w:Password Type="{PASSWORD}Password{"Text" if text else "Digest"}">{escape(value)}</w:Password>'
            f'<w:Nonce>{base64.b64encode(nonce).decode()}</w:Nonce><u:Created>{created}</u:Created>'
            '</w:UsernameToken></w:Security>')


def envelope(operation, namespace=DEVICE, security=""):
    return (f'<s:Envelope xmlns:s="{SOAP}" xmlns:d="{namespace}"><s:Header>{security}</s:Header>'
            f'<s:Body>{operation}</s:Body></s:Envelope>').encode()


def call(body, path="/onvif/device_service", authorization=None):
    connection = http.client.HTTPConnection("127.0.0.1", 8080, timeout=6)
    headers = {"Content-Type": "application/soap+xml"}
    if authorization:
        headers["Authorization"] = authorization
    connection.request("POST", path, body, headers)
    response = connection.getresponse()
    result = response.status, response.getheaders(), response.read()
    connection.close()
    return result


def authorize(challenge, algorithm, login="a", password="a1", path="/onvif/device_service", method="POST", nc="00000001"):
    values = dict(re.findall(r'(\w+)="([^"]*)"', challenge))
    base = algorithm.removesuffix("-sess")
    name = {"MD5": "md5", "SHA-256": "sha256", "SHA-512-256": "sha512_256"}[base]
    def h(value):
        return hashlib.new(name, value.encode()).hexdigest()
    client = secrets.token_hex(16)
    first = h(f'{login}:{values["realm"]}:{password}')
    if algorithm.endswith("-sess"):
        first = h(f'{first}:{values["nonce"]}:{client}')
    result = h(f'{first}:{values["nonce"]}:{nc}:{client}:auth:{h(f"{method}:{path}")}')
    return (f'Digest username="{login}", realm="{values["realm"]}", nonce="{values["nonce"]}", '
            f'uri="{path}", algorithm={algorithm}, qop=auth, nc={nc}, cnonce="{client}", response="{result}"')


def check_not_authorized(result):
    status, headers, body = result
    assert status == 400, result
    assert not any(key.lower() == "www-authenticate" for key, _ in headers), headers
    fault = ET.fromstring(body).find(f"{{{SOAP}}}Body/{{{SOAP}}}Fault")
    assert fault is not None, body
    assert fault.find(f"{{{SOAP}}}Code/{{{SOAP}}}Value").text == "s:Sender"
    assert fault.find(f"{{{SOAP}}}Code/{{{SOAP}}}Subcode/{{{SOAP}}}Value").text == "ter:NotAuthorized"


def check_role_permissions(configs, mechanism):
    def invoke(operation, namespace, path, login, password):
        payload = envelope(operation, namespace, token(login, password) if mechanism == "ws-security" else "")
        if mechanism == "ws-security":
            return call(payload, path)
        denied = call(payload, path)
        assert denied[0] == 401, denied
        challenge = next(v for k, v in denied[1] if k.lower() == "www-authenticate")
        return call(payload, path, authorize(challenge, "SHA-256", login, password, path))

    device_path = "/onvif/device_service"
    media_path = "/onvif/media2_service"
    media_ns = "http://www.onvif.org/ver20/media/wsdl"
    profile_file = configs / "media_profiles.config"
    for login, password in (("u", "u1"), ("o", "o1"), ("a", "a1")):
        assert invoke("<d:GetDeviceInformation/>", DEVICE, device_path, login, password)[0] == 200
        assert invoke("<d:GetProfiles/>", media_ns, media_path, login, password)[0] == 200
        before = profile_file.read_bytes()
        operation = f"<d:CreateProfile><d:Name>role-test-{login}</d:Name></d:CreateProfile>"
        result = invoke(operation, media_ns, media_path, login, password)
        if login == "u":
            check_not_authorized(result)
            assert profile_file.read_bytes() == before, "Denied profile creation modified persisted settings"
            # Even malformed requests must be denied before invoking the operation handler.
            check_not_authorized(invoke("<d:CreateProfile/>", media_ns, media_path, login, password))
        else:
            assert result[0] == 200, result
            profile = ET.fromstring(result[2]).find(f".//{{{media_ns}}}Token")
            assert profile is not None, result
            assert profile_file.read_bytes() != before
            result = invoke(f"<d:DeleteProfile><d:Token>{profile.text}</d:Token></d:DeleteProfile>",
                            media_ns, media_path, login, password)
            assert result[0] == 200, result
        if login != "a":
            check_not_authorized(invoke("<d:SystemReboot/>", DEVICE, device_path, login, password))
            assert invoke("<d:GetDeviceInformation/>", DEVICE, device_path, login, password)[0] == 200
    print(f"PASS {mechanism}: role permissions, SOAP NotAuthorized and denied operations have no side effects")


def run(binary):
    root = Path(__file__).resolve().parents[1]
    request = envelope("<d:GetDeviceInformation/>")
    with tempfile.TemporaryDirectory(prefix="onvif-auth-") as directory:
        configs = Path(directory) / "configs"
        shutil.copytree(root / "server_configs", configs)
        common = json.loads((configs / "common.config").read_text())
        common["rtsp_metadata"]["enabled"] = False
        common["networkDelaySimulation"]["milliseconds"] = 0
        users = json.loads((configs / "users.config").read_text())
        users["users"].append({"login": "disabled", "password": "disabled", "type": "administrator", "enabled": False})
        (configs / "users.config").write_text(json.dumps(users))
        for mode in ("digest", "ws-security", "digest/ws-security"):
            common["authentication"] = mode
            (configs / "common.config").write_text(json.dumps(common))
            with (Path(directory) / "server.log").open("w+") as log:
                process = subprocess.Popen([str(binary.resolve()), str(configs)], stdout=log, stderr=subprocess.STDOUT)
                try:
                    for _ in range(150):
                        assert process.poll() is None, "Server exited during startup"
                        try:
                            status, headers, body = call(request)
                            break
                        except OSError:
                            time.sleep(.1)
                    else:
                        raise AssertionError("Server did not start")
                    challenges = [value for key, value in headers if key.lower() == "www-authenticate"]
                    assert status == (400 if mode == "ws-security" else 401), (mode, status, body)
                    if mode != "ws-security":
                        assert [re.search(r'algorithm=([^, ]+)', c)[1] for c in challenges] == ["MD5", "SHA-256", "SHA-512-256"]
                        for algorithm in ("MD5", "SHA-256", "SHA-512-256", "MD5-sess", "SHA-256-sess", "SHA-512-256-sess"):
                            header = authorize(challenges[0], algorithm)
                            assert call(request, authorization=header)[0] == 200, algorithm
                            assert call(request, authorization=header)[0] == 401, "Digest replay accepted"
                            assert call(request, authorization=authorize(challenges[0], algorithm, password="wrong"))[0] == 401
                            assert call(request, authorization=authorize(challenges[0], algorithm, method="GET"))[0] == 401
                            assert call(request, authorization=authorize(challenges[0], algorithm, path="/other"))[0] == 401
                        assert call(request, authorization=authorize(challenges[0], "MD5", "disabled", "disabled"))[0] == 401
                        reboot = envelope("<d:SystemReboot/>")
                        assert call(reboot, authorization=authorize(challenges[0], "MD5", "u", "u1"))[0] == 400
                    # PRE_AUTH remains reachable without credentials in every mode.
                    assert call(envelope("<d:GetSystemDateAndTime/>"))[0] == 200
                    caps = ET.fromstring(call(envelope("<d:GetServiceCapabilities/>"))[2])
                    security = caps.find(f'.//{{{DEVICE}}}Security')
                    assert security.attrib["UsernameToken"] == str(mode != "digest").lower()
                    assert security.attrib["HttpDigest"] == str(mode != "ws-security").lower()
                    if mode != "digest":
                        for text in (False, True):
                            signed = envelope("<d:GetDeviceInformation/>", security=token(text=text))
                            assert call(signed)[0] == 200
                            assert call(signed)[0] == 400, "UsernameToken replay accepted"
                        for invalid in (token(password="wrong"), token(created="2000-01-01T00:00:00Z"),
                                        token(namespace="urn:wrong"), token("disabled", "disabled")):
                            assert call(envelope("<d:GetDeviceInformation/>", security=invalid))[0] in (400, 401)
                        if mode == "digest/ws-security":
                            both = envelope("<d:GetDeviceInformation/>", security=token(password="wrong"))
                            assert call(both, authorization=authorize(challenges[0], "MD5"))[0] == 400
                    for mechanism in (("digest", "ws-security") if mode == "digest/ws-security" else (mode,)):
                        check_role_permissions(configs, mechanism)
                    # Events use a separate dispatcher; PullPoint inherits the same authentication policy.
                    path = "/onvif/event_service"
                    event_request = envelope("<d:CreatePullPointSubscription/>", EVENT,
                                             token() if mode != "digest" else "")
                    if mode == "digest":
                        event_challenge = next(v for k, v in call(event_request, path)[1] if k.lower() == "www-authenticate")
                        result = call(event_request, path, authorize(event_challenge, "SHA-256", path=path))
                    else:
                        result = call(event_request, path)
                    assert result[0] == 200, result
                    xml = ET.fromstring(result[2])
                    address = xml.find('.//{http://www.w3.org/2005/08/addressing}Address').text
                    from urllib.parse import urlsplit
                    subscription = urlsplit(address).path
                    addressing = (f'<wsa:Action xmlns:wsa="http://www.w3.org/2005/08/addressing">'
                                  'http://docs.oasis-open.org/wsn/bw-2/SubscriptionManager/UnsubscribeRequest</wsa:Action>'
                                  f'<wsa:To xmlns:wsa="http://www.w3.org/2005/08/addressing">{address}</wsa:To>')
                    unsigned = envelope("<d:Unsubscribe/>", "http://docs.oasis-open.org/wsn/b-2", addressing)
                    assert call(unsigned, subscription)[0] == (400 if mode == "ws-security" else 401)
                    if mode == "digest":
                        challenge = next(v for k, v in call(unsigned, subscription)[1] if k.lower() == "www-authenticate")
                        result = call(unsigned, subscription, authorize(challenge, "SHA-512-256", path=subscription))
                    else:
                        result = call(envelope("<d:Unsubscribe/>", "http://docs.oasis-open.org/wsn/b-2", addressing + token()), subscription)
                    assert result[0] == 200, result
                    print(f"PASS {mode}: credentials, disabled accounts, replay, roles, capabilities, Events/PullPoint")
                except BaseException:
                    log.flush(); log.seek(0); print(log.read())
                    raise
                finally:
                    process.terminate()
                    try:
                        process.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        process.kill(); process.wait()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=Path("build/main"))
    run(parser.parse_args().binary)
