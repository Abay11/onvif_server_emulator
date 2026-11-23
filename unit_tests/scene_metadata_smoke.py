#!/usr/bin/env python3
"""Check synthetic scene metadata using disposable default emulator configs.

Exercises the fixed module, metadata selection, profile isolation and shared
scene/event sample identity over real RTSP. Requires metadata transport enabled.
Use one-second event generator intervals in the disposable event.config so that
the default 30-second simulator period does not exceed the receive windows.
"""
import argparse
import base64
from contextlib import ExitStack
from copy import deepcopy
import http.client
from pathlib import Path
import sys
import time
import xml.etree.ElementTree as ET
from urllib.parse import urlsplit

from event_delivery_smoke import call, pull, A, T, W, S
from metadata_configuration_smoke import soap, attach, stream_path, M, M2
from rtsp_metadata_smoke import Receiver

ANALYTICS = "http://www.onvif.org/ver20/analytics/wsdl"
MODULE = "ObjectTracker"
LPR_MODULE = "LicensePlateRecognition"
FACE_MODULE = "FaceRecognition"
URN = "urn:onvif-server-emulator:analytics"
TOPIC = "tns1:RuleEngine/LineDetector/Crossed"
LPR_TOPIC = "tns1:RuleEngine/Recognition/LicensePlate"
FACE_TOPIC = "tns1:RuleEngine/Recognition/Face"
FC = "http://www.onvif.org/ver20/analytics/humanface"
BD = "http://www.onvif.org/ver20/analytics/humanbody"


