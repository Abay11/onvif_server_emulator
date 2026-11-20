#!/usr/bin/env python3
"""Verify persisted event settings and stable RTSP URIs across fresh processes.

Starts the source-built binary on separate loopback ports with copied configs.
Requires the metadata payloader. No host configuration or source config is changed.
"""
import argparse
import hashlib
from copy import deepcopy
import json
from pathlib import Path
import shutil
import socket
import subprocess
import tempfile
import time
import xml.etree.ElementTree as ET

from metadata_configuration_smoke import soap, M, T
from rtsp_metadata_smoke import Receiver


def ready(process, port):
    deadline = time.monotonic() + 15
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError(f"Server exited during startup: {process.returncode}")
        try:
            with socket.create_connection(("127.0.0.1", port), 0.2):
                return
        except OSError:
            time.sleep(0.1)
    raise TimeoutError("Server listener did not start")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=Path("build/main"))
    parser.add_argument("--configs", type=Path, default=Path("server_configs"))
    parser.add_argument("--http-port", type=int, default=18080)
    parser.add_argument("--rtsp-port", type=int, default=18554)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="osrv-metadata-restart-") as directory:
        configs = Path(directory) / "configs"
        shutil.copytree(args.configs, configs)
        common = configs / "common.config"
        tree = json.loads(common.read_text())
        tree["rtsp_metadata"] = {"enabled": True}
        tree["addresses"].update(ipv4="127.0.0.1", http_port=str(args.http_port), rtsp_port=str(args.rtsp_port))
        tree["advertised"].update(ipv4="127.0.0.1", http_port=0, rtsp_port=0, pullpoint_port=0)
        common.write_text(json.dumps(tree))
        discovery = configs / "discovery.config"
        discovery.write_text(json.dumps({"enabled": False, "interface_ipv4": "127.0.0.1"}))
        profiles = configs / "media_profiles.config"
        tree = json.loads(profiles.read_text())
        profile = tree["MediaProfiles"][0]
        profile.update(Metadata="MetadataToken0", VideoEncoder="", AudioEncoder="")
        # Encoder tokens are user-defined. Preserve the configured low-stream route.
        tree["MediaProfiles"][1].update(VideoEncoder="CustomLowEncoder", Metadata="MetadataToken0")
        for catalog in ("VideoEncoder", "VideoEncoderConfigurations"):
            for configuration in tree[catalog]:
                if configuration["token"] == "VideoEncoderToken1":
                    configuration["token"] = "CustomLowEncoder"
        profiles.write_text(json.dumps(tree))
        for filename in ("media.config", "media2.config"):
            file = configs / filename
            settings = json.loads(file.read_text())
            for route in settings["GetStreamUri"]:
                if route["VideoEncoderToken"] == "VideoEncoderToken1":
                    route["VideoEncoderToken"] = "CustomLowEncoder"
            file.write_text(json.dumps(settings))
        path = "onvif/profile/" + hashlib.sha256(b"ProfileToken0").hexdigest()
        for restart in (False, True):
            with (Path(directory) / "server.log").open("ab") as log:
                process = subprocess.Popen([str(args.binary.resolve()), str(configs)], stdout=log, stderr=log)
                try:
                    ready(process, args.http_port)
                    ready(process, args.rtsp_port)
                    custom_path = "onvif/profile/" + hashlib.sha256(b"ProfileToken1").hexdigest()
                    custom = Receiver("127.0.0.1", args.rtsp_port, custom_path, "tcp")
                    try:
                        custom.document() if restart else custom.document(initialized=True)
                    finally:
                        custom.close()
                    # Resolve the same URI directly, before any GetStreamUri request.
                    receiver = Receiver("127.0.0.1", args.rtsp_port, path, "tcp")
                    try:
                        if not restart:
                            receiver.document(initialized=True)
                            root = soap("127.0.0.1", args.http_port, "GetMetadataConfiguration",
                                        "<m:ConfigurationToken>MetadataToken0</m:ConfigurationToken>")
                            configuration = deepcopy(root.find(f".//{{{M}}}Configuration"))
                            configuration.remove(configuration.find(f"{{{T}}}Events"))
                            soap("127.0.0.1", args.http_port, "SetMetadataConfiguration",
                                 ET.tostring(configuration, encoding="unicode") + "<m:ForcePersistence>false</m:ForcePersistence>")
                        else:
                            for _ in range(3):
                                assert not receiver.document().findall(".//{http://docs.oasis-open.org/wsn/b-2}NotificationMessage")
                    finally:
                        receiver.close()
                except Exception:
                    print((Path(directory) / "server.log").read_text())
                    raise
                finally:
                    process.terminate()
                    try:
                        process.wait(timeout=3)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait()
        tree = json.loads(common.read_text())
        tree["rtsp_metadata"]["enabled"] = False
        common.write_text(json.dumps(tree))
        rejected = subprocess.run([str(args.binary.resolve()), str(configs)], stdout=subprocess.PIPE,
                                  stderr=subprocess.STDOUT, text=True, timeout=10)
        assert rejected.returncode != 0 and "rtsp_metadata.enabled is false" in rejected.stdout, rejected.stdout
        print("PASS stable URI preloaded before SOAP, metadata-only profile, custom encoder tokens, "
              "persisted Events=false across restart and disabled-transport startup guard")


if __name__ == "__main__":
    main()
