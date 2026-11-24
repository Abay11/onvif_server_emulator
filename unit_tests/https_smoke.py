#!/usr/bin/env python3
"""HTTP/HTTPS transport matrix against source-built server and isolated config copies."""
import argparse
from copy import deepcopy
import http.client
from http.client import HTTPException
import json
import os
from pathlib import Path
import shutil
import socket
import ssl
import subprocess
import sys
import tempfile
import time
import uuid
import urllib.request
from urllib.parse import urlsplit
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
SOAP = 'http://www.w3.org/2003/05/soap-envelope'
NETWORK_SCHEMA = None
FAULT_SCHEMA = None


def request(scheme, port, path, body=None, headers=None, context=None):
    connection = (http.client.HTTPSConnection('127.0.0.1', port, timeout=5, context=context)
                  if scheme == 'https' else http.client.HTTPConnection('127.0.0.1', port, timeout=5))
    try:
        connection.request('POST' if body is not None else 'GET', path, body, headers or {})
        response = connection.getresponse()
        return response.status, dict(response.getheaders()), response.read()
    finally:
        connection.close()


def soap(scheme, port, service, operation, body='', context=None, expected=200):
    ns = {'device': 'http://www.onvif.org/ver10/device/wsdl', 'event': 'http://www.onvif.org/ver10/events/wsdl', 'media': 'http://www.onvif.org/ver10/media/wsdl', 'media2': 'http://www.onvif.org/ver20/media/wsdl'}[service]
    xml = f'<s:Envelope xmlns:s="{SOAP}" xmlns:t="{ns}"><s:Body><t:{operation}>{body}</t:{operation}></s:Body></s:Envelope>'
    if NETWORK_SCHEMA is not None and operation in ('GetNetworkProtocols', 'GetServices') and expected == 200:
        NETWORK_SCHEMA.validate(ET.tostring(ET.fromstring(xml).find(f'{{{SOAP}}}Body')[0]))
    status, _, data = request(scheme, port, f'/onvif/{service}_service', xml,
                              {'Content-Type': 'application/soap+xml'}, context)
    assert status == expected, (status, data)
    result = ET.fromstring(data)
    if NETWORK_SCHEMA is not None and operation in ('GetNetworkProtocols', 'GetServices'):
        if expected == 200:
            NETWORK_SCHEMA.validate(ET.tostring(result.find(f'{{{SOAP}}}Body')[0]))
        else:
            from lxml import etree
            FAULT_SCHEMA.assertValid(etree.fromstring(data))
    return result


def values(tree, name):
    return [item.text for item in tree.iter() if item.tag.split('}')[-1] == name]


def stopped(port):
    with socket.socket() as sock:
        sock.settimeout(1)
        assert sock.connect_ex(('127.0.0.1', port)) != 0, f'Unexpected listener on {port}'


