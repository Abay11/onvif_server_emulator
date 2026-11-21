#!/usr/bin/env python3
"""Optional Chromium acceptance for detector settings, status colors and manual steps."""
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

from config_validation_smoke import request, wait


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=Path("build/main"))
    parser.add_argument("--screenshot", type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    with tempfile.TemporaryDirectory(prefix="detector-controls-") as directory:
        configs = Path(directory) / "configs"
        shutil.copytree(root / "server_configs", configs)
        target = configs / "event.config"
        env = dict(os.environ, ONVIF_ADVERTISED_IPV4="127.0.0.1", ONVIF_DISCOVERY_INTERFACE_IPV4="127.0.0.1")
        with (Path(directory) / "server.log").open("w+") as log:
            process = subprocess.Popen([str(args.binary.resolve()), str(configs)], env=env,
                                       stdout=log, stderr=subprocess.STDOUT)
            browser = None
            try:
                wait(process)
                options = webdriver.ChromeOptions()
                for option in ("--headless", "--no-sandbox", "--disable-dev-shm-usage", "--disable-background-networking", "--window-size=1440,1000"):
                    options.add_argument(option)
                options.set_capability("goog:loggingPrefs", {"browser": "ALL"})
                browser = webdriver.Chrome(service=Service("/usr/bin/chromedriver"), options=options)
                ready = WebDriverWait(browser, 20)
                browser.execute_cdp_cmd("Emulation.setEmulatedMedia", {"features": [{"name": "prefers-reduced-motion", "value": "reduce"}]})
                browser.get("http://127.0.0.1:8080/admin/#detectors")
                ready.until(lambda driver: len(driver.find_elements(By.CSS_SELECTOR, "#detector-catalog details")) == 6)
                assert not browser.find_elements(By.CSS_SELECTOR, "#detector-catalog input[type=checkbox]")

                def motion():
                    return browser.find_element(By.CSS_SELECTOR, '#detector-catalog button[aria-label^="Motion Alarm:"]')

                def setting(key):
                    return browser.find_element(By.CSS_SELECTOR, f'#detector-settings input[data-validation-path="MotionAlarm/{key}"]')

                def manual():
                    return browser.find_element(By.CSS_SELECTOR, "#detector-settings .manual-generate")

                def save():
                    browser.find_element(By.CSS_SELECTOR, "#detector-settings button[type=submit]").click()
                    ready.until(lambda driver: "Detector settings updated." in driver.find_element(By.ID, "detector-message").text)

                assert "detector-enabled" in motion().get_attribute("class")
                motion().click()
                assert setting("Enabled").is_selected()
                assert not setting("AutoGenerateEvents").is_selected()
                assert manual().is_enabled()
                cursor = request("/admin/api/journal?limit=200")["cursor"]
                manual().click()
                ready.until(lambda driver: "Detector ran once." in driver.find_element(By.ID, "detector-message").text)
                assert any(item["category"] == "detector" and item["source"] == "tns1:VideoSource/MotionAlarm"
                           for item in request(f"/admin/api/journal?after={cursor}&limit=200")["entries"])
                timeout = setting("EventGenerationTimeout")
                timeout.clear()
                timeout.send_keys("7")
                assert not manual().is_enabled()  # Prevent execution using unsaved settings.
                save()
                assert json.loads(target.read_text())["MotionAlarm"]["EventGenerationTimeout"] == "7"
                assert manual().is_enabled()
                setting("Enabled").click()
                save()
                assert "detector-disabled" in motion().get_attribute("class")
                assert not manual().is_enabled()
                request("/admin/api/detectors", {"action": "manual", "id": "MotionAlarm"}, expected=409)
                setting("Enabled").click()
                save()
                assert "detector-enabled" in motion().get_attribute("class")
                assert manual().is_enabled()

                # Local validation opens the same modal and retains the invalid draft.
                before = target.read_bytes()
                timeout = setting("EventGenerationTimeout")
                timeout.clear()
                timeout.send_keys("0")
                browser.find_element(By.CSS_SELECTOR, "#detector-settings button[type=submit]").click()
                ready.until(lambda driver: driver.find_element(By.ID, "config-error-dialog").get_property("open"))
                assert "EventGenerationTimeout" in browser.find_element(By.ID, "config-error-dialog").text
                assert target.read_bytes() == before
                browser.find_element(By.CSS_SELECTOR, "#config-error-dialog button").click()
                assert setting("EventGenerationTimeout").get_property("value") == "0"
                timeout.clear()
                timeout.send_keys("7")
                save()

                # Each expanded rule type exposes editable shared detector settings.
                family = browser.find_elements(By.CSS_SELECTOR, "#detector-catalog details")[0]
                family.find_element(By.TAG_NAME, "summary").click()
                next(item for item in family.find_elements(By.TAG_NAME, "button") if item.text == "Detector settings").click()
                assert browser.find_element(By.CSS_SELECTOR, '#detector-settings input[data-validation-path="tt:MotionRegionDetector/AutoGenerateEvents"]')
                assert manual().is_enabled()
                assert not browser.find_elements(By.CSS_SELECTOR, "#detector-catalog input[type=checkbox]")
                if args.screenshot:
                    browser.save_screenshot(str(args.screenshot))
                errors = [entry for entry in browser.get_log("browser") if entry["level"] == "SEVERE" and "/favicon.ico" not in entry["message"]]
                assert not errors, errors
                print("PASS Chromium: no list checkboxes, green/red states, editable settings, live save, auto-off manual step, disabled gates, validation modal and family controls")
            except BaseException:
                if browser and args.screenshot:
                    browser.save_screenshot(str(args.screenshot))
                log.flush()
                log.seek(0)
                print(log.read())
                raise
            finally:
                if browser:
                    browser.quit()
                process.terminate()
                try:
                    process.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()


if __name__ == "__main__":
    main()
