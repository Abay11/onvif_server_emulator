#!/usr/bin/env python3
"""Optional Chromium acceptance for profiles, entity settings, restart hints and failed saves."""
from admin_session import browser_login
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

from selenium import webdriver
from selenium.common.exceptions import StaleElementReferenceException
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
        ready = WebDriverWait(browser, 25, ignored_exceptions=(StaleElementReferenceException,))
        browser.execute_cdp_cmd("Emulation.setEmulatedMedia", {"features": [{"name": "prefers-reduced-motion", "value": "reduce"}]})
        browser_login(browser)
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

        catalog_button("CH01 - Main stream · fixed").click()
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
        catalog_button("CH01 - Event metadata").click()
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
        name.send_keys("CH01 - Event metadata")
        save()

        browser.get("http://127.0.0.1:8080/admin/#users")
        ready.until(lambda driver: len(driver.find_elements(By.CSS_SELECTOR, "#users-body tr")) == 3)
        assert "digest/ws-security" in browser.find_element(By.ID, "authentication-active").text
        def account_row(login):
            return next(row for row in browser.find_elements(By.CSS_SELECTOR, "#users-body tr")
                        if row.find_element(By.TAG_NAME, "td").text == login)
        assert all(not item.is_enabled() for item in account_row("a").find_elements(By.TAG_NAME, "button"))
        browser.find_element(By.ID, "user-login").send_keys("browser-user")
        browser.find_element(By.ID, "user-password").send_keys("browser-secret")
        browser.find_element(By.CSS_SELECTOR, "#user-create button").click()
        ready.until(lambda driver: len(driver.find_elements(By.CSS_SELECTOR, "#users-body tr")) == 4)
        assert browser.find_element(By.ID, "user-password").get_property("value") == ""
        assert all(user["password"] == "__UNCHANGED_SECRET__" for user in json.loads(entry("users.config")["document"])["users"])
        account_row("browser-user").find_elements(By.TAG_NAME, "button")[0].click()
        ready.until(lambda _: "Disabled" in account_row("browser-user").text)
        account_row("browser-user").find_elements(By.TAG_NAME, "button")[0].click()
        ready.until(lambda _: "Enabled" in account_row("browser-user").text)
        account_row("browser-user").find_elements(By.TAG_NAME, "button")[1].click()
        browser.switch_to.alert.accept()
        ready.until(lambda driver: len(driver.find_elements(By.CSS_SELECTOR, "#users-body tr")) == 3)
        scheme = browser.find_element(By.ID, "authentication-scheme")
        next(option for option in scheme.find_elements(By.TAG_NAME, "option") if option.get_attribute("value") == "digest").click()
        browser.find_element(By.CSS_SELECTOR, "#authentication-form button").click()
        ready.until(lambda _: json.loads(entry("common.config")["document"])["authentication"] == "digest")
        ready.until(lambda driver: "Restart the application" in driver.find_element(By.ID, "users-message").text)
        assert "digest/ws-security" in browser.find_element(By.ID, "authentication-active").text
        browser.get("http://127.0.0.1:8080/admin/#configuration")
        ready.until(lambda driver: len(driver.find_elements(By.CSS_SELECTOR, "#configuration-catalog button")) > 15)
        open_group("Configuration files · advanced")
        catalog_button("common.config").click()
        text = browser.find_element(By.CSS_SELECTOR, "#configuration-editor textarea.configuration-json")
        common = json.loads(text.get_property("value"))
        users = json.loads(entry("users.config")["document"])
        assert all(item["password"] == "__UNCHANGED_SECRET__" for item in users["users"])
        common["loggingLevel"] = "INFO"
        text.clear(); text.send_keys(json.dumps(common))
        save()
        assert browser.find_element(By.ID, "configuration-restart").is_displayed()
        ready.until(lambda driver: driver.find_element(By.ID, "configuration-saved-dialog").get_property("open"))
        assert "only after restarting" in browser.find_element(By.ID, "configuration-saved-dialog").text
        assert "only after restarting" in browser.find_element(By.ID, "configuration-message").text
        assert json.loads(entry("common.config")["document"])["loggingLevel"] == "INFO"
        errors = [item for item in browser.get_log("browser") if item["level"] == "SEVERE"
                  and "/favicon.ico" not in item["message"] and "422 (Unprocessable Entity)" not in item["message"]]
        assert not errors, errors
        print("PASS Chromium: fixed profile protection, profile creation/deletion, configuration attachment, "
              "metadata editing, schema error dialog, retained draft, account create/enable/disable/delete, "
              "authentication scheme selection, write-only passwords and restart notification")

        browser.find_element(By.CSS_SELECTOR, "#configuration-saved-dialog button").click()

        def sidebar(section):
            return browser.find_element(By.CSS_SELECTOR, f'.nav-link[href="#{section}"]')

        assert sidebar("recordings").text.endswith("Recording Storage")
        assert sidebar("recordings").find_element(By.CSS_SELECTOR, '[aria-hidden="true"]').text
        sidebar("recordings").click()
        ready.until(lambda driver: "Channel 1" in driver.find_element(By.ID, "recordings-jobs").text)
        assert sidebar("channels").text.endswith("NVR Channels")
        sidebar("channels").click()
        channel_editor = browser.find_element(By.ID, "channels-editor")
        count = channel_editor.find_element(By.CSS_SELECTOR, 'input[type="number"]')
        count.clear(); count.send_keys("2")
        next(button for button in channel_editor.find_elements(By.TAG_NAME, "button") if button.text == "Apply channel count").click()
        added = channel_editor.find_elements(By.CSS_SELECTOR, ".channel-editor-list > details")[1]
        added.find_element(By.TAG_NAME, "summary").click()
        assert "CH02 - Main stream" in added.text
        next(button for button in added.find_elements(By.TAG_NAME, "button") if button.text == "Add stream").click()
        channel_editor.find_element(By.CSS_SELECTOR, 'button[type="submit"]').click()
        ready.until(lambda driver: "Configuration saved" in driver.find_element(By.ID, "channels-message").text)
        channels = json.loads(entry("channels.config")["document"])["channels"]
        first, second = channels
        assert first["media"]["VideoSource"][0]["token"] == "VideoSrcConfigToken0"
        assert second["media"]["VideoSource"][0]["token"].startswith("VideoSourceConfig_CH02_")
        assert second["media"]["VideoSource"][0]["SourceToken"] == second["videoSource"]["token"]
        for catalog in ("VideoEncoder", "VideoEncoderConfigurations"):
            assert [item["Name"] for item in second["media"][catalog]] == [
                "CH02 - Video encoder 1", "CH02 - Video encoder 2", "CH02 - Video encoder 3"]
        assert len(browser.find_elements(By.CSS_SELECTOR, "#configuration-catalog > .channel-catalog")) == 1

        browser.find_element(By.CSS_SELECTOR, "#configuration-saved-dialog button").click()
        sidebar("system").click()
        ready.until(lambda driver: driver.find_element(By.ID, "restart").is_enabled())
        browser.find_element(By.ID, "restart").click()
        browser.find_element(By.ID, "confirm-restart").click()
        ready.until(lambda driver: driver.current_url.endswith("/admin/login"))
        browser_login(browser)
        sidebar("configuration").click()
        channel_groups = "#configuration-catalog > .channel-catalog"
        ready.until(lambda driver: len(driver.find_elements(By.CSS_SELECTOR, channel_groups)) == 2)
        first_group, second_group = browser.find_elements(By.CSS_SELECTOR, channel_groups)
        assert "CH01 - Main stream" in first_group.text
        second_group.find_element(By.TAG_NAME, "summary").click()
        assert "CH02 - Main stream" in second_group.text
        assert "CH01 - Main stream" not in second_group.text
        ready.until(lambda driver: [row.text for row in driver.find_elements(By.CSS_SELECTOR, "#profiles-body .profile-channel")]
                    == ["Channel 1", "Channel 2"])
        catalog_button("CH02 - Main stream · fixed").click()
        sources = browser.find_element(By.CSS_SELECTOR, '[data-configuration-type="VideoSource"]')
        assert not sources.find_element(By.CSS_SELECTOR, 'option[value="VideoSrcConfigToken0"]').is_enabled()
        encoder_group = next(group for group in second_group.find_elements(By.TAG_NAME, "details")
                             if group.find_element(By.TAG_NAME, "summary").text == "Video encoder (Media2)")
        encoder_group.find_element(By.TAG_NAME, "summary").click()
        catalog_button("CH02 - Video encoder 1").click()
        name = field("Name"); name.clear(); name.send_keys("CH02 - Browser encoder")
        save()
        saved_media = json.loads(entry("channels.config")["document"])["channels"][1]["media"]
        for catalog in ("VideoEncoder", "VideoEncoderConfigurations"):
            assert saved_media[catalog][0]["Name"] == "CH02 - Browser encoder"
            assert saved_media[catalog][0]["token"] == second["media"][catalog][0]["token"]
        print("PASS Chromium: separate NVR and storage sections, readable new identities, active channel grouping "
              "after restart, scoped profile choices, and paired encoder rename")
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