def exercise(http, https, https_port, context, network=False, browser=False, pullpoint_port=None, advertised_https=False):
    scheme, port = ('http', 8080) if http else ('https', https_port)
    base = f'https://127.0.0.1:{https_port}/' if advertised_https else f'{scheme}://127.0.0.1:{port}/'
    for protocol, enabled, listen_port in [('http', http, 8080), ('https', https, https_port)]:
        if not enabled:
            stopped(listen_port)
            continue
        status, _, data = request(protocol, listen_port, '/snapshot.jpeg', context=context)
        assert status == 200 and data.startswith(b'\xff\xd8')
        service_base = f'{protocol}://127.0.0.1:{listen_port}/'
        for include in ('false', 'true'):
            services = soap(protocol, listen_port, 'device', 'GetServices',
                            f'<t:IncludeCapability>{include}</t:IncludeCapability>', context=context)
            assert values(services, 'XAddr') and all(addr.startswith(service_base) for addr in values(services, 'XAddr'))
        media2 = next(node for node in services.iter() if node.tag.endswith('}Service') and
                      values(node, 'Namespace') == ['http://www.onvif.org/ver20/media/wsdl'])
        address = urlsplit(values(media2, 'XAddr')[0])
        payload = (f'<s:Envelope xmlns:s="{SOAP}" xmlns:m="http://www.onvif.org/ver20/media/wsdl">'
                   '<s:Body><m:GetProfiles><m:Type>VideoSource</m:Type><m:Type>VideoEncoder</m:Type>'
                   '</m:GetProfiles></s:Body></s:Envelope>')
        status, _, profiles = request(address.scheme, address.port, address.path, payload,
                                       {'Content-Type': 'application/soap+xml'}, context)
        assert status == 200 and b'ProfileToken0' in profiles, (status, profiles)
        capabilities = soap(protocol, listen_port, 'device', 'GetCapabilities', context=context)
        assert values(capabilities, 'XAddr') and all(addr.startswith(base) for addr in values(capabilities, 'XAddr'))
        protocols = soap(protocol, listen_port, 'device', 'GetNetworkProtocols', context=context)
        entries = [node for node in protocols.iter() if node.tag.endswith('}NetworkProtocols')]
        assert len(entries) == 3
        assert [(values(node, 'Name')[0], values(node, 'Enabled')[0], int(values(node, 'Port')[0]))
                for node in entries] == [('HTTP', str(http).lower(), 8080),
                                         ('HTTPS', str(https).lower(), https_port), ('RTSP', 'true', 8554)]
        fault = soap(protocol, listen_port, 'device', 'GetNetworkProtocols', '<t:Unexpected/>', context=context, expected=400)
        assert 'ter:InvalidArgVal' in values(fault, 'Value')
        for media in ('media', 'media2'):
            snapshot = soap(protocol, listen_port, media, 'GetSnapshotUri', '<t:ProfileToken>ProfileToken0</t:ProfileToken>', context=context)
            assert values(snapshot, 'Uri') == [base + 'snapshot.jpeg']
        capabilities = soap(protocol, listen_port, 'device', 'GetServiceCapabilities', context=context)
        security = next(item for item in capabilities.iter() if item.tag.endswith('}Security'))
        assert security.attrib['TLS1.2'] == str(https).lower()
        assert security.attrib['TLS1.0'] == security.attrib['TLS1.1'] == 'false'
        fault = soap(protocol, listen_port, 'device', 'SystemReboot', '<t:Unexpected/>', context=context, expected=400)
        assert any(item.tag == f'{{{SOAP}}}Fault' for item in fault.iter())
        # An unauthenticated API call must still fail, even with SOAP authentication disabled.
        assert request(protocol, listen_port, '/admin/api/status', context=context)[0] == 401
        status, headers, _ = request(protocol, listen_port, '/admin/api/login',
                                     json.dumps({'login': 'a', 'password': 'a1'}),
                                     {'Content-Type': 'application/json', 'X-ONVIF-Admin': 'login'}, context)
        assert status == 200
        cookie = headers['Set-Cookie']
        assert ('; Secure' in cookie) == (protocol == 'https')
        auth = {'Cookie': cookie.split(';')[0]}
        status, _, data = request(protocol, listen_port, '/admin/api/status', headers=auth, context=context)
        assert status == 200
        settings = json.loads(data)['settings']['network']
        assert settings['http_enabled'] == str(http).lower()
        assert settings['https_enabled'] == str(https).lower()
        assert settings['http_endpoint'] == ('http://127.0.0.1:8080/' if http else 'Disabled')
        assert settings['service_endpoint'] == base
        status, _, data = request(protocol, listen_port, '/admin/api/configuration', headers=auth, context=context)
        entry = next(item for item in json.loads(data)['files'] if item['name'] == 'common.config')
        document = json.loads(entry['document'])
        headers = {**auth, 'Content-Type': 'application/json', 'X-ONVIF-Admin': 'configuration'}
        for invalid in ('disabled', 'same-port', 'certificate'):
            candidate = deepcopy(document)
            if invalid == 'disabled':
                candidate['http'] = {'enabled': False}; candidate['https']['enabled'] = False
            elif invalid == 'same-port':
                candidate['http'] = {'enabled': True}; candidate['https']['enabled'] = True
                candidate['addresses']['https_port'] = candidate['addresses']['http_port']
            else:
                candidate['https']['enabled'] = True
                candidate['https']['certificate'] = 'only-cert.pem'; candidate['https']['private_key'] = ''
            command = {'file': entry['name'], 'revision': entry['revision'], 'document': json.dumps(candidate)}
            result, _, data = request(protocol, listen_port, '/admin/api/configuration', json.dumps(command), headers, context)
            assert result == 422 and json.loads(data)['validation_errors'], (result, data)
        command = {'file': entry['name'], 'revision': entry['revision'], 'document': entry['document']}
        assert request(protocol, listen_port, '/admin/api/configuration', json.dumps(command), headers, context)[0] == 200
    subscription = soap(scheme, port, 'event', 'CreatePullPointSubscription', context=context)
    address = values(subscription, 'Address')[0]
    pull_scheme = 'https' if base.startswith('https:') else 'http'
    pull_port = pullpoint_port or (https_port if advertised_https else port)
    pullpoint_base = f'{pull_scheme}://127.0.0.1:{pullpoint_port}/' if pullpoint_port else base
    assert address.startswith(pullpoint_base), address
    path = '/' + address.split('/', 3)[3]
    payload = (f'<s:Envelope xmlns:s="{SOAP}" xmlns:t="http://www.onvif.org/ver10/events/wsdl" '
               'xmlns:wsa="http://www.w3.org/2005/08/addressing">'
               f'<s:Header><wsa:To>{address}</wsa:To>'
               '<wsa:Action>http://www.onvif.org/ver10/events/wsdl/PullPointSubscription/PullMessagesRequest</wsa:Action>'
               f'<wsa:MessageID>urn:uuid:{uuid.uuid4()}</wsa:MessageID></s:Header>'
               '<s:Body><t:PullMessages><t:Timeout>PT1S</t:Timeout><t:MessageLimit>1</t:MessageLimit>'
               '</t:PullMessages></s:Body></s:Envelope>')
    assert request(pull_scheme, pull_port, path, payload, {'Content-Type': 'application/soap+xml'}, context)[0] == 200
    if not http or advertised_https:
        identifier = f'urn:uuid:{uuid.uuid4()}'
        probe = (f'<s:Envelope xmlns:s="{SOAP}" xmlns:a="http://schemas.xmlsoap.org/ws/2004/08/addressing" '
                 'xmlns:d="http://schemas.xmlsoap.org/ws/2005/04/discovery">'
                 f'<s:Header><a:MessageID>{identifier}</a:MessageID>'
                 '<a:Action>http://schemas.xmlsoap.org/ws/2005/04/discovery/Probe</a:Action></s:Header>'
                 '<s:Body><d:Probe/></s:Body></s:Envelope>')
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as discovery:
            discovery.settimeout(5); discovery.sendto(probe.encode(), ('127.0.0.1', 3702))
            reply = ET.fromstring(discovery.recvfrom(65536)[0])
            assert values(reply, 'XAddrs') == [base + 'onvif/device_service']
    if https:
        try:
            request('http', https_port, '/snapshot.jpeg')
            raise AssertionError('HTTPS listener accepted plaintext HTTP')
        except (OSError, HTTPException):
            pass
        # Default development certificate must not pass normal system trust verification.
        if context.verify_mode == ssl.CERT_NONE:
            try:
                request('https', https_port, '/snapshot.jpeg', context=ssl.create_default_context())
                raise AssertionError('Self-signed certificate unexpectedly trusted')
            except ssl.SSLCertVerificationError:
                pass
    if network:
        subprocess.run(['python3', 'unit_tests/network_smoke.py', '--host', '127.0.0.1',
                        '--advertised-ip', '127.0.0.1', '--multicast-interface', '127.0.0.1'], check=True)
    if browser:
        browser_check()


