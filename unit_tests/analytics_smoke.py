#!/usr/bin/env python3
"""Rules and PullPoint checks. Run against a disposable configuration with auth disabled.

Creates four rules and removes them on completion. Requires Python 3 only.
"""
import argparse
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
        supported = soap("GetSupportedRules", token)
        desc = supported.find(".//tt:RuleDescription", NS)
        assert desc.attrib["Name"] == "tt:MotionRegionDetector" and desc.attrib["maxInstances"] == "4"
        line_desc = next(d for d in supported.findall(".//tt:RuleDescription", NS)
                         if d.attrib["Name"] == "tt:LineDetector")
        assert line_desc.find("tt:Messages", NS).attrib["IsProperty"] == "false"
        count_desc = next(d for d in supported.findall(".//tt:RuleDescription", NS)
                          if d.attrib["Name"] == "tt:LineCounting")
        assert count_desc.find("tt:Messages", NS).attrib["IsProperty"] == "true"
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
        print("PASS Motion Region, Line Detector and LineCounting: CRUD, options, faults, Media/Media2, "
              "PullPoint, aggregated counts and scheduled reset")
    finally:
        if subscription:
            soap("Unsubscribe", prefix="wsnt", url=subscription)
        existing = soap("GetRules", token).findall(".//tan:Rule", NS)
        cleanup = [r.attrib["Name"] for r in existing
                   if r.attrib["Name"] in {name, second, line_name, count_name, name + "_rollback", name + "_overflow"}]
        if cleanup:
            soap("DeleteRules", token + "".join(f"<tan:RuleName>{n}</tan:RuleName>" for n in cleanup))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8080)
    parser.add_argument("--token", default="AnalyticsConfig_0")
    parser.add_argument("--capture", help="Save SOAP responses for optional XSD validation")
    run(parser.parse_args())
