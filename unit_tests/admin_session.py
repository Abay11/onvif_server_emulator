"""Authenticated test client for the WebUI; never used by production code."""
import http.cookiejar
import json
import os
import urllib.error
import urllib.request
from urllib.parse import urlsplit

_login = os.environ.get("OSRV_TEST_ADMIN_LOGIN", "a")
_password = os.environ.get("OSRV_TEST_ADMIN_PASSWORD", "a1")
_opener = urllib.request.build_opener(urllib.request.HTTPCookieProcessor(http.cookiejar.CookieJar()))


def set_credentials(login, password):
    global _login, _password, _opener
    _login, _password = login, password
    _opener = urllib.request.build_opener(urllib.request.HTTPCookieProcessor(http.cookiejar.CookieJar()))


def urlopen(request, timeout=15):
    url = request.full_url if isinstance(request, urllib.request.Request) else request
    parsed = urlsplit(url)
    try:
        response = _opener.open(request, timeout=timeout)
        if parsed.path not in ("/admin", "/admin/") or urlsplit(response.geturl()).path.rstrip("/") != "/admin/login":
            return response
        response.close()
    except urllib.error.HTTPError as error:
        if error.code != 401 or not parsed.path.startswith("/admin/") or parsed.path == "/admin/api/login":
            raise
        error.close()
    login = urllib.request.Request(f"{parsed.scheme}://{parsed.netloc}/admin/api/login",
                                   json.dumps({"login": _login, "password": _password}).encode(),
                                   {"Content-Type": "application/json", "X-ONVIF-Admin": "login"})
    with _opener.open(login, timeout=timeout) as response:
        assert response.status == 200
    if isinstance(request, urllib.request.Request):
        request.remove_header("Cookie")
    return _opener.open(request, timeout=timeout)


def browser_login(browser, login=None, password=None):
    from selenium.webdriver.support.ui import WebDriverWait
    browser.get("http://127.0.0.1:8080/admin/login")
    browser.find_element("id", "login-username").send_keys(login or _login)
    browser.find_element("id", "login-password").send_keys(password or _password)
    browser.find_element("id", "login-submit").click()
    WebDriverWait(browser, 20).until(lambda driver: driver.current_url.endswith("/admin/"))
