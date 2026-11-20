#!/usr/bin/env python3
"""Exercise Stage 4 SOAP configuration and profile-specific RTSP metadata.

Run against a disposable copy of the default configuration, with metadata enabled
and authentication disabled. Configuration and attachments are restored on exit.
"""
import argparse
from contextlib import ExitStack
from copy import deepcopy
import http.client
from pathlib import Path
import sys
import time
from urllib.parse import urlsplit
import xml.etree.ElementTree as ET

from event_delivery_smoke import call, pull, A, W, T, S
from rtsp_metadata_smoke import Receiver
from network_smoke import Rtsp

M = "http://www.onvif.org/ver10/media/wsdl"
M2 = "http://www.onvif.org/ver20/media/wsdl"
TOPICS = "http://www.onvif.org/ver10/topics"


def soap(host, port, operation, body="", media2=False, expected=200, validate=None):
    namespace = M2 if media2 else M
    payload = (f'<s:Envelope xmlns:s="{S}" xmlns:m="{namespace}" xmlns:tt="{T}" '
               f'xmlns:wsnt="{W}" xmlns:osrv="urn:onvif-server-emulator:analytics" xmlns:alias="{TOPICS}"><s:Body>'
               f'<m:{operation}>{body}</m:{operation}></s:Body></s:Envelope>')
    if validate and expected == 200:
        from lxml import etree
        validate(etree.tostring(etree.fromstring(payload.encode()).find(f"{{{S}}}Body")[0]), media2)
    connection = http.client.HTTPConnection(host, port, timeout=15)
    try:
        connection.request("POST", "/onvif/" + ("media2" if media2 else "media") + "_service",
                           payload, {"Content-Type": "application/soap+xml"})
        response = connection.getresponse()
        data = response.read()
        assert response.status == expected, (operation, response.status, data)
        root = ET.fromstring(data)
        if validate:
            from lxml import etree
            validate(etree.tostring(etree.fromstring(data).find(f"{{{S}}}Body")[0]), media2) if expected == 200 else validate(data, None)
        return root
    finally:
        connection.close()


def attach(host, port, profile, token="MetadataToken0", media2=False):
    body = f"<m:ProfileToken>{profile}</m:ProfileToken>"
    if media2:
        body += f"<m:Configuration><m:Type>Metadata</m:Type><m:Token>{token}</m:Token></m:Configuration>"
        return soap(host, port, "AddConfiguration", body, True)
    return soap(host, port, "AddMetadataConfiguration", body + f"<m:ConfigurationToken>{token}</m:ConfigurationToken>")


