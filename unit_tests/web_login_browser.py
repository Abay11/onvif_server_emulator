#!/usr/bin/env python3
"""Chromium acceptance for administrator sign-in, protected APIs, logout and restart."""
import argparse
import http.client
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import time

from selenium import webdriver
from selenium.webdriver.chrome.service import Service
from selenium.webdriver.support.ui import WebDriverWait
from config_validation_smoke import wait


def run(binary, screenshots):
    root = Path(__file__).resolve().parents[1]
    screenshots.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="onvif-login-") as directory:
        configs = Path(directory) / "configs"
        shutil.copytree(root / "server_configs", configs)
        common = json.loads((configs / "common.config").read_text())
        common["authentication"] = "none"
        common["rtsp_metadata"]["enabled"] = False
        (configs / "common.config").write_text(json.dumps(common))
        users = json.loads((configs / "users.config").read_text())
        users["users"].extend([
            {"login": "second-admin", "password": "second-secret", "type": "administrator", "enabled": True},
            {"login": "disabled-admin", "password": "secret", "type": "administrator", "enabled": False}])
        (configs / "users.config").write_text(json.dumps(users))
        with (Path(directory) / "server.log").open("w+") as log:
            def start():
                process = subprocess.Popen([str(binary.resolve()), str(configs)], stdout=log, stderr=subprocess.STDOUT)
                wait(process)
                return process
            process = start()
            options = webdriver.ChromeOptions()
            for argument in ("--headless", "--no-sandbox", "--disable-dev-shm-usage", "--disable-background-networking", "--window-size=1440,1000"):
                options.add_argument(argument)
            browser = webdriver.Chrome(service=Service("/usr/bin/chromedriver"), options=options)
            ready = WebDriverWait(browser, 20)
            try:
                browser.get("http://127.0.0.1:8080/admin/")
                assert browser.current_url.endswith("/admin/login")
                browser.save_screenshot(str(screenshots / "web-login-desktop.png"))
                browser.set_window_size(390, 844)
                assert browser.execute_script("return document.documentElement.scrollWidth <= innerWidth")
                browser.save_screenshot(str(screenshots / "web-login-mobile.png"))
                browser.set_window_size(1440, 1000)

                def login(username, password):
                    field = browser.find_element("id", "login-username")
                    field.clear(); field.send_keys(username)
                    browser.find_element("id", "login-password").send_keys(password)
                    browser.find_element("id", "login-submit").click()

                for username, password in (("a", "wrong"), ("o", "o1"), ("u", "u1"), ("disabled-admin", "secret")):
                    login(username, password)
                    ready.until(lambda driver: driver.find_element("id", "login-error").is_displayed() and
                                driver.find_element("id", "login-submit").is_enabled() and
                                driver.find_element("id", "login-password").get_property("value") == "")
                    assert browser.current_url.endswith("/admin/login")
                for username, password in (("a", "a1"), ("second-admin", "second-secret")):
                    login(username, password)
                    ready.until(lambda driver: driver.current_url.endswith("/admin/"))
                    ready.until(lambda driver: driver.find_element("id", "connection-label").text == "Online")
                    cookie = browser.get_cookie("onvif_admin_session")
                    assert cookie["httpOnly"] and cookie["sameSite"] == "Strict" and cookie["path"] == "/admin"
                    assert "onvif_admin_session" not in browser.execute_script("return document.cookie")
                    assert password not in browser.execute_script("return JSON.stringify(localStorage)")
                    if username == "a":
                        browser.set_window_size(390, 844)
                        assert browser.find_element("id", "sign-out").is_displayed()
                        assert browser.execute_script("return document.getElementById('sign-out').getBoundingClientRect().right <= innerWidth")
                        browser.find_element("id", "sign-out").click()
                        ready.until(lambda driver: driver.current_url.endswith("/admin/login"))
                        connection = http.client.HTTPConnection("127.0.0.1", 8080)
                        connection.request("GET", "/admin/api/configuration", headers={"Cookie": "onvif_admin_session=" + cookie["value"]})
                        response = connection.getresponse()
                        assert response.status == 401
                        response.read(); connection.close()
                        browser.set_window_size(1440, 1000)
                browser.save_screenshot(str(screenshots / "web-admin-signed-in.png"))
                # The existing page must return to sign-in when the session is lost after restart.
                process.terminate(); process.wait(timeout=5)
                process = start()
                ready.until(lambda driver: driver.current_url.endswith("/admin/login"))
                print("PASS Chromium: responsive sign-in, admin accounts, role/disabled rejection, protected API, HttpOnly session, logout and restart expiry")
            except BaseException:
                log.flush(); log.seek(0); print(log.read())
                raise
            finally:
                browser.quit()
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill(); process.wait()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=Path("build/main"))
    parser.add_argument("--screenshots", type=Path, default=Path("/tmp/web-login-preview"))
    args = parser.parse_args()
    run(args.binary, args.screenshots)
