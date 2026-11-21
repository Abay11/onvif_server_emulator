#!/usr/bin/env python3
"""Optional Chromium acceptance for profiles, entity settings, restart hints and failed saves."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

from selenium import webdriver
from selenium.webdriver.chrome.service import Service
from selenium.webdriver.common.by import By
from selenium.webdriver.support.ui import WebDriverWait

from config_validation_smoke import wait
from configuration_ui_smoke import entry


def exercise(screenshot=None, read_only=False):
    options = webdriver.ChromeOptions()
    for argument in ("--headless", "--no-sandbox", "--disable-dev-shm-usage", "--disable-background-networking", "--window-size=1440,1100"):
        options.add_argument(argument)
    options.set_capability("goog:loggingPrefs", {"browser": "ALL"})
    browser = webdriver.Chrome(service=Service("/usr/bin/chromedriver"), options=options)
    try:
        ready = WebDriverWait(browser, 25)
        browser.execute_cdp_cmd("Emulation.setEmulatedMedia", {"features": [{"name": "prefers-reduced-motion", "value": "reduce"}]})
        browser.get("http://127.0.0.1:8080/admin/#configuration")
        ready.until(lambda driver: len(driver.find_elements(By.CSS_SELECTOR, "#configuration-catalog button")) > 15)

        def catalog_button(text):
            return next(item for item in browser.find_elements(By.CSS_SELECTOR, "#configuration-catalog button") if item.text == text)

        def editor_button(text):
            return next(item for item in browser.find_elements(By.CSS_SELECTOR, "#configuration-editor button") if item.text == text)

        def save():
            editor_button("Save configuration").click()
            ready.until(lambda driver: "Configuration saved" in driver.find_element(By.ID, "configuration-message").text)

        def field(key):
            return browser.find_element(By.CSS_SELECTOR, f'#configuration-editor [data-configuration-key="{key}"]')

        def open_group(text):
            group = next(item for item in browser.find_elements(By.CSS_SELECTOR, "#configuration-catalog details")
                         if item.find_element(By.TAG_NAME, "summary").text == text)
            if not group.get_property("open"):
                group.find_element(By.TAG_NAME, "summary").click()
            return group

        catalog_button("MainProfile · fixed").click()
        assert not editor_button("Delete profile").is_enabled()
        if read_only:
            before = entry("media_profiles.config")["document"]
            name = field("Name"); name.clear(); name.send_keys("Cannot persist this")
            editor_button("Save configuration").click()
            ready.until(lambda driver: driver.find_element(By.ID, "config-error-dialog").get_property("open"))
            assert "read-only" in browser.find_element(By.ID, "config-error-dialog").text
            assert field("Name").get_property("value") == "Cannot persist this"
            assert entry("media_profiles.config")["document"] == before
            print("PASS Chromium read-only mount: failure dialog and retained draft, no file write")
            return

        select = browser.find_element(By.CSS_SELECTOR, '#configuration-editor select[data-configuration-type="Metadata"]')
        next(option for option in select.find_elements(By.TAG_NAME, "option") if option.get_attribute("value") == "MetadataToken0").click()
        save()
        assert json.loads(entry("media_profiles.config")["document"])["MediaProfiles"][0]["Metadata"] == "MetadataToken0"
        assert not browser.find_element(By.ID, "configuration-restart").is_displayed()
        catalog_button("Create media profile").click()
        name = field("Name"); name.clear(); name.send_keys("Browser profile")
        save()
        assert editor_button("Delete profile").is_enabled()
        editor_button("Delete profile").click()
        browser.switch_to.alert.accept()
        ready.until(lambda _: not any(item["Name"] == "Browser profile" for item in json.loads(entry("media_profiles.config")["document"])["MediaProfiles"]))
        ready.until(lambda driver: "Saved. Select an item" in driver.find_element(By.ID, "configuration-editor").text)

        open_group("Metadata")
        catalog_button("Event metadata").click()
        field("Events").click()
        save()
        assert json.loads(entry("media_profiles.config")["document"])["Metadata"][0]["Events"] in (False, "false")
        name = field("Name"); name.clear()
        before = entry("media_profiles.config")["document"]
        editor_button("Save configuration").click()
        ready.until(lambda driver: driver.find_element(By.ID, "config-error-dialog").get_property("open"))
        assert "media_profiles.config" in browser.find_element(By.ID, "config-error-dialog").text
        assert entry("media_profiles.config")["document"] == before
        assert field("Name").get_property("value") == ""
        browser.find_element(By.CSS_SELECTOR, "#config-error-dialog button").click()
        name.send_keys("Event metadata")
        save()

        open_group("Configuration files · advanced")
        catalog_button("common.config").click()
        text = browser.find_element(By.CSS_SELECTOR, "#configuration-editor textarea.configuration-json")
        common = json.loads(text.get_property("value"))
        assert all(item["password"] == "__UNCHANGED_SECRET__" for item in common["users"])
        common["loggingLevel"] = "INFO"
        text.clear(); text.send_keys(json.dumps(common))
        save()
        assert browser.find_element(By.ID, "configuration-restart").is_displayed()
        ready.until(lambda driver: driver.find_element(By.ID, "configuration-saved-dialog").get_property("open"))
        assert "only after restarting" in browser.find_element(By.ID, "configuration-saved-dialog").text
        assert "only after restarting" in browser.find_element(By.ID, "configuration-message").text
        assert json.loads(entry("common.config")["document"])["loggingLevel"] == "INFO"
        assert not [item for item in browser.get_log("browser") if item["level"] == "SEVERE" and "/favicon.ico" not in item["message"]
                    and "422 (Unprocessable Entity)" not in item["message"]]
        print("PASS Chromium: fixed profile protection, profile creation/deletion, configuration attachment, "
              "metadata editing, schema error dialog, retained draft, write-only passwords and restart notification")
    except BaseException:
        if screenshot:
            browser.save_screenshot(str(screenshot))
        raise
    finally:
        if screenshot:
            browser.save_screenshot(str(screenshot))
        browser.quit()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=Path("build/main"))
    parser.add_argument("--screenshot", type=Path)
    parser.add_argument("--read-only", action="store_true")
    args = parser.parse_args()
    if args.read_only:
        exercise(args.screenshot, True)
        return
    root = Path(__file__).resolve().parents[1]
    with tempfile.TemporaryDirectory(prefix="configuration-browser-") as directory:
        configs = Path(directory) / "configs"
        shutil.copytree(root / "server_configs", configs)
        environment = dict(os.environ, ONVIF_ADVERTISED_IPV4="127.0.0.1", ONVIF_DISCOVERY_INTERFACE_IPV4="127.0.0.1")
        with (Path(directory) / "server.log").open("w+") as log:
            process = subprocess.Popen([str(args.binary.resolve()), str(configs)], env=environment,
                                       stdout=log, stderr=subprocess.STDOUT)
            try:
                wait(process)
                exercise(args.screenshot)
            except BaseException:
                log.flush(); log.seek(0); print(log.read())
                raise
            finally:
                process.terminate()
                try: process.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    process.kill(); process.wait()


if __name__ == "__main__":
    main()