def stream_path(host, port, profile, media2=False, validate=None):
    setup = "<m:Protocol>RTSP</m:Protocol>" if media2 else (
        "<m:StreamSetup><tt:Stream>RTP-Unicast</tt:Stream><tt:Transport>"
        "<tt:Protocol>RTSP</tt:Protocol></tt:Transport></m:StreamSetup>")
    root = soap(host, port, "GetStreamUri", setup + f"<m:ProfileToken>{profile}</m:ProfileToken>", media2, validate=validate)
    return urlsplit(root.find(f".//{{{M2 if media2 else T}}}Uri").text).path.lstrip("/")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--http-port", type=int, default=8080)
    parser.add_argument("--rtsp-port", type=int, default=8554)
    parser.add_argument("--contract", type=Path)
    args = parser.parse_args()
    validate = None
    metadata_validate = None
    if args.contract:
        from lxml import etree
        sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "experiments/rtsp_metadata"))
        from contract import load_media_schema, load_schema
        schemas = [load_media_schema(args.contract, value) for value in (False, True)]
        fault_schema = load_schema(args.contract, resource_faults=True)
        def validate(data, media2):
            if media2 is None:
                fault_schema.assertValid(etree.fromstring(data))
            else:
                schemas[int(media2)].validate(data)
        metadata_schema = load_schema(args.contract)
        metadata_validate = lambda data: metadata_schema.assertValid(etree.fromstring(data))
    request = lambda operation, body="", media2=False, expected=200: soap(
        args.host, args.http_port, operation, body, media2, expected, validate)
    originals = []
    profile = None
    subscription = None
    try:
        for media2 in (False, True):
            request("GetMetadataConfigurations", media2=media2)
            request("GetMetadataConfigurationOptions", media2=media2)
            fault = request("GetMetadataConfigurations" if media2 else "GetMetadataConfiguration",
                            "<m:ConfigurationToken>missing</m:ConfigurationToken>", media2, 400)
            assert fault.find(f".//{{{S}}}Subcode/{{{S}}}Subcode/{{{S}}}Value").text == "ter:NoConfig"
        request("GetCompatibleMetadataConfigurations", "<m:ProfileToken>ProfileToken0</m:ProfileToken>")
        capabilities = request("GetServiceCapabilities", media2=True)
        assert "Metadata" in capabilities.find(f".//{{{M2}}}ProfileCapabilities").get("ConfigurationsSupported").split()
        for token in ("MetadataToken0", "MetadataToken1"):
            root = request("GetMetadataConfiguration", f"<m:ConfigurationToken>{token}</m:ConfigurationToken>")
            originals.append(deepcopy(root.find(f".//{{{M}}}Configuration")))
        references = "".join(f"<m:Configuration><m:Type>{kind}</m:Type><m:Token>{token}</m:Token></m:Configuration>"
                             for kind, token in (("Metadata", "MetadataToken1"), ("VideoEncoder", "VideoEncoderToken0"),
                                                 ("VideoSource", "VideoSrcConfigToken0")))
        profile_root = request("CreateProfile", "<m:Name>Stage4 filtered profile</m:Name>" + references, True)
        profile = profile_root.find(f".//{{{M2}}}Token").text
        attach(args.host, args.http_port, "ProfileToken0")
        attach(args.host, args.http_port, profile, "MetadataToken1", True)
        configuration = deepcopy(originals[1])
        configuration.tag = f"{{{M2}}}Configuration"
        configuration.set("CompressionType", "None")
        events = configuration.find(f"{{{T}}}Events")
        filters = ET.SubElement(events, f"{{{T}}}Filter")
        ET.SubElement(filters, f"{{{W}}}TopicExpression", Dialect="http://www.onvif.org/ver10/tev/topicExpression/ConcreteSet").text = "alias:RuleEngine/CellMotionDetector//."
        ET.SubElement(filters, f"{{{W}}}MessageContent", Dialect="http://www.onvif.org/ver10/tev/messageContentFilter/ItemFilter").text = "boolean(//tt:SimpleItem[@Name='IsMotion' and @Value='true'])"
        request("SetMetadataConfiguration", ET.tostring(configuration, encoding="unicode"), True)
        configured = request("GetMetadataConfigurations", "<m:ConfigurationToken>MetadataToken1</m:ConfigurationToken>", True)
        assert configured.find(f".//{{{M2}}}Configurations").get("CompressionType") == "None", "CompressionType did not round-trip"

        for media2 in (False, True):
            request("GetProfiles", "<m:Type>All</m:Type>" if media2 else "", media2)
        filtered_path = stream_path(args.host, args.http_port, profile, True, validate)
        all_path = stream_path(args.host, args.http_port, "ProfileToken0", validate=validate)
        assert filtered_path != all_path
        assert stream_path(args.host, args.http_port, profile) == filtered_path
        endpoint = f"http://{args.host}:{args.http_port}/onvif/event_service"
        subscription = call(endpoint, "CreatePullPointSubscription").find(f".//{{{A}}}Address").text
        with ExitStack() as stack:
            def receive(path, transport="tcp", mode="metadata"):
                receiver = Receiver(args.host, args.rtsp_port, path, transport, mode=mode, validate=metadata_validate)
                stack.callback(receiver.close)
                return receiver
            all_events = receive(all_path, mode="all")
            filtered = receive(filtered_path, "udp")
            root = filtered.document(changed=True)
            for message in root.findall(f".//{{{W}}}NotificationMessage"):
                assert "CellMotionDetector" in message.find(f"{{{W}}}Topic").text
                assert message.find(f".//{{{T}}}SimpleItem[@Name='IsMotion']").get("Value") == "true"
            assert pull(subscription), "PullPoint stopped after metadata filtering"
            # Drain existing initialization, then request a new property snapshot.
            all_events.document(initialized=True)
            before_idr = all_events.video_idr
            request("SetSynchronizationPoint", "<m:ProfileToken>ProfileToken0</m:ProfileToken>")
            all_events.document(initialized=True)
            deadline = time.monotonic() + 3
            while all_events.video_idr == before_idr and time.monotonic() < deadline:
                all_events.packet()
            assert all_events.video_idr > before_idr, "Synchronization did not deliver an H.264 IDR"
            request("SetSynchronizationPoint", f"<m:ProfileToken>{profile}</m:ProfileToken>", True)
            configuration.remove(events)
            request("SetMetadataConfiguration", ET.tostring(configuration, encoding="unicode"), True)
            assert stream_path(args.host, args.http_port, profile, True) == filtered_path
            quiet = receive(filtered_path)
            for _ in range(3):
                assert not quiet.document().findall(f".//{{{W}}}NotificationMessage"), "Events-disabled stream delivered events"
            request("RemoveConfiguration", f"<m:ProfileToken>{profile}</m:ProfileToken>"
                    "<m:Configuration><m:Type>Metadata</m:Type><m:Token>MetadataToken1</m:Token></m:Configuration>", True)
            client = Rtsp(args.host, args.rtsp_port)
            try:
                _, sdp = client.request("DESCRIBE", f"rtsp://{args.host}:{args.rtsp_port}/{filtered_path}", "Accept: application/sdp\r\n")
                assert b"m=application" not in sdp, "Removed association still exposes metadata to new connections"
            finally:
                client.socket.close()
            filtered.document(changed=True)  # Existing session retains its filter even after removal.
            print("PASS Media/Media2 " + ("pinned WSDL/XSD, " if validate else "") + "token faults, profile isolation, topic/content filters, "
                  "event disable, stable URI, active-session snapshots, synchronization and independent PullPoint", flush=True)
    finally:
        if subscription:
            call(subscription, "Unsubscribe")
        if profile:
            request("DeleteProfile", f"<m:Token>{profile}</m:Token>", True)
        request("RemoveMetadataConfiguration", "<m:ProfileToken>ProfileToken0</m:ProfileToken>")
        for configuration in originals:
            request("SetMetadataConfiguration", ET.tostring(configuration, encoding="unicode") + "<m:ForcePersistence>true</m:ForcePersistence>")


if __name__ == "__main__":
    main()