def digest_check(context):
    xml = (f'<s:Envelope xmlns:s="{SOAP}" xmlns:t="http://www.onvif.org/ver10/media/wsdl">'
           '<s:Body><t:GetProfiles/></s:Body></s:Envelope>').encode()
    for scheme, port in [('http', 8080), ('https', 8443)]:
        status, _, _ = request(scheme, port, '/onvif/media_service', xml,
                               {'Content-Type': 'application/soap+xml'}, context)
        assert status == 401
        url = f'{scheme}://127.0.0.1:{port}/onvif/media_service'
        passwords = urllib.request.HTTPPasswordMgrWithDefaultRealm()
        passwords.add_password(None, url, 'a', 'a1')
        opener = urllib.request.build_opener(urllib.request.HTTPDigestAuthHandler(passwords),
                                             urllib.request.HTTPSHandler(context=context))
        with opener.open(urllib.request.Request(url, xml, {'Content-Type': 'application/soap+xml'}), timeout=5) as result:
            assert result.status == 200 and b'ProfileToken0' in result.read()
    services = soap('https', 8443, 'device', 'GetServices',
                    '<t:IncludeCapability>false</t:IncludeCapability>', context=context)
    media2 = next(node for node in services.iter() if node.tag.endswith('}Service') and
                  values(node, 'Namespace') == ['http://www.onvif.org/ver20/media/wsdl'])
    url = values(media2, 'XAddr')[0]
    assert url == 'https://127.0.0.1:8443/onvif/media2_service', url
    xml = (f'<s:Envelope xmlns:s="{SOAP}" xmlns:m="http://www.onvif.org/ver20/media/wsdl">'
           '<s:Body><m:GetProfiles><m:Type>VideoSource</m:Type><m:Type>VideoEncoder</m:Type>'
           '</m:GetProfiles></s:Body></s:Envelope>').encode()
    passwords = urllib.request.HTTPPasswordMgrWithDefaultRealm()
    passwords.add_password(None, url, 'a', 'a1')
    opener = urllib.request.build_opener(urllib.request.HTTPDigestAuthHandler(passwords),
                                         urllib.request.HTTPSHandler(context=context))
    with opener.open(urllib.request.Request(url, xml, {'Content-Type': 'application/soap+xml'}), timeout=5) as result:
        assert result.status == 200 and b'ProfileToken0' in result.read()
    print('PASS HTTP/HTTPS Digest and HTTPS GetServices -> Media2 GetProfiles without transport downgrade')


