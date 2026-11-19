#!/usr/bin/env python3
"""Face Recognition G.2 wire checks against a disposable server configuration."""
import argparse
import base64
import http.client
import time
import uuid
import xml.etree.ElementTree as ET
from pathlib import Path
from urllib.parse import urlsplit
from xml.sax.saxutils import escape
from analytics_smoke import NS


def run(args):
    capture_index = 0

    def soap(op, body="", prefix="tan", url=None, fault=None):
        nonlocal capture_index
        action = NS[prefix] + "/" + op
        if url:
            action = ("http://docs.oasis-open.org/wsn/bw-2/SubscriptionManager/UnsubscribeRequest"
                      if op == "Unsubscribe" else
                      "http://www.onvif.org/ver10/events/wsdl/PullPointSubscription/" + op + "Request")
        envelope = ('<s:Envelope ' + " ".join(f'xmlns:{p}="{uri}"' for p, uri in NS.items()) +
                    f'><s:Header><wsa:Action>{action}</wsa:Action><wsa:To>{escape(url or "")}</wsa:To>'
                    f'<wsa:MessageID>urn:uuid:{uuid.uuid4()}</wsa:MessageID></s:Header><s:Body>'
                    f'<{prefix}:{op}>{body}</{prefix}:{op}></s:Body></s:Envelope>')
        target = urlsplit(url) if url else None
        service = "event" if prefix == "tet" else "analytics"
        connection = http.client.HTTPConnection(target.hostname if target else args.host,
                                                target.port if target else args.port, timeout=15)
        try:
            connection.request("POST", target.path if target else f"/onvif/{service}_service", envelope,
                               {"Content-Type": f'application/soap+xml; action="{action}"'})
            response = connection.getresponse()
            payload = response.read()
            if args.capture:
                directory = Path(args.capture)
                directory.mkdir(parents=True, exist_ok=True)
                (directory / f"{capture_index:03d}-{op}.xml").write_bytes(payload)
                capture_index += 1
            tree = ET.fromstring(payload)
            if fault:
                assert response.status >= 400, payload
                assert tree.find(".//s:Subcode/s:Subcode/s:Value", NS).text == "ter:" + fault, payload
            else:
                assert response.status == 200 and tree.find(".//s:Fault", NS) is None, payload
            return tree
        finally:
            connection.close()

    def download(uri):
        target = urlsplit(uri)
        assert target.hostname == args.host and target.port == args.port, uri
        connection = http.client.HTTPConnection(target.hostname, target.port, timeout=10)
        try:
            connection.request("GET", target.path)
            response = connection.getresponse()
            assert response.status == 200 and response.getheader("Content-Type") == "image/png"
            data = response.read()
            assert data.startswith(b"\x89PNG\r\n\x1a\n")
            return data
        finally:
            connection.close()

    token = f"<tan:ConfigurationToken>{args.token}</tan:ConfigurationToken>"
    name = "FaceSmoke_" + uuid.uuid4().hex
    subscription = None
    created = False

    def rule(mode="Embedded", threshold="0.5"):
        return (f'<tan:Rule Name="{name}" Type="tt:FaceRecognition"><tt:Parameters>'
                f'<tt:SimpleItem Name="IncludeImage" Value="{mode}"/>'
                f'<tt:SimpleItem Name="Threshold" Value="{threshold}"/>'
                '</tt:Parameters></tan:Rule>')

    try:
        supported = soap("GetSupportedRules", token)
        desc = supported.find(".//tt:RuleDescription[@Name='tt:FaceRecognition']", NS)
        assert desc is not None
        assert desc.find("tt:Messages", NS).attrib["IsProperty"] == "false"
        assert desc.find("tt:Messages/tt:ParentTopic", NS).text == "tns1:RuleEngine/Recognition/Face"
        expected_simple = {"Likelihood", "Label", "EnrollmentID", "ImageUri", "RefImageUri"}
        expected_elements = {"ObjectId", "ParentObjectId", "BoundingBox", "Image"}
        properties = soap("GetEventProperties", prefix="tet")
        face_desc = properties.find(".//Face/tt:MessageDescription", NS)
        assert face_desc is not None and face_desc.attrib["IsProperty"] == "false"
        for description in (desc.find("tt:Messages", NS), face_desc):
            assert {n.attrib["Name"] for n in description.findall("tt:Data/tt:SimpleItemDescription", NS)} == expected_simple
            assert {n.attrib["Name"] for n in description.findall("tt:Data/tt:ElementItemDescription", NS)} == expected_elements
        options = soap("GetRuleOptions", "<tan:RuleType>tt:FaceRecognition</tan:RuleType>" + token)
        assert len(options.findall(".//tan:RuleOptions", NS)) == 2
        soap("CreateRules", token + rule("RemoteStorage"), fault="InvalidRule")
        soap("CreateRules", token + rule(threshold="1.1"), fault="InvalidRule")
        soap("CreateRules", token + rule())
        created = True
        soap("CreateRules", token + rule(), fault="RuleAlreadyExistent")
        assert soap("GetRules", token).find(f".//tan:Rule[@Name='{name}']", NS) is not None
        subscription = soap("CreatePullPointSubscription", prefix="tet").find(".//wsa:Address", NS).text
        for mode in ("Embedded", "LocalStorage", ""):
            if mode != "Embedded":
                soap("ModifyRules", token + rule(mode))
                # Discard notifications queued before the change.
                soap("PullMessages", "<tet:Timeout>PT1S</tet:Timeout><tet:MessageLimit>100</tet:MessageLimit>",
                     prefix="tet", url=subscription)
            seen, payloads = set(), set()
            deadline = time.monotonic() + 12
            while len(seen) < 3 and time.monotonic() < deadline:
                events = soap("PullMessages", "<tet:Timeout>PT1S</tet:Timeout><tet:MessageLimit>100</tet:MessageLimit>",
                              prefix="tet", url=subscription)
                for notification in events.findall(".//wsnt:NotificationMessage", NS):
                    if (notification.find("wsnt:Topic", NS).text or "") != "tns1:RuleEngine/Recognition/Face":
                        continue
                    msg = notification.find("wsnt:Message/tt:Message", NS)
                    assert "PropertyOperation" not in msg.attrib
                    sources = {n.attrib["Name"]: n.attrib["Value"] for n in msg.findall("tt:Source/tt:SimpleItem", NS)}
                    assert sources == {"VideoSource": "VideoSource0", "Rule": name}, sources
                    values = {n.attrib["Name"]: n.attrib["Value"] for n in msg.findall("tt:Data/tt:SimpleItem", NS)}
                    assert set(values) == expected_simple - ({"ImageUri"} if not mode else set())
                    identity = values["EnrollmentID"]
                    index = int(identity[-3:])
                    assert identity == f"TEST-PERSON-{index:03d}" and 1 <= index <= 3
                    assert float(values["Likelihood"]) == 0.98
                    assert values["Label"] == ("Whitelisted", "Blacklisted", "Temporary")[index - 1]
                    assert urlsplit(values["RefImageUri"]).path == f"/analytics/face/face-{index:03d}.png"
                    binary = download(values["RefImageUri"])
                    payloads.add(binary)
                    elements = {n.attrib["Name"]: n[0] for n in msg.findall("tt:Data/tt:ElementItem", NS)}
                    assert set(elements) == expected_elements - ({"Image"} if mode != "Embedded" else set())
                    assert int(elements["ObjectId"].attrib["ObjectId"]) == index
                    assert int(elements["ParentObjectId"].attrib["ObjectId"]) == index + 100
                    box = {k: float(v) for k, v in elements["BoundingBox"].attrib.items()}
                    assert -1 <= box["left"] < box["right"] <= 1
                    assert -1 <= box["bottom"] < box["top"] <= 1
                    if mode:
                        assert download(values["ImageUri"]) == binary
                    if mode == "Embedded":
                        assert base64.b64decode(elements["Image"].text, validate=True) == binary
                    seen.add(identity)
            assert len(seen) == 3 and len(payloads) == 3, (mode, seen)
        print("PASS Face Recognition: descriptions, CRUD/faults, three identities, image modes, HTTP/base64 and PullPoint")
    finally:
        if subscription:
            soap("Unsubscribe", prefix="wsnt", url=subscription)
        if created:
            soap("DeleteRules", token + f"<tan:RuleName>{name}</tan:RuleName>")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8080)
    parser.add_argument("--token", default="AnalyticsConfig_0")
    parser.add_argument("--capture")
    run(parser.parse_args())
