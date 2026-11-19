#!/usr/bin/env python3
"""Rules and PullPoint checks. Run against a disposable configuration with auth disabled.

Creates temporary rules and removes them on completion. Requires Python 3 only.
"""
import argparse
import base64
from datetime import datetime, timedelta, timezone
import http.client
import time
import uuid
import xml.etree.ElementTree as ET
from pathlib import Path
from urllib.parse import urlsplit
from xml.sax.saxutils import escape

NS = {
    "s": "http://www.w3.org/2003/05/soap-envelope",
    "tan": "http://www.onvif.org/ver20/analytics/wsdl",
    "tt": "http://www.onvif.org/ver10/schema",
    "axt": "http://www.onvif.org/ver20/analytics",
    "tet": "http://www.onvif.org/ver10/events/wsdl",
    "wsnt": "http://docs.oasis-open.org/wsn/b-2",
    "trt": "http://www.onvif.org/ver10/media/wsdl",
    "tr2": "http://www.onvif.org/ver20/media/wsdl",
    "wsa": "http://www.w3.org/2005/08/addressing",
}


def run(args):
    captured = 0

    def soap(op, body="", service="analytics", prefix="tan", fault=None, url=None):
        nonlocal captured
        action = NS[prefix] + "/" + op
        if url:
            if op in {"Unsubscribe", "Renew"}:
                action = "http://docs.oasis-open.org/wsn/bw-2/SubscriptionManager/" + op + "Request"
            else:
                action = "http://www.onvif.org/ver10/events/wsdl/PullPointSubscription/" + op + "Request"
        header = (f"<s:Header><wsa:Action>{action}</wsa:Action><wsa:To>{escape(url or '')}</wsa:To>"
                  f"<wsa:MessageID>urn:uuid:{uuid.uuid4()}</wsa:MessageID></s:Header>")
        envelope = ('<s:Envelope ' + " ".join(f'xmlns:{p}="{uri}"' for p, uri in NS.items()) +
                    f'>{header}<s:Body><{prefix}:{op}>{body}</{prefix}:{op}></s:Body></s:Envelope>')
        target = urlsplit(url) if url else None
        connection = http.client.HTTPConnection(target.hostname if target else args.host,
                                                target.port if target else args.port, timeout=15)
        try:
            connection.request("POST", target.path if target else f"/onvif/{service}_service", envelope,
                               {"Content-Type": f'application/soap+xml; action="{action}"'})
            response = connection.getresponse()
            data = response.read()
            if args.capture:
                directory = Path(args.capture)
                directory.mkdir(parents=True, exist_ok=True)
                (directory / f"{captured:03d}-{op}.xml").write_bytes(data)
                captured += 1
            tree = ET.fromstring(data)
            if fault:
                assert response.status >= 400, (op, response.status, data)
                assert tree.find(".//s:Fault", NS) is not None, data
                assert tree.find(".//s:Subcode/s:Subcode/s:Value", NS).text == "ter:" + fault, data
            else:
                assert response.status == 200, (op, response.status, data)
                assert tree.find(".//s:Fault", NS) is None, data
            return tree
        finally:
            connection.close()

    token = f"<tan:ConfigurationToken>{escape(args.token)}</tan:ConfigurationToken>"
    name = "smoke_" + uuid.uuid4().hex[:12]
    second = name + "_disarmed"
    line_name = name + "_line"
    count_name = name + "_count"
    field_name = name + "_field"
    plate_name = name + "_plate"
    subscription = None

    def rule(rule_name, armed=True):
        return (f'<tan:Rule Name="{rule_name}" Type="tt:MotionRegionDetector"><tt:Parameters>'
                '<tt:ElementItem Name="MotionRegion">'
                f'<axt:MotionRegionConfig Armed="{str(armed).lower()}" Sensitivity="1"/>'
                '</tt:ElementItem></tt:Parameters></tan:Rule>')

    def line_rule(direction="Any", points="<tt:Point x='0' y='-1'/><tt:Point x='0' y='1'/>"):
        return (f'<tan:Rule Name="{line_name}" Type="tt:LineDetector"><tt:Parameters>'
                f'<tt:SimpleItem Name="Direction" Value="{direction}"/>'
                f'<tt:ElementItem Name="Segments"><tt:Polyline>{points}</tt:Polyline>'
                '</tt:ElementItem></tt:Parameters></tan:Rule>')

    def count_rule(params=None):
        if params is None:
            params = ('<tt:SimpleItem Name="ReportTimeInterval" Value="PT3S"/>'
                      '<tt:SimpleItem Name="PassAllPolylines" Value="false"/>')
            for x in (-0.1, 0.1):
                params += ('<tt:ElementItem Name="Segments"><tt:Polyline>'
                           f'<tt:Point x="{x}" y="-1"/><tt:Point x="{x}" y="1"/>'
                           '</tt:Polyline></tt:ElementItem>')
        return (f'<tan:Rule Name="{count_name}" Type="tt:LineCounting"><tt:Parameters>'
                + params + '</tt:Parameters></tan:Rule>')

    def field_rule(points="<tt:Point x='-0.5' y='-0.5'/><tt:Point x='0.5' y='-0.5'/><tt:Point x='0.5' y='0.5'/><tt:Point x='-0.5' y='0.5'/>"):
        return (f'<tan:Rule Name="{field_name}" Type="tt:FieldDetector"><tt:Parameters>'
                f'<tt:ElementItem Name="Field"><tt:Polygon>{points}</tt:Polygon>'
                '</tt:ElementItem></tt:Parameters></tan:Rule>')

    try:

        caps = soap("GetServiceCapabilities").find(".//tan:Capabilities", NS)
        assert caps.attrib["RuleSupport"] == "true" and caps.attrib["AnalyticsModuleSupport"] == "false"
        properties = soap("GetEventProperties", service="event", prefix="tet")
        descriptions = properties.findall(".//tt:MessageDescription", NS)
        assert any({n.attrib["Name"] for n in d.findall("tt:Source/tt:SimpleItemDescription", NS)} ==
                   {"VideoSource", "RuleName"} for d in descriptions)
        line_desc = properties.find(".//LineDetector/Crossed/tt:MessageDescription", NS)
        assert line_desc is not None and line_desc.attrib["IsProperty"] == "false"
        assert {n.attrib["Name"] for n in line_desc.findall("tt:Source/tt:SimpleItemDescription", NS)} == {
            "VideoSource", "AnalyticsConfiguration", "Rule"}
        assert line_desc.find("tt:Data/tt:SimpleItemDescription", NS).attrib == {
            "Name": "ObjectId", "Type": "tt:StringList"}
        count_desc = properties.find(".//CountAggregation/Counter/tt:MessageDescription", NS)
        assert count_desc is not None and count_desc.attrib["IsProperty"] == "true"
        assert count_desc.find("tt:Data/tt:SimpleItemDescription", NS).attrib == {
            "Name": "Count", "Type": "xsd:int"}
        field_desc = properties.find(".//FieldDetector/ObjectsInside/tt:MessageDescription", NS)
        assert field_desc is not None and field_desc.attrib["IsProperty"] == "true"
        assert field_desc.find("tt:Data/tt:SimpleItemDescription", NS).attrib == {
            "Name": "IsInside", "Type": "xsd:boolean"}
        supported = soap("GetSupportedRules", token)
        desc = supported.find(".//tt:RuleDescription", NS)
        assert desc.attrib["Name"] == "tt:MotionRegionDetector" and desc.attrib["maxInstances"] == "4"
        line_desc = next(d for d in supported.findall(".//tt:RuleDescription", NS)
                         if d.attrib["Name"] == "tt:LineDetector")
        assert line_desc.find("tt:Messages", NS).attrib["IsProperty"] == "false"
        count_desc = next(d for d in supported.findall(".//tt:RuleDescription", NS)
                          if d.attrib["Name"] == "tt:LineCounting")
        assert count_desc.find("tt:Messages", NS).attrib["IsProperty"] == "true"
        field_desc = next(d for d in supported.findall(".//tt:RuleDescription", NS)
                          if d.attrib["Name"] == "tt:FieldDetector")
        assert field_desc.find("tt:Parameters/tt:ElementItemDescription", NS).attrib == {
            "Name": "Field", "Type": "tt:Polygon"}
        field_options = soap("GetRuleOptions", "<tan:RuleType>tt:FieldDetector</tan:RuleType>" + token)
        assert field_options.find(".//tt:PolygonOptions/tt:VertexLimits/tt:Min", NS).text == "3"
        assert field_options.find(".//tt:PolygonOptions/tt:VertexLimits/tt:Max", NS).text == "8"
        options = soap("GetRuleOptions", token)
        option = options.find(".//tan:RuleOptions", NS)
        assert option.attrib["RuleType"] == "tt:MotionRegionDetector"
        region_options = option.find("axt:MotionRegionConfigOptions", NS)
        assert region_options.attrib["PTZPresetMotionSupport"] == "false"
        assert region_options.find("axt:PolygonLimits/tt:Min", NS).text == "3"
        line_options = soap("GetRuleOptions", "<tan:RuleType>tt:LineDetector</tan:RuleType>" + token)
        line_options = {o.attrib["Name"]: o for o in line_options.findall(".//tan:RuleOptions", NS)}
        assert line_options["Direction"].find("tt:StringList", NS).text.split() == ["Left", "Right", "Any"]
        assert line_options["Segments"].find("tt:IntRange/tt:Min", NS).text == "2"
        assert line_options["Segments"].find("tt:IntRange/tt:Max", NS).text == "8"
        count_options = soap("GetRuleOptions", "<tan:RuleType>tt:LineCounting</tan:RuleType>" + token)
        count_options = {o.attrib["Name"]: o for o in count_options.findall(".//tan:RuleOptions", NS)}
        assert count_options["Segments"].attrib["maxOccurs"] == "4"
        assert count_options["ReportTimeInterval"].find("tt:DurationRange/tt:Min", NS).text == "PT0S"
        soap("GetRules", "<tan:ConfigurationToken>missing</tan:ConfigurationToken>", fault="NoConfig")
        soap("GetRuleOptions", "<tan:RuleType>tt:Unknown</tan:RuleType>" + token, fault="RuleNotExistent")
        soap("CreateRules", token + line_rule("Entering"), fault="InvalidRule")
        soap("CreateRules", token + line_rule(points="<tt:Point x='0' y='0'/><tt:Point x='0' y='0'/>"),
             fault="InvalidRule")
        soap("CreateRules", token + count_rule('<tt:SimpleItem Name="ResetTime" Value="25:00:00"/>'),
             fault="InvalidRule")
        soap("CreateRules", token + rule(name) + rule(second, False) + line_rule() + count_rule())
        soap("CreateRules", token + rule(name + "_rollback") + rule(name), fault="RuleAlreadyExistent")
        rules = soap("GetRules", token).findall(".//tan:Rule", NS)
        names = {r.attrib["Name"] for r in rules}
        assert name in names and second in names and name + "_rollback" not in names
        assert line_name in names
        assert count_name in names
        soap("CreateRules", token + rule(name + "_overflow"), fault="TooManyRules")
        soap("ModifyRules", token + rule(line_name), fault="TypeChangeProhibited")
        for service, prefix, operation in [("media", "trt", "GetVideoAnalyticsConfigurations"),
                                            ("media2", "tr2", "GetAnalyticsConfigurations")]:
            media = soap(operation, service=service, prefix=prefix)
            configs = media.findall(f".//{prefix}:Configurations", NS)
            cfg = next(c for c in configs if c.attrib["token"] == args.token)
            assert [child.tag for child in cfg][:4] == [
                f"{{{NS['tt']}}}{field}" for field in
                ("Name", "UseCount", "AnalyticsEngineConfiguration", "RuleEngineConfiguration")]
            assert cfg.find("tt:Count", NS) is None
            assert int(cfg.find("tt:UseCount", NS).text) >= 0
            assert {r.attrib["Name"] for r in cfg.findall("tt:RuleEngineConfiguration/tt:Rule", NS)} == names
            line = next(r for r in cfg.findall("tt:RuleEngineConfiguration/tt:Rule", NS)
                        if r.attrib["Name"] == line_name)
            assert line.attrib["Type"] == "tt:LineDetector"
            assert len(line.findall("tt:Parameters/tt:ElementItem/tt:Polyline/tt:Point", NS)) == 2
            counter = next(r for r in cfg.findall("tt:RuleEngineConfiguration/tt:Rule", NS)
                           if r.attrib["Name"] == count_name)
            assert counter.attrib["Type"] == "tt:LineCounting"
            assert len(counter.findall("tt:Parameters/tt:ElementItem", NS)) == 2
        created = soap("CreatePullPointSubscription", service="event", prefix="tet")
        subscription = next(n.text for n in created.iter() if n.tag.endswith("}Address"))
        soap("SetSynchronizationPoint", prefix="tet", url=subscription)
        seen_on = seen_off = seen_initialized = seen_crossed = False
        count_initialized = count_changed = False
        deadline = time.monotonic() + 20
        while time.monotonic() < deadline and not (
                seen_on and seen_off and seen_initialized and seen_crossed and count_initialized and count_changed):
            events = soap("PullMessages", "<tet:Timeout>PT2S</tet:Timeout><tet:MessageLimit>100</tet:MessageLimit>",
                          prefix="tet", url=subscription)
            for notification in events.findall(".//wsnt:NotificationMessage", NS):
                topic = notification.find("wsnt:Topic", NS).text or ""
                if topic.endswith("RuleEngine/CountAggregation/Counter"):
                    message = notification.find("wsnt:Message/tt:Message", NS)
                    source = {n.attrib["Name"]: n.attrib["Value"]
                              for n in message.findall("tt:Source/tt:SimpleItem", NS)}
                    if source["Rule"] == count_name:
                        assert source["AnalyticsConfiguration"] == args.token
                        value = message.find("tt:Data/tt:SimpleItem", NS)
                        assert value.attrib["Name"] == "Count"
                        count = int(value.attrib["Value"])
                        assert count >= 0 and count % 2 == 0
                        count_initialized |= message.attrib["PropertyOperation"] == "Initialized"
                        count_changed |= message.attrib["PropertyOperation"] == "Changed" and count > 0
                    continue
                if topic.endswith("RuleEngine/LineDetector/Crossed"):
                    message = notification.find("wsnt:Message/tt:Message", NS)
                    assert "PropertyOperation" not in message.attrib
                    source = {n.attrib["Name"]: n.attrib["Value"]
                              for n in message.findall("tt:Source/tt:SimpleItem", NS)}
                    if source["Rule"] == line_name:
                        assert source["AnalyticsConfiguration"] == args.token and source["VideoSource"]
                        value = message.find("tt:Data/tt:SimpleItem", NS)
                        assert value.attrib == {"Name": "ObjectId", "Value": "1"}
                        seen_crossed = True
                    continue
                if not topic.endswith("RuleEngine/MotionRegionDetector/Motion"):
                    continue
                message = notification.find("wsnt:Message/tt:Message", NS)
                source = {n.attrib["Name"]: n.attrib["Value"] for n in message.findall("tt:Source/tt:SimpleItem", NS)}
                assert "VideoSource" in source and "RuleName" in source
                value = message.find("tt:Data/tt:SimpleItem", NS)
                assert value.attrib["Name"] == "State"
                if source["RuleName"] == second:
                    assert value.attrib["Value"] == "false", ET.tostring(message)
                if source["RuleName"] == name:
                    if message.attrib["PropertyOperation"] == "Initialized":
                        seen_initialized = True
                    elif message.attrib["PropertyOperation"] == "Changed":
                        seen_on |= value.attrib["Value"] == "true"
                        seen_off |= value.attrib["Value"] == "false"
        assert seen_on and seen_off and seen_initialized and seen_crossed, (
            seen_on, seen_off, seen_initialized, seen_crossed)
        assert count_initialized and count_changed, (count_initialized, count_changed)
        soap("ModifyRules", token + rule(name, False))
        result = soap("GetRules", token)
        modified = next(r for r in result.findall(".//tan:Rule", NS) if r.attrib["Name"] == name)
        assert modified.find("tt:Parameters/tt:ElementItem/axt:MotionRegionConfig", NS).attrib["Armed"] == "false"
        soap("ModifyRules", token + line_rule("Left"))
        modified = next(r for r in soap("GetRules", token).findall(".//tan:Rule", NS)
                        if r.attrib["Name"] == line_name)
        assert modified.find("tt:Parameters/tt:SimpleItem", NS).attrib["Value"] == "Left"
        reset_time = (datetime.now(timezone.utc) + timedelta(seconds=4)).strftime("%H:%M:%SZ")
        soap("ModifyRules", token + count_rule(
            '<tt:SimpleItem Name="ReportTimeInterval" Value="PT0S"/>'
            '<tt:SimpleItem Name="PassAllPolylines" Value="true"/>'
            f'<tt:SimpleItem Name="ResetTime" Value="{reset_time}"/>'))
        seen_reset = False
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline and not seen_reset:
            events = soap("PullMessages", "<tet:Timeout>PT2S</tet:Timeout><tet:MessageLimit>100</tet:MessageLimit>",
                          prefix="tet", url=subscription)
            for notification in events.findall(".//wsnt:NotificationMessage", NS):
                if not (notification.find("wsnt:Topic", NS).text or "").endswith("CountAggregation/Counter"):
                    continue
                message = notification.find("wsnt:Message/tt:Message", NS)
                source = {n.attrib["Name"]: n.attrib["Value"]
                          for n in message.findall("tt:Source/tt:SimpleItem", NS)}
                value = message.find("tt:Data/tt:SimpleItem", NS)
                seen_reset |= (source["Rule"] == count_name and message.attrib["PropertyOperation"] == "Changed"
                               and value.attrib["Name"] == "Count" and value.attrib["Value"] == "0")
        assert seen_reset, "LineCounting did not report its scheduled reset"
        # Release one slot, then check A.3 using the same active subscription.
        soap("DeleteRules", token + f"<tan:RuleName>{second}</tan:RuleName>")
        soap("CreateRules", token + field_rule("<tt:Point x='0' y='0'/><tt:Point x='0' y='0'/><tt:Point x='1' y='1'/>"),
             fault="InvalidRule")
        soap("CreateRules", token + field_rule())
        soap("CreateRules", token + field_rule(), fault="RuleAlreadyExistent")
        for service, prefix, operation in [("media", "trt", "GetVideoAnalyticsConfigurations"),
                                           ("media2", "tr2", "GetAnalyticsConfigurations")]:
            media = soap(operation, service=service, prefix=prefix)
            field = next(r for r in media.findall(".//tt:Rule", NS) if r.attrib["Name"] == field_name)
            assert field.attrib["Type"] == "tt:FieldDetector"
            assert len(field.findall("tt:Parameters/tt:ElementItem/tt:Polygon/tt:Point", NS)) == 4
        soap("ModifyRules", token + field_rule())
        soap("SetSynchronizationPoint", prefix="tet", url=subscription)
        field_on = field_off = field_init = False
        deadline = time.monotonic() + 20
        while time.monotonic() < deadline and not (field_on and field_off and field_init):
            events = soap("PullMessages", "<tet:Timeout>PT2S</tet:Timeout><tet:MessageLimit>100</tet:MessageLimit>",
                          prefix="tet", url=subscription)
            for notification in events.findall(".//wsnt:NotificationMessage", NS):
                if not (notification.find("wsnt:Topic", NS).text or "").endswith("FieldDetector/ObjectsInside"):
                    continue
                message = notification.find("wsnt:Message/tt:Message", NS)
                source = {n.attrib["Name"]: n.attrib["Value"] for n in message.findall("tt:Source/tt:SimpleItem", NS)}
                assert source == {"VideoSource": "VideoSource0", "AnalyticsConfiguration": args.token, "Rule": field_name}
                value = message.find("tt:Data/tt:SimpleItem", NS)
                assert value.attrib["Name"] == "IsInside" and value.attrib["Value"] in {"true", "false"}
                field_init |= message.attrib["PropertyOperation"] == "Initialized"
                if message.attrib["PropertyOperation"] == "Changed":
                    field_on |= value.attrib["Value"] == "true"
                    field_off |= value.attrib["Value"] == "false"
        assert field_on and field_off and field_init, "FieldDetector property transitions missing"
        soap("DeleteRules", token + f"<tan:RuleName>{field_name}</tan:RuleName>")
        deleted = False
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline and not deleted:
            events = soap("PullMessages", "<tet:Timeout>PT1S</tet:Timeout><tet:MessageLimit>100</tet:MessageLimit>",
                          prefix="tet", url=subscription)
            for notification in events.findall(".//wsnt:NotificationMessage", NS):
                if (notification.find("wsnt:Topic", NS).text or "").endswith("FieldDetector/ObjectsInside"):
                    message = notification.find("wsnt:Message/tt:Message", NS)
                    deleted |= message.attrib["PropertyOperation"] == "Deleted"
        assert deleted, "FieldDetector Deleted event missing"
        caps = soap("GetServiceCapabilities").find(".//tan:Capabilities", NS)
        assert caps.attrib["ImageSendingType"] == "Embedded LocalStorage"
        plate_description = properties.find(".//Recognition/LicensePlate/tt:MessageDescription", NS)
        assert plate_description is not None and plate_description.attrib["IsProperty"] == "false"
        elements = {i.attrib["Name"]: i.attrib["Type"] for i in plate_description.findall("tt:Data/tt:ElementItemDescription", NS)}
        assert elements == {"LicensePlateInfo": "tt:LicensePlateInfo", "VehicleInfo": "tt:VehicleInfo",
                            "BoundingBox": "tt:Rectangle", "Image": "xsd:base64Binary", "VehicleImage": "xsd:base64Binary"}
        plate_rule = (f'<tan:Rule Name="{plate_name}" Type="tt:LicensePlateRecognition"><tt:Parameters>'
                      '<tt:SimpleItem Name="EventInterval" Value="3"/>'
                      '</tt:Parameters></tan:Rule>')
        soap("GetRuleOptions", "<tan:RuleType>tt:LicensePlateRecognition</tan:RuleType>" + token)
        soap("CreateRules", token + plate_rule)
        for service, prefix, op in [("media", "trt", "GetVideoAnalyticsConfigurations"),
                                    ("media2", "tr2", "GetAnalyticsConfigurations")]:
            configs = soap(op, service=service, prefix=prefix)
            assert any(r.attrib["Name"] == plate_name for r in configs.findall(".//tt:Rule", NS))
        expected_vehicles = {
            "TEST123": ("US", "Car", "Test Brand", "Synthetic Vehicle", ""),
            "TEST456": ("GB", "Car", "Test Motors", "Red SUV", "-TEST456"),
            "TEST789": ("DE", "Truck", "Test Cargo", "White Van", "-TEST789"),
            "TEST321": ("FR", "Bus", "Test Transit", "Yellow Minibus", "-TEST321"),
            "TEST654": ("IT", "Motorcycle", "Test Riders", "Green Motorcycle", "-TEST654"),
        }
        received = {}
        repeated = False
        image_payloads = {}
        deadline = time.monotonic() + 2 * len(expected_vehicles) + 4
        while time.monotonic() < deadline and (len(received) < len(expected_vehicles) or not repeated):
            events = soap("PullMessages", "<tet:Timeout>PT1S</tet:Timeout><tet:MessageLimit>100</tet:MessageLimit>",
                          prefix="tet", url=subscription)
            for notification in events.findall(".//wsnt:NotificationMessage", NS):
                if not (notification.find("wsnt:Topic", NS).text or "").endswith("Recognition/LicensePlate"):
                    continue
                message = notification.find("wsnt:Message/tt:Message", NS)
                assert "PropertyOperation" not in message.attrib
                assert {i.attrib["Name"] for i in message.findall("tt:Source/tt:SimpleItem", NS)} == {"VideoSource", "Rule"}
                likelihood = message.find("tt:Data/tt:SimpleItem[@Name='Likelihood']", NS)
                assert 0 <= float(likelihood.attrib["Value"]) <= 1
                plate = message.find("tt:Data/tt:ElementItem[@Name='LicensePlateInfo']/tt:LicensePlateInfo", NS)
                number = plate.find("tt:PlateNumber", NS).text
                assert number in expected_vehicles
                country, vehicle_type, brand, model, suffix = expected_vehicles[number]
                assert plate.find("tt:CountryCode", NS).text == country
                values = {i.attrib["Name"]: i.attrib["Value"] for i in message.findall("tt:Data/tt:SimpleItem", NS)}
                assert float(values["Speed"]) >= 0 and values["Direction"] in {"Left", "Right", "Any"}
                vehicle = message.find("tt:Data/tt:ElementItem[@Name='VehicleInfo']/tt:VehicleInfo", NS)
                assert vehicle.find("tt:Type", NS).text == vehicle_type
                assert vehicle.find("tt:Brand", NS).text == brand
                assert vehicle.find("tt:Model", NS).text == model
                box = message.find("tt:Data/tt:ElementItem[@Name='BoundingBox']/tt:BoundingBox", NS)
                assert -1 <= float(box.attrib["left"]) <= float(box.attrib["right"]) <= 1
                assert -1 <= float(box.attrib["bottom"]) <= float(box.attrib["top"]) <= 1
                images = {}
                for field, uri_name in [("Image", "ImageURI"), ("VehicleImage", "VehicleImageURI")]:
                    binary = base64.b64decode(message.find(f"tt:Data/tt:ElementItem[@Name='{field}']/tt:{field}", NS).text, validate=True)
                    images[field] = binary
                    target = urlsplit(values[uri_name])
                    assert target.hostname == args.host and target.port == args.port
                    kind = "plate" if field == "Image" else "vehicle"
                    assert target.path == f"/analytics/lpr/{kind}{suffix}.png"
                    connection = http.client.HTTPConnection(target.hostname, target.port, timeout=10)
                    try:
                        connection.request("GET", target.path)
                        response = connection.getresponse()
                        assert response.status == 200 and response.getheader("Content-Type") == "image/png"
                        assert response.read() == binary and binary.startswith(b"\x89PNG\r\n\x1a\n")
                    finally:
                        connection.close()
                assert images["Image"] != images["VehicleImage"], "Plate image must differ from vehicle image"
                image_payloads[number] = (images["Image"], images["VehicleImage"])
                timestamp = datetime.fromisoformat(message.attrib["UtcTime"].replace("Z", "+00:00"))
                if number in received:
                    assert (timestamp - received[number]).total_seconds() >= 3
                    repeated = True
                received[number] = timestamp
        assert set(received) == set(expected_vehicles), "Not all vehicles produced events"
        assert repeated, "No repeated plate to verify EventInterval"
        for kind in (0, 1):
            assert len({pair[kind] for pair in image_payloads.values()}) == len(expected_vehicles), "Vehicle image pairs must be distinct"
        # Modification clears repeat state; exercise both non-embedded modes on the wire.
        for mode in ("LocalStorage", ""):
            changed = (f'<tan:Rule Name="{plate_name}" Type="tt:LicensePlateRecognition"><tt:Parameters>'
                       f'<tt:SimpleItem Name="IncludeImage" Value="{mode}"/>'
                       '<tt:SimpleItem Name="EventInterval" Value="0"/>'
                       '</tt:Parameters></tan:Rule>')
            soap("ModifyRules", token + changed)
            checked = False
            deadline = time.monotonic() + 5
            while time.monotonic() < deadline and not checked:
                events = soap("PullMessages", "<tet:Timeout>PT1S</tet:Timeout><tet:MessageLimit>100</tet:MessageLimit>",
                              prefix="tet", url=subscription)
                for notification in events.findall(".//wsnt:NotificationMessage", NS):
                    if not (notification.find("wsnt:Topic", NS).text or "").endswith("Recognition/LicensePlate"):
                        continue
                    data = notification.find("wsnt:Message/tt:Message/tt:Data", NS)
                    assert data.find("tt:ElementItem[@Name='Image']", NS) is None
                    assert data.find("tt:ElementItem[@Name='VehicleImage']", NS) is None
                    for uri_name in ("ImageURI", "VehicleImageURI"):
                        assert (data.find(f"tt:SimpleItem[@Name='{uri_name}']", NS) is not None) == bool(mode)
                    checked = True
            assert checked, f"Missing LPR event for IncludeImage={mode!r}"
        soap("DeleteRules", token + f"<tan:RuleName>{plate_name}</tan:RuleName>")
        print("PASS Motion Region, Line Detector, LineCounting, Field Detector and License Plate Recognition: CRUD, options, faults, "
              "Media/Media2, PullPoint, image modes/downloads, vehicle fields, property transitions, counts and reset")
    finally:
        if subscription:
            soap("Unsubscribe", prefix="wsnt", url=subscription)
        existing = soap("GetRules", token).findall(".//tan:Rule", NS)
        cleanup = [r.attrib["Name"] for r in existing
                   if r.attrib["Name"] in {name, second, line_name, count_name, field_name, plate_name, name + "_rollback", name + "_overflow"}]
        if cleanup:
            soap("DeleteRules", token + "".join(f"<tan:RuleName>{n}</tan:RuleName>" for n in cleanup))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8080)
    parser.add_argument("--token", default="AnalyticsConfig_0")
    parser.add_argument("--capture", help="Save SOAP responses for optional XSD validation")
    run(parser.parse_args())