def reboot_check(context):
    headers = {'Content-Type': 'application/json', 'X-ONVIF-Admin': 'login'}
    status, response, _ = request('https', 9443, '/admin/api/login',
                                json.dumps({'login': 'a', 'password': 'a1'}), headers, context)
    assert status == 200
    headers['Cookie'] = response['Set-Cookie'].split(';')[0]
    headers['X-ONVIF-Admin'] = 'configuration'
    status, _, data = request('https', 9443, '/admin/api/configuration', headers=headers, context=context)
    assert status == 200
    entry = next(item for item in json.loads(data)['files'] if item['name'] == 'common.config')
    document = json.loads(entry['document']); document['addresses']['https_port'] = 10443
    command = {'file': entry['name'], 'revision': entry['revision'], 'document': json.dumps(document)}
    assert request('https', 9443, '/admin/api/configuration', json.dumps(command), headers, context)[0] == 200
    headers['X-ONVIF-Admin'] = 'reboot'
    assert request('https', 9443, '/admin/api/reboot', '{"confirm":true}', headers, context)[0] == 202
    for _ in range(100):
        try:
            if request('https', 10443, '/snapshot.jpeg', context=context)[0] == 200:
                break
        except (OSError, HTTPException):
            time.sleep(.1)
    else:
        raise AssertionError('New HTTPS port did not start after restart')
    stopped(8080); stopped(9443)
    assert request('https', 10443, '/admin/api/status', headers=headers, context=context)[0] == 401
    services = soap('https', 10443, 'device', 'GetServices',
                    '<t:IncludeCapability>false</t:IncludeCapability>', context=context)
    assert all(addr.startswith('https://127.0.0.1:10443/') for addr in values(services, 'XAddr'))
    print('PASS HTTPS save/restart: new port, updated advertised URLs, expired session')


