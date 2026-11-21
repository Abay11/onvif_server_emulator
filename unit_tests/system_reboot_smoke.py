#!/usr/bin/env python3
"""Start a source-built server with copied configs and verify ONVIF application reboot.

Run in an isolated network namespace (for example the documented dev container).
The test occupies the default service ports and HTTP 18080, and never edits source configs.
"""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request
import xml.etree.ElementTree as ET

SOAP = "http://www.w3.org/2003/05/soap-envelope"
DEVICE = "http://www.onvif.org/ver10/device/wsdl"
SCHEMA = None


def call(port, operation, credentials=None):
    url = f"http://127.0.0.1:{port}/onvif/device_service"
    payload = (f'<s:Envelope xmlns:s="{SOAP}" xmlns:d="{DEVICE}">'
               f'<s:Body><d:{operation}/></s:Body></s:Envelope>').encode()
    if SCHEMA is not None and operation == "SystemReboot":
        SCHEMA.validate(ET.tostring(ET.fromstring(payload).find(f"{{{SOAP}}}Body")[0]))
    opener = urllib.request.build_opener()
    if credentials:
        passwords = urllib.request.HTTPPasswordMgrWithDefaultRealm()
        passwords.add_password(None, url, *credentials)
        opener = urllib.request.build_opener(urllib.request.HTTPDigestAuthHandler(passwords))
    request = urllib.request.Request(url, payload, {
        "Content-Type": f'application/soap+xml; action="{DEVICE}/{operation}"',
        "Connection": "close",
    })
    with opener.open(request, timeout=5) as response:
        root = ET.fromstring(response.read())
        if SCHEMA is not None and operation == "SystemReboot":
            SCHEMA.validate(ET.tostring(root.find(f"{{{SOAP}}}Body")[0]))
        return root


def wait_for(process, port, manufacturer, credentials=None):
    deadline = time.monotonic() + 30
    while time.monotonic() < deadline:
        assert process.poll() is None, f"Server exited: {process.returncode}"
        try:
            root = call(port, "GetDeviceInformation", credentials)
            if root.find(f".//{{{DEVICE}}}Manufacturer").text == manufacturer:
                return
        except (OSError, urllib.error.URLError):
            pass
        time.sleep(0.1)
    raise AssertionError("Server did not reload the configuration")


def reboot(port, credentials=None):
    root = call(port, "SystemReboot", credentials)
    body = root.find(f"{{{SOAP}}}Body")
    assert len(body) == 1 and body[0].tag == f"{{{DEVICE}}}SystemRebootResponse"
    assert len(body[0]) == 1 and body[0][0].tag == f"{{{DEVICE}}}Message"
    assert body[0][0].text


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=Path("build/main"))
    parser.add_argument("--network-smoke", action="store_true")
    parser.add_argument("--contract-dir", type=Path, help="Prepared ONVIF 24.12 schema cache")
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[1]
    if args.contract_dir:
        sys.path.insert(0, str(repo / "experiments/rtsp_metadata"))
        from contract import checked_imports, verify_repository
        from lxml import etree
        from xmlschema import XMLSchema11
        root = args.contract_dir.resolve()
        verify_repository(root / "specs")
        path = root / "specs/wsdl/ver10/device/wsdl/devicemgmt.wsdl"
        wsdl = etree.parse(str(path), etree.XMLParser(no_network=True, resolve_entities=False))
        global SCHEMA
        SCHEMA = XMLSchema11(
            etree.tostring(wsdl.find(".//{http://www.w3.org/2001/XMLSchema}schema")),
            base_url=path.parent.as_uri() + "/", allow="local", use_fallback=False,
            uri_mapper={url: str(file.resolve()) for url, file in checked_imports(root).items()},
            validation="lax")
        # The pinned Device WSDL has bare text inside one xs:annotation. Permit
        # only that known schema-document error; payload validation stays strict.
        for error in SCHEMA.all_errors:
            if (error.path != "/xs:schema/xs:simpleType[2]/xs:annotation" or
                    error.message != "character data between child elements not allowed"):
                raise error
    with tempfile.TemporaryDirectory(prefix="onvif-reboot-") as directory:
        configs = Path(directory) / "configs"
        shutil.copytree(repo / "server_configs", configs)
        common_file = configs / "common.config"
        device_file = configs / "device.config"
        common = json.loads(common_file.read_text())
        device = json.loads(device_file.read_text())
        env = dict(os.environ, ONVIF_ADVERTISED_IPV4="127.0.0.1",
                   ONVIF_DISCOVERY_INTERFACE_IPV4="127.0.0.1")
        with (Path(directory) / "server.log").open("w+") as log:
            process = subprocess.Popen([str(args.binary.resolve()), str(configs)], stdout=log,
                                       stderr=subprocess.STDOUT, env=env)
            try:
                manufacturer = device["GetDeviceInformation"]["Manufacturer"]
                wait_for(process, 8080, manufacturer)
                pid = process.pid
                for index in range(2):
                    manufacturer = f"Reboot test {index}"
                    device["GetDeviceInformation"]["Manufacturer"] = manufacturer
                    device_file.write_text(json.dumps(device))
                    reboot(8080)
                    wait_for(process, 8080, manufacturer)
                    assert process.pid == pid and process.poll() is None
                    if args.network_smoke:
                        subprocess.run([sys.executable, str(repo / "unit_tests/network_smoke.py")],
                                       check=True)
                # Change the listener and enable authentication, then reload both.
                common["addresses"]["http_port"] = "18080"
                common["authentication"] = "digest"
                common_file.write_text(json.dumps(common))
                reboot(8080)
                wait_for(process, 18080, manufacturer, ("a", "a1"))
                for credentials in (None, ("u", "u1"), ("o", "o1")):
                    try:
                        reboot(18080, credentials)
                        raise AssertionError("Non-administrator reboot accepted")
                    except urllib.error.HTTPError as error:
                        assert error.code == (401 if credentials is None else 400)
                manufacturer = "Authenticated reboot"
                device["GetDeviceInformation"]["Manufacturer"] = manufacturer
                device_file.write_text(json.dumps(device))
                reboot(18080, ("a", "a1"))
                wait_for(process, 18080, manufacturer, ("a", "a1"))
                assert process.pid == pid and process.poll() is None
                print("PASS complete reboot response, repeated config reload, stable PID, listener reload, "
                      "Digest administrator reboot and user/operator rejection")
            except BaseException:
                log.flush()
                log.seek(0)
                print(log.read(), file=sys.stderr)
                raise
            finally:
                process.terminate()
                try:
                    process.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()


if __name__ == "__main__":
    main()