def check_human_descriptors(root):
    for appearance in root.findall(f".//{{{T}}}Object/{{{T}}}Appearance"):
        kind = appearance.find(f"{{{T}}}Class/{{{T}}}Type").text
        face = appearance.find(f"{{{T}}}HumanFace")
        body = appearance.find(f"{{{T}}}HumanBody")
        if kind == "HumanFace":
            assert face is not None and len(face), "Missing HumanFace descriptor"
            pose = face.find(f"{{{FC}}}PoseAngle/{{{FC}}}PoseAngles")
            assert pose is not None and all(float(pose.get(a)) == 0 for a in ("yaw", "pitch", "roll"))
            for accessory in ("Opticals", "Mask"):
                assert face.find(f"{{{FC}}}Accessory/{{{FC}}}{accessory}/{{{FC}}}Wear").text in ("false", "0")
        else:
            assert face is None, "Face descriptor on a non-face object"
        if kind == "Human":
            assert body is not None and len(body), "Missing HumanBody descriptor"
            assert int(body.find(f"{{{BD}}}BodyMetric/{{{BD}}}Height").text) == 175
            assert body.find(f"{{{BD}}}Behaviour/{{{BD}}}Activity").text == "Walking"
            for garment, category, style, rgb in (("Tops", "LongSleeve", "Shirt", (40, 100, 180)),
                                                 ("Bottoms", "Trousers", "Jeans", (30, 40, 70))):
                node = body.find(f"{{{BD}}}Clothing/{{{BD}}}{garment}")
                assert node.find(f"{{{BD}}}Category").text == category
                assert node.find(f"{{{BD}}}Style").text == style
                color = node.find(f"{{{BD}}}Color/{{{T}}}ColorCluster/{{{T}}}Color")
                assert color.get("Colorspace") == "http://www.onvif.org/ver10/colorspace/RGB"
                assert tuple(int(color.get(a)) for a in ("X", "Y", "Z")) == rgb
        else:
            assert body is None, "Body descriptor on a non-human object"


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

    image_cache = {}

    def check_images(root, required=False):
        check_human_descriptors(root)
        found = False
        for appearance in root.findall(f".//{{{T}}}Object/{{{T}}}Appearance"):
            reference = appearance.find(f"{{{T}}}ImageRef")
            embedded = appearance.find(f"{{{T}}}Image")
            if reference is None:
                assert embedded is None, "Embedded image has no matching reference in this simulator"
                continue
            assert reference.text, "Empty ImageRef"
            if reference.text not in image_cache:
                target = urlsplit(reference.text)
                assert target.scheme == "http" and target.hostname, reference.text
                connection = http.client.HTTPConnection(target.hostname, target.port, timeout=15)
                try:
                    connection.request("GET", target.path)
                    response = connection.getresponse()
                    image = response.read()
                    assert response.status == 200 and response.getheader("Content-Type") == "image/png"
                    assert image.startswith(b"\x89PNG\r\n\x1a\n"), "ImageRef did not return PNG"
                    image_cache[reference.text] = image
                finally:
                    connection.close()
            if embedded is not None:
                found = True
                assert embedded.text, "Empty Image"
                assert base64.b64decode(embedded.text, validate=True) == image_cache[reference.text], "Image/ImageRef differ"
        if required:
            assert found, "No embedded object image"
        return found

    def frame(client, only=None):
        deadline = time.monotonic() + 12
        while time.monotonic() < deadline:
            root = client.document()
            check_human_descriptors(root)
            assert not root.findall(f".//{{{W}}}NotificationMessage"), "Events leaked into a scene-only stream"
            if only:
                assert all(node.get("Source") == only for node in root.findall(f"{{{T}}}VideoAnalytics/{{{T}}}Frame"))
            result = root.find(f"{{{T}}}VideoAnalytics/{{{T}}}Frame[@Source='{MODULE}']")
            if result is not None:
                if result.get("Source") != MODULE:
                    continue
                objects = result.findall(f"{{{T}}}Object")
                assert all(int(obj.get("ObjectId")) > 0 for obj in objects)
                if not any(obj.find(f"{{{T}}}Appearance/{{{T}}}Class/{{{T}}}Type").text == "Human"
                           for obj in objects):
                    continue
                for box in result.findall(f".//{{{T}}}BoundingBox"):
                    left, right = float(box.get("left")), float(box.get("right"))
                    bottom, top = float(box.get("bottom")), float(box.get("top"))
                    assert -1 <= left <= right <= 1 and -1 <= bottom <= top <= 1
                return result
        raise TimeoutError("No synthetic scene frame")

    def plate_frame(client):
        deadline = time.monotonic() + 40
        while time.monotonic() < deadline:
            root = client.document()
            assert not root.findall(f".//{{{W}}}NotificationMessage"), "Events leaked into a scene-only stream"
            if root.find(f".//{{{T}}}Object/{{{T}}}Appearance/{{{T}}}LicensePlateInfo/{{{T}}}PlateNumber") is not None and check_images(root):
                return
        raise TimeoutError("No LPR descriptor in scene-only stream")

    def quiet(client):
        for _ in range(3):
            root = client.document()
            assert root.find(f"{{{T}}}VideoAnalytics") is None, "Disabled or unassociated analytics delivered scene data"
            assert not root.findall(f".//{{{W}}}NotificationMessage")

    rule_created = False
    plate_rule_created = False
    face_rule_created = False
    subscription = None
    try:
        caps = analytics("GetServiceCapabilities")
        assert caps.find(f".//{{{ANALYTICS}}}Capabilities").get("SupportedMetadata") == "true"
        modules = analytics("GetAnalyticsModules", "<m:ConfigurationToken>AnalyticsConfig_0</m:ConfigurationToken>")
        assert any(node.get("Name") == MODULE for node in modules.findall(f".//{{{ANALYTICS}}}AnalyticsModule"))
        analytics("GetSupportedAnalyticsModules", "<m:ConfigurationToken>AnalyticsConfig_0</m:ConfigurationToken>")
        supported = analytics("GetSupportedMetadata")
        assert supported.find(f".//{{{T}}}Appearance/{{{T}}}LicensePlateInfo/{{{T}}}PlateNumber").text
        check_images(supported, required=True)
        assert supported.find(f".//{{{T}}}Appearance/{{{T}}}HumanFace") is not None
        assert supported.find(f".//{{{T}}}Appearance/{{{T}}}HumanBody") is not None
        samples = supported.findall(f".//{{{ANALYTICS}}}AnalyticsModule")
        assert {node.get("Type") for node in samples} == {"osrv:ObjectTracker", "osrv:LicensePlateRecognition", "osrv:FaceRecognition"}
        for name in (MODULE, LPR_MODULE, FACE_MODULE):
            response = analytics("GetSupportedMetadata", f"<m:Type>osrv:{name}</m:Type>")
            nodes = response.findall(f".//{{{ANALYTICS}}}AnalyticsModule")
            assert len(nodes) == 1 and nodes[0].get("Type") == f"osrv:{name}"
            assert nodes[0].find(f"{{{ANALYTICS}}}SampleFrame").get("Source") == name
            check_human_descriptors(response)
        analytics("GetSupportedMetadata", f'<m:Type xmlns:alias="{URN}"> alias:ObjectTracker </m:Type>')
        sample = analytics("GetSupportedMetadata", "<m:Type>osrv:ObjectTracker</m:Type>")
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
            plate_frame(first)  # Scene observations exist before an LPR rule is created.
            isolated = receiver(stack, "ProfileToken1", "udp")
            quiet(isolated)
            media("AddConfiguration", '<m:ProfileToken>ProfileToken1</m:ProfileToken><m:Configuration>'
                  '<m:Type>Analytics</m:Type><m:Token>AnalyticsConfig_0</m:Token></m:Configuration>', True)
            associated = receiver(stack, "ProfileToken1", "udp")
            frame(associated)
            plate_frame(associated)
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
            module = ET.SubElement(engine, f"{{{T}}}AnalyticsModule", Name=MODULE, Type="osrv:ObjectTracker")
            params = ET.SubElement(module, f"{{{T}}}Parameters")
            ET.SubElement(params, f"{{{T}}}SimpleItem", Name="Ignored", Value="1")
            save()
            selected = receiver(stack, "ProfileToken0")
            frame(selected, only=MODULE)
            for name in (LPR_MODULE, FACE_MODULE):
                engine.clear()
                single = ET.SubElement(engine, f"{{{T}}}AnalyticsModule", Name=name, Type=f"osrv:{name}")
                ET.SubElement(single, f"{{{T}}}Parameters")
                save()
                client = receiver(stack, "ProfileToken0")
                deadline = time.monotonic() + 12
                seen = False
                while time.monotonic() < deadline:
                    root = client.document()
                    assert not root.findall(f".//{{{W}}}NotificationMessage")
                    nodes = root.findall(f"{{{T}}}VideoAnalytics/{{{T}}}Frame")
                    assert all(node.get("Source") == name for node in nodes), "Module selection leaked another engine"
                    if nodes and check_images(root):
                        seen = True
                        break
                assert seen, f"No image frame from selected {name}"
            engine.clear()
            for name in (MODULE, LPR_MODULE, FACE_MODULE):
                selected_module = ET.SubElement(engine, f"{{{T}}}AnalyticsModule", Name=name, Type=f"osrv:{name}")
                ET.SubElement(selected_module, f"{{{T}}}Parameters")
            save()
            endpoint = f"http://{args.host}:{args.http_port}/onvif/event_service"
            subscription = call(endpoint, "CreatePullPointSubscription").find(f".//{{{A}}}Address").text
            analytics("CreateRules", '<m:ConfigurationToken>AnalyticsConfig_0</m:ConfigurationToken>'
                      '<m:Rule Name="scene-plate" Type="tt:LicensePlateRecognition"><tt:Parameters>'
                      '<tt:SimpleItem Name="IncludeImage" Value=""/>'
                      '<tt:SimpleItem Name="EventInterval" Value="0"/>'
                      '</tt:Parameters></m:Rule>')
            plate_rule_created = True
            analytics("CreateRules", '<m:ConfigurationToken>AnalyticsConfig_0</m:ConfigurationToken>'
                      '<m:Rule Name="scene-face" Type="tt:FaceRecognition"><tt:Parameters>'
                      '<tt:SimpleItem Name="IncludeImage" Value=""/>'
                      '</tt:Parameters></m:Rule>')
            face_rule_created = True
            event_node = ET.Element(f"{{{T}}}Events")
            index = list(config).index(config.find(f"{{{T}}}Analytics"))
            config.insert(index, event_node)
            save()
            mixed = receiver(stack, "ProfileToken0")
            frames, rtsp_events, pull_events = set(), set(), set()
            plates, rtsp_plates, pull_plates = set(), set(), set()
            faces, rtsp_faces, pull_faces = set(), set(), set()

            def plate_key(utc, info, likelihood, box):
                country = info.find(f"{{{T}}}CountryCode")
                return (utc, info.find(f"{{{T}}}PlateNumber").text,
                        country.text if country is not None else "", float(likelihood),
                        tuple(float(box.get(side)) for side in ("left", "right", "bottom", "top")))

            def collect(root):
                check_images(root)
                for item in root.findall(f"{{{T}}}VideoAnalytics/{{{T}}}Frame"):
                    for obj in item.findall(f"{{{T}}}Object"):
                        frames.add((item.get("UtcTime"), obj.get("ObjectId")))
                        if item.get("Source") == FACE_MODULE:
                            assert obj.find(f"{{{T}}}Appearance/{{{T}}}Class/{{{T}}}Type").text == "HumanFace"
                            faces.add((item.get("UtcTime"), obj.get("ObjectId")))
                        appearance = obj.find(f"{{{T}}}Appearance")
                        vehicle = appearance.find(f"{{{T}}}VehicleInfo")
                        if vehicle is not None:
                            assert item.get("Source") == LPR_MODULE
                            assert vehicle.find(f"{{{T}}}Type").text in {"Car", "Truck", "Bus", "Motorcycle"}
                            assert float(obj.find(f"{{{T}}}Behaviour/{{{T}}}Speed").text) >= 0
                            color = appearance.find(f"{{{T}}}Color/{{{T}}}ColorCluster/{{{T}}}Color")
                            assert color.get("Colorspace") == "http://www.onvif.org/ver10/colorspace/RGB"
                            assert all(0 <= float(color.get(component)) <= 255 for component in ("X", "Y", "Z"))
                        info = appearance.find(f"{{{T}}}LicensePlateInfo")
                        if info is not None:
                            assert item.get("Source") == LPR_MODULE
                            assert item.find(f"{{{T}}}Object[@ObjectId='{obj.get('Parent')}']/{{{T}}}Appearance/{{{T}}}VehicleInfo") is not None
                            plates.add(plate_key(item.get("UtcTime"), info,
                                      info.find(f"{{{T}}}PlateNumber").get("Likelihood", "1"),
                                      appearance.find(f"{{{T}}}Shape/{{{T}}}BoundingBox")))
                rtsp_events.update(keys(root.findall(f".//{{{W}}}NotificationMessage")))
                rtsp_plates.update(plate_keys(root.findall(f".//{{{W}}}NotificationMessage")))
                rtsp_faces.update(face_keys(root.findall(f".//{{{W}}}NotificationMessage")))

            def face_keys(messages):
                result = set()
                for message in messages:
                    if message.find(f"{{{W}}}Topic").text != FACE_TOPIC:
                        continue
                    body = message.find(f"{{{W}}}Message/{{{T}}}Message")
                    obj = body.find(f"{{{T}}}Data/{{{T}}}ElementItem[@Name='ObjectId']/{{{T}}}ObjectId")
                    result.add((body.get("UtcTime"), obj.get("ObjectId")))
                return result

            def plate_keys(messages):
                result = set()
                for message in messages:
                    if message.find(f"{{{W}}}Topic").text != LPR_TOPIC:
                        continue
                    body = message.find(f"{{{W}}}Message/{{{T}}}Message")
                    data = body.find(f"{{{T}}}Data")
                    result.add(plate_key(body.get("UtcTime"), data.find(f".//{{{T}}}LicensePlateInfo"),
                               data.find(f"{{{T}}}SimpleItem[@Name='Likelihood']").get("Value"),
                               data.find(f".//{{{T}}}BoundingBox")))
                return result
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
                messages = pull(subscription)
                pull_events.update(keys(messages))
                pull_plates.update(plate_keys(messages))
                pull_faces.update(face_keys(messages))
                if frames & rtsp_events & pull_events and plates & rtsp_plates & pull_plates and faces & rtsp_faces & pull_faces:
                    break
            assert frames & rtsp_events & pull_events, "Scene ObjectId/UtcTime did not match RTSP and PullPoint line events"
            assert plates & rtsp_plates & pull_plates, "LPR scene/event plate, country, confidence, box and UTC did not match"
            assert faces & rtsp_faces & pull_faces, "Face scene/event ObjectId/UtcTime did not match"
            config.remove(event_node)
            config.find(f"{{{T}}}Analytics").text = "false"
            save()
            quiet(receiver(stack, "ProfileToken0"))
        print("PASS scene TCP/UDP, fixed module discovery, schema/geometry, analytics selection, profile isolation, "
              "active-session snapshots, line ObjectId/UtcTime and LPR plate/country/confidence/box/UtcTime "
              "across scene/RTSP/PullPoint, downloadable PNG ImageRef and matching base64 Image, independent modules, "
              "Face ObjectId/UtcTime and nonempty HumanFace/HumanBody descriptors in SOAP and RTP")
    finally:
        if subscription:
            call(subscription, "Unsubscribe")
        if rule_created:
            analytics("DeleteRules", '<m:ConfigurationToken>AnalyticsConfig_0</m:ConfigurationToken>'
                      '<m:RuleName>stage5-scene-line</m:RuleName>')
        if face_rule_created:
            analytics("DeleteRules", '<m:ConfigurationToken>AnalyticsConfig_0</m:ConfigurationToken>'
                      '<m:RuleName>scene-face</m:RuleName>')
        if plate_rule_created:
            analytics("DeleteRules", '<m:ConfigurationToken>AnalyticsConfig_0</m:ConfigurationToken>'
                      '<m:RuleName>scene-plate</m:RuleName>')
        media("RemoveConfiguration", '<m:ProfileToken>ProfileToken1</m:ProfileToken>'
              '<m:Configuration><m:Type>Analytics</m:Type></m:Configuration>', True)
        for profile in ("ProfileToken0", "ProfileToken1"):
            media("RemoveMetadataConfiguration", f"<m:ProfileToken>{profile}</m:ProfileToken>")
        media("SetMetadataConfiguration", ET.tostring(original, encoding="unicode") + '<m:ForcePersistence>true</m:ForcePersistence>')


if __name__ == "__main__":
    main()