def browser_check():
    from selenium import webdriver
    from selenium.webdriver.chrome.service import Service
    from selenium.webdriver.common.by import By
    from selenium.webdriver.support.ui import WebDriverWait
    options = webdriver.ChromeOptions()
    for arg in ('--headless', '--no-sandbox', '--disable-dev-shm-usage', '--ignore-certificate-errors', '--window-size=1440,1100'):
        options.add_argument(arg)
    with webdriver.Chrome(service=Service('/usr/bin/chromedriver'), options=options) as driver:
        driver.execute_cdp_cmd('Emulation.setEmulatedMedia', {'features': [{'name': 'prefers-reduced-motion', 'value': 'reduce'}]})
        driver.get('https://127.0.0.1:8443/admin/login')
        driver.find_element(By.ID, 'login-username').send_keys('a')
        driver.find_element(By.ID, 'login-password').send_keys('a1')
        driver.find_element(By.ID, 'login-submit').click()
        ready = WebDriverWait(driver, 15)
        ready.until(lambda d: d.current_url.endswith('/admin/'))
        driver.find_element(By.CSS_SELECTOR, '.nav-link[href="#network"]').click()
        ready.until(lambda d: d.find_element(By.ID, 'network-title').is_displayed())
        ready.until(lambda d: not d.find_element(By.ID, 'network-workspace').get_property('inert'))
        assert not any(item.text == 'HTTP / HTTPS connections' for item in driver.find_elements(By.CSS_SELECTOR, '#configuration-catalog button'))
        checks = driver.find_elements(By.CSS_SELECTOR, '#network-connections input[type=checkbox]')
        assert len(checks) == 2 and all(item.is_selected() for item in checks)
        from selenium.webdriver.support.ui import Select
        protocol = driver.find_element(By.CSS_SELECTOR, '[data-network-path="advertised.web_scheme"]')
        Select(protocol).select_by_value('https')
        port = driver.find_element(By.CSS_SELECTOR, '[data-network-path="addresses.https_port"]')
        port.clear(); port.send_keys('9443')
        driver.find_element(By.CSS_SELECTOR, '#network-connections button[type=submit]').click()
        ready.until(lambda d: d.find_element(By.ID, 'configuration-saved-dialog').get_property('open'))
        assert 'restart' in driver.find_element(By.ID, 'network-restart').text.lower()
        assert Select(driver.find_element(By.CSS_SELECTOR, '[data-network-path="advertised.web_scheme"]')).first_selected_option.get_attribute('value') == 'https'
        assert driver.find_element(By.CSS_SELECTOR, '[data-network-path="addresses.https_port"]').get_attribute('value') == '9443'
        print('PASS HTTPS browser: Network configuration navigation, advertised HTTPS selection, port save, restart notice')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, default=Path('build/main'))
    parser.add_argument('--network', action='store_true')
    parser.add_argument('--browser', action='store_true')
    parser.add_argument('--contract-dir', type=Path, help='Validate Device network/service responses using pinned ONVIF schemas')
    args = parser.parse_args()
    if args.contract_dir:
        sys.path.insert(0, str(ROOT / 'experiments/rtsp_metadata'))
        from contract import checked_imports, verify_repository, load_schema
        from lxml import etree
        from xmlschema import XMLSchema11
        root = args.contract_dir.resolve()
        verify_repository(root / 'specs')
        path = root / 'specs/wsdl/ver10/device/wsdl/devicemgmt.wsdl'
        wsdl = etree.parse(str(path), etree.XMLParser(no_network=True, resolve_entities=False))
        global NETWORK_SCHEMA, FAULT_SCHEMA
        NETWORK_SCHEMA = XMLSchema11(
            etree.tostring(wsdl.find('.//{http://www.w3.org/2001/XMLSchema}schema')),
            base_url=path.parent.as_uri() + '/', allow='local', use_fallback=False,
            uri_mapper={url: str(file.resolve()) for url, file in checked_imports(root).items()}, validation='lax')
        # Permit only the known schema-document annotation error in the pinned Device WSDL.
        # Payloads are still validated strictly, as in system_reboot_smoke.py.
        for error in NETWORK_SCHEMA.all_errors:
            if (error.path != '/xs:schema/xs:simpleType[2]/xs:annotation' or
                    error.message != 'character data between child elements not allowed'):
                raise error
        FAULT_SCHEMA = load_schema(root, resource_faults=True)
    binary = str(args.binary.resolve())
    for mode in ('both', 'prefer-https', 'prefer-https-pullpoint', 'https-only', 'https-pullpoint', 'http-only', 'custom-certificate', 'authentication', 'bind-conflict', 'disabled', 'same-port', 'missing-certificate', 'disabled-scheme'):
        with tempfile.TemporaryDirectory(prefix='https-smoke-') as directory:
            configs = Path(directory) / 'configs'
            shutil.copytree(ROOT / 'server_configs', configs)
            common = json.loads((configs / 'common.config').read_text())
            common['authentication'] = 'digest' if mode == 'authentication' else 'none'
            http, https = mode not in ('https-only', 'https-pullpoint'), mode != 'http-only'
            common['http']['enabled'], common['https']['enabled'] = http, https
            context = ssl._create_unverified_context()
            https_port = 8443
            if mode.startswith('prefer-https'):
                common['advertised']['web_scheme'] = 'https'
            if mode == 'https-only':
                https_port = 9443
                common['addresses']['https_port'] = https_port
            elif mode in ('https-pullpoint', 'prefer-https-pullpoint'):
                events = json.loads((configs / 'event.config').read_text())
                events['PullPoint']['UseHttpServerPort'] = False
                events['PullPoint']['Port'] = 5550
                (configs / 'event.config').write_text(json.dumps(events))
            elif mode == 'custom-certificate':
                subprocess.run(['openssl', 'req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-days', '1',
                                '-subj', '/CN=localhost', '-addext', 'subjectAltName=IP:127.0.0.1',
                                '-keyout', str(configs / 'key.pem'), '-out', str(configs / 'cert.pem')],
                               check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                common['https'].update(certificate='cert.pem', private_key='key.pem')
                context = ssl.create_default_context(cafile=str(configs / 'cert.pem'))
            elif mode == 'disabled':
                common['http']['enabled'] = common['https']['enabled'] = False
            elif mode == 'same-port':
                common['addresses']['https_port'] = common['addresses']['http_port']
            elif mode == 'missing-certificate':
                common['https'].update(certificate='absent.pem', private_key='absent-key.pem')
            if mode == 'disabled-scheme':
                common['https']['enabled'] = False; common['advertised']['web_scheme'] = 'https'
            (configs / 'common.config').write_text(json.dumps(common))
            environment = dict(os.environ, ONVIF_ADVERTISED_IPV4='127.0.0.1', ONVIF_DISCOVERY_INTERFACE_IPV4='127.0.0.1')
            occupied = None
            if mode == 'bind-conflict':
                occupied = socket.socket()
                occupied.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
                occupied.bind(('0.0.0.0', 8443)); occupied.listen()
            with (Path(directory) / 'server.log').open('w+') as log:
                process = subprocess.Popen([binary, str(configs)], env=environment, stdout=log, stderr=subprocess.STDOUT)
                try:
                    if mode in ('disabled', 'same-port', 'missing-certificate', 'bind-conflict', 'disabled-scheme'):
                        assert process.wait(timeout=10) != 0
                        stopped(8080)
                        if not occupied: stopped(8443)
                    else:
                        scheme, port = ('http', 8080) if http else ('https', https_port)
                        for _ in range(150):
                            assert process.poll() is None, 'Server exited during startup'
                            try:
                                if request(scheme, port, '/snapshot.jpeg', context=context)[0] == 200:
                                    break
                            except (OSError, HTTPException):
                                time.sleep(.1)
                        else:
                            raise AssertionError('Server startup timed out')
                        if mode == 'authentication':
                            digest_check(context)
                        else:
                            exercise(http, https, https_port, context, args.network and mode == 'both', args.browser and mode == 'both', 5550 if mode in ('https-pullpoint', 'prefer-https-pullpoint') else None, mode.startswith('prefer-https'))
                        if mode == 'https-only':
                            reboot_check(context)
                    print('PASS', mode)
                except BaseException:
                    log.flush(); log.seek(0); print(log.read())
                    raise
                finally:
                    if occupied: occupied.close()
                    process.terminate()
                    try:
                        process.wait(timeout=10)
                    except subprocess.TimeoutExpired:
                        process.kill(); process.wait()


if __name__ == '__main__':
    main()
