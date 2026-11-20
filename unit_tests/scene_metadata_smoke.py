#!/usr/bin/env python3
"""Check synthetic scene metadata using disposable default emulator configs.

Exercises the fixed module, metadata selection, profile isolation and shared
scene/event sample identity over real RTSP. Requires metadata transport enabled.
"""
import argparse
from contextlib import ExitStack
from copy import deepcopy
import http.client
from pathlib import Path
import sys
import time
import xml.etree.ElementTree as ET

from event_delivery_smoke import call, pull, A, T, W, S
from metadata_configuration_smoke import soap, attach, stream_path, M, M2
from rtsp_metadata_smoke import Receiver

ANALYTICS = "http://www.onvif.org/ver20/analytics/wsdl"
MODULE = "SyntheticObjectTracker"
URN = "urn:onvif-server-emulator:analytics"
TOPIC = "tns1:RuleEngine/LineDetector/Crossed"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--http-port", type=int, default=8080)
    parser.add_argument("--rtsp-port", type=int, default=8554)
    parser.add_argument("--contract", type=Path)
    args = parser.parse_args()
    validate = None
    analytics_schema = None
    media_schemas = None
    if args.contract:
        from lxml import etree
        sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "experiments/rtsp_metadata"))
        from contract import load_schema, load_service_schema, load_media_schema
        schema = load_schema(args.contract)
        validate = lambda data: schema.assertValid(etree.fromstring(data))
        analytics_schema = load_service_schema(args.contract, "wsdl/ver20/analytics/wsdl/analytics.wsdl")
        media_schemas = [load_media_schema(args.contract, False), load_media_schema(args.contract, True)]

    def media(operation, body="", media2=False, expected=200):
        def check(data, version):
            if version is not None:
                media_schemas[int(version)].validate(data)
        return soap(args.host, args.http_port, operation, body, media2, expected,
                    check if media_schemas else None)

    def analytics(operation, body="", expected=200):
        payload = (f'<s:Envelope xmlns:s="{S}" xmlns:m="{ANALYTICS}" xmlns:tt="{T}" xmlns:osrv="{URN}">'
                   f'<s:Body><m:{operation}>{body}</m:{operation}></s:Body></s:Envelope>')
        if analytics_schema and expected == 200:
            from lxml import etree
            analytics_schema.validate(etree.tostring(etree.fromstring(payload.encode()).find(f"{{{S}}}Body")[0]))
        connection = http.client.HTTPConnection(args.host, args.http_port, timeout=15)
        try:
            connection.request("POST", "/onvif/analytics_service", payload,
                               {"Content-Type": "application/soap+xml"})
            response = connection.getresponse()
            raw = response.read()
            assert response.status == expected, (operation, response.status, raw)
            if analytics_schema and expected == 200:
                from lxml import etree
                analytics_schema.validate(etree.tostring(etree.fromstring(raw).find(f"{{{S}}}Body")[0]))
            return ET.fromstring(raw)
        finally:
            connection.close()

    def current_configuration():
        return media("GetMetadataConfiguration", "<m:ConfigurationToken>MetadataToken0</m:ConfigurationToken>").find(f".//{{{M}}}Configuration")

    original = deepcopy(current_configuration())
    config = deepcopy(original)
    config.tag = f"{{{M2}}}Configuration"
    config.find(f"{{{T}}}Analytics").text = "true"
    events = config.find(f"{{{T}}}Events")
    if events is not None:
        config.remove(events)

    def save():
        media("SetMetadataConfiguration", ET.tostring(config, encoding="unicode"), True)
        result = current_configuration()
        assert result.find(f"{{{T}}}Analytics").text == config.find(f"{{{T}}}Analytics").text

    def receiver(stack, profile, transport="tcp"):
        path = stream_path(args.host, args.http_port, profile, True)
        client = Receiver(args.host, args.rtsp_port, path, transport, validate=validate)
        stack.callback(client.close)
        return client

    def frame(client):
        deadline = time.monotonic() + 12
        while time.monotonic() < deadline:
            root = client.document()
            assert not root.findall(f".//{{{W}}}NotificationMessage"), "Events leaked into a scene-only stream"
            result = root.find(f"{{{T}}}VideoAnalytics/{{{T}}}Frame")
            if result is not None:
                assert result.get("Source") == MODULE
                obj = result.find(f"{{{T}}}Object")
                assert obj is not None and int(obj.get("ObjectId")) > 0
                assert obj.find(f"{{{T}}}Appearance/{{{T}}}Class/{{{T}}}Type").text == "Human"
                for box in result.findall(f".//{{{T}}}BoundingBox"):
                    left, right = float(box.get("left")), float(box.get("right"))
                    bottom, top = float(box.get("bottom")), float(box.get("top"))
                    assert -1 <= left <= right <= 1 and -1 <= bottom <= top <= 1
                return result
        raise TimeoutError("No synthetic scene frame")

    def quiet(client):
        for _ in range(3):
            root = client.document()
            assert root.find(f"{{{T}}}VideoAnalytics") is None, "Disabled or unassociated analytics delivered scene data"
            assert not root.findall(f".//{{{W}}}NotificationMessage")

    rule_created = False
    subscription = None
    try:
        caps = analytics("GetServiceCapabilities")
        assert caps.find(f".//{{{ANALYTICS}}}Capabilities").get("SupportedMetadata") == "true"
        modules = analytics("GetAnalyticsModules", "<m:ConfigurationToken>AnalyticsConfig_0</m:ConfigurationToken>")
        assert any(node.get("Name") == MODULE for node in modules.findall(f".//{{{ANALYTICS}}}AnalyticsModule"))
        analytics("GetSupportedAnalyticsModules", "<m:ConfigurationToken>AnalyticsConfig_0</m:ConfigurationToken>")
        analytics("GetSupportedMetadata")
        analytics("GetSupportedMetadata", f'<m:Type xmlns:alias="{URN}"> alias:SyntheticObjectTracker </m:Type>')
        sample = analytics("GetSupportedMetadata", "<m:Type>osrv:SyntheticObjectTracker</m:Type>")
        for box in sample.findall(f".//{{{T}}}BoundingBox"):
            assert float(box.get("right")) <= 0 and float(box.get("bottom")) >= 0, "Sample is not in top-left quadrant"
        failure = analytics("GetSupportedMetadata", "<m:Type>osrv:Unknown</m:Type>", 400)
        assert failure.find(f".//{{{S}}}Subcode/{{{S}}}Subcode/{{{S}}}Value").text == "ter:TypeNotExistent"
        analytics("GetAnalyticsModules", "<m:ConfigurationToken>unknown</m:ConfigurationToken>", 400)
        analytics("CreateRules", '<m:ConfigurationToken>AnalyticsConfig_0</m:ConfigurationToken>'
                  '<m:Rule Name="stage5-scene-line" Type="tt:LineDetector"><tt:Parameters/></m:Rule>')
        rule_created = True
        for profile in ("ProfileToken0", "ProfileToken1"):
            attach(args.host, args.http_port, profile)
        save()
        # Both profile responses describe the fixed module in their analytics configuration.
        media("GetProfiles")
        media("GetProfiles", "<m:Type>All</m:Type>", True)
        with ExitStack() as stack:
            first = receiver(stack, "ProfileToken0")
            frame(first)
            isolated = receiver(stack, "ProfileToken1", "udp")
            quiet(isolated)
            media("AddConfiguration", '<m:ProfileToken>ProfileToken1</m:ProfileToken><m:Configuration>'
                  '<m:Type>Analytics</m:Type><m:Token>AnalyticsConfig_0</m:Token></m:Configuration>', True)
            associated = receiver(stack, "ProfileToken1", "udp")
            frame(associated)
            quiet(isolated)  # Old prepared session retains its profile snapshot.
            engine = ET.SubElement(config, f"{{{T}}}AnalyticsEngineConfiguration")
            save()
            quiet(receiver(stack, "ProfileToken0"))  # Empty selector suppresses all modules.
            invalid = deepcopy(config)
            invalid_engine = invalid.find(f"{{{T}}}AnalyticsEngineConfiguration")
            invalid_module = ET.SubElement(invalid_engine, f"{{{T}}}AnalyticsModule",
                                           Name=MODULE, Type="osrv:Unknown")
            ET.SubElement(invalid_module, f"{{{T}}}Parameters")
            failure = media("SetMetadataConfiguration", ET.tostring(invalid, encoding="unicode"), True, 400)
            assert failure.find(f".//{{{S}}}Subcode/{{{S}}}Subcode/{{{S}}}Value").text == "ter:ConfigModify"
            module = ET.SubElement(engine, f"{{{T}}}AnalyticsModule", Name=MODULE, Type="osrv:SyntheticObjectTracker")
            params = ET.SubElement(module, f"{{{T}}}Parameters")
            ET.SubElement(params, f"{{{T}}}SimpleItem", Name="Ignored", Value="1")
            save()
            selected = receiver(stack, "ProfileToken0")
            frame(selected)
            endpoint = f"http://{args.host}:{args.http_port}/onvif/event_service"
            subscription = call(endpoint, "CreatePullPointSubscription").find(f".//{{{A}}}Address").text
            event_node = ET.Element(f"{{{T}}}Events")
            index = list(config).index(config.find(f"{{{T}}}Analytics"))
            config.insert(index, event_node)
            save()
            mixed = receiver(stack, "ProfileToken0")
            frames, rtsp_events, pull_events = set(), set(), set()
            def collect(root):
                for item in root.findall(f"{{{T}}}VideoAnalytics/{{{T}}}Frame"):
                    for obj in item.findall(f"{{{T}}}Object"):
                        frames.add((item.get("UtcTime"), obj.get("ObjectId")))
                rtsp_events.update(keys(root.findall(f".//{{{W}}}NotificationMessage")))
            def keys(messages):
                result = set()
                for message in messages:
                    if message.find(f"{{{W}}}Topic").text != TOPIC:
                        continue
                    body = message.find(f"{{{W}}}Message/{{{T}}}Message")
                    obj = body.find(f"{{{T}}}Data/{{{T}}}SimpleItem[@Name='ObjectId']")
                    result.add((body.get("UtcTime"), obj.get("Value")))
                return result
            deadline = time.monotonic() + 30
            while time.monotonic() < deadline:
                for _ in range(3):
                    collect(mixed.document())
                pull_events.update(keys(pull(subscription)))
                if frames & rtsp_events & pull_events:
                    break
            assert frames & rtsp_events & pull_events, "Scene ObjectId/UtcTime did not match RTSP and PullPoint line events"
            config.remove(event_node)
            config.find(f"{{{T}}}Analytics").text = "false"
            save()
            quiet(receiver(stack, "ProfileToken0"))
        print("PASS scene TCP/UDP, fixed module discovery, schema/geometry, analytics selection, profile isolation, "
              "active-session snapshots and identical scene/event ObjectId + UtcTime across RTSP/PullPoint")
    finally:
        if subscription:
            call(subscription, "Unsubscribe")
        if rule_created:
            analytics("DeleteRules", '<m:ConfigurationToken>AnalyticsConfig_0</m:ConfigurationToken>'
                      '<m:RuleName>stage5-scene-line</m:RuleName>')
        media("RemoveConfiguration", '<m:ProfileToken>ProfileToken1</m:ProfileToken>'
              '<m:Configuration><m:Type>Analytics</m:Type></m:Configuration>', True)
        for profile in ("ProfileToken0", "ProfileToken1"):
            media("RemoveMetadataConfiguration", f"<m:ProfileToken>{profile}</m:ProfileToken>")
        media("SetMetadataConfiguration", ET.tostring(original, encoding="unicode") + '<m:ForcePersistence>true</m:ForcePersistence>')


if __name__ == "__main__":
    main()
