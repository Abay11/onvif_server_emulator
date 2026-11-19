#!/usr/bin/env python3
"""Check independent PullPoint delivery against a running default emulator."""
import argparse
import http.client
import uuid
from urllib.parse import urlsplit
import xml.etree.ElementTree as ET

S = "http://www.w3.org/2003/05/soap-envelope"
E = "http://www.onvif.org/ver10/events/wsdl"
W = "http://docs.oasis-open.org/wsn/b-2"
A = "http://www.w3.org/2005/08/addressing"
T = "http://www.onvif.org/ver10/schema"


def call(url, operation, body="", expected=200):
    namespace = W if operation in ("Renew", "Unsubscribe") else E
    if operation in ("Renew", "Unsubscribe"):
        action = f"http://docs.oasis-open.org/wsn/bw-2/SubscriptionManager/{operation}Request"
    elif operation == "CreatePullPointSubscription":
        action = f"{E}/EventPortType/CreatePullPointSubscriptionRequest"
    else:
        action = f"{E}/PullPointSubscription/{operation}Request"
    if operation == "Renew" and not body:
        body = "<n:TerminationTime>PT60S</n:TerminationTime>"
    payload = (f'<s:Envelope xmlns:s="{S}" xmlns:wsa="{A}" xmlns:e="{E}" xmlns:n="{namespace}">'
               f'<s:Header><wsa:To>{url}</wsa:To><wsa:Action>{action}</wsa:Action>'
               f'<wsa:MessageID>urn:uuid:{uuid.uuid4()}</wsa:MessageID></s:Header>'
               f'<s:Body><n:{operation}>{body}</n:{operation}></s:Body></s:Envelope>')
    endpoint = urlsplit(url)
    connection = http.client.HTTPConnection(endpoint.hostname, endpoint.port, timeout=70)
    try:
        connection.request("POST", endpoint.path, payload, {"Content-Type": "application/soap+xml"})
        response = connection.getresponse()
        data = response.read()
        assert response.status == expected, (operation, response.status, data)
        return ET.fromstring(data)
    finally:
        connection.close()


def pull(url, limit=1024):
    root = call(url, "PullMessages", f"<e:Timeout>PT1S</e:Timeout><e:MessageLimit>{limit}</e:MessageLimit>")
    return root.findall(f".//{{{W}}}NotificationMessage")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--http-port", type=int, default=8080)
    args = parser.parse_args()
    endpoint = f"http://{args.host}:{args.http_port}/onvif/event_service"
    subscriptions = []
    try:
        for _ in range(2):
            root = call(endpoint, "CreatePullPointSubscription")
            subscriptions.append(root.find(f".//{{{A}}}Address").text)
        first, second = subscriptions
        assert first != second, subscriptions
        for url in subscriptions:
            messages = pull(url, 1)
            assert len(messages) == 1, messages
            assert messages[0].find(f".//{{{T}}}Message").get("PropertyOperation") == "Initialized"
        seen = [set(), set()]
        for _ in range(4):
            for index, url in enumerate(subscriptions):
                for message in pull(url):
                    if message.find(f".//{{{T}}}Message").get("PropertyOperation") != "Initialized":
                        seen[index].add(ET.tostring(message))
            if seen[0] & seen[1]:
                break
        assert seen[0] & seen[1], "Subscribers did not receive an identical live notification"
        call(first, "SetSynchronizationPoint")
        assert any(message.find(f".//{{{T}}}Message").get("PropertyOperation") == "Initialized"
                   for message in pull(first))
        call(first, "Unsubscribe")
        subscriptions.remove(first)
        unknown = call(first, "Renew", expected=500)
        assert unknown.find(".//{http://docs.oasis-open.org/wsrf/r-2}ResourceUnknownFault") is not None
        call(second, "Renew")
        assert pull(second), "Second subscription stopped when the first unsubscribed"
        print("PASS two unique PullPoints, initial state, MessageLimit, identical live events, "
              "synchronization, independent unsubscribe and ResourceUnknown fault")
    finally:
        for url in subscriptions:
            call(url, "Unsubscribe")


if __name__ == "__main__":
    main()
