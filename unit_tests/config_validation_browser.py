#!/usr/bin/env python3
"""Optional Chromium/Selenium check for save validation modals and retained drafts."""
from admin_session import browser_login
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
    with tempfile.TemporaryDirectory(prefix="config-modal-") as directory:
        configs = Path(directory) / "configs"
        shutil.copytree(root / "server_configs", configs)
        target = configs / "media_profiles.config"
        config = json.loads(target.read_text())
        config["TypoSetting"] = "never-echo-this-secret"
        target.write_text(json.dumps(config))
        before = target.read_bytes()
        env = dict(os.environ, ONVIF_ADVERTISED_IPV4="127.0.0.1", ONVIF_DISCOVERY_INTERFACE_IPV4="127.0.0.1")
        with (Path(directory) / "server.log").open("w+") as log:
            process = subprocess.Popen([str(args.binary.resolve()), str(configs)], env=env,
                                       stdout=log, stderr=subprocess.STDOUT)
            browser = None
            try:
                state = wait(process)
                options = webdriver.ChromeOptions()
                for option in ("--headless", "--no-sandbox", "--disable-dev-shm-usage", "--disable-background-networking", "--window-size=1440,1000"):
                    options.add_argument(option)
                browser = webdriver.Chrome(service=Service("/usr/bin/chromedriver"), options=options)
                ready = WebDriverWait(browser, 20)
                browser.execute_cdp_cmd("Emulation.setEmulatedMedia", {"features": [{"name": "prefers-reduced-motion", "value": "reduce"}]})
                browser_login(browser)
                browser.get("http://127.0.0.1:8080/admin/#detectors")
                ready.until(lambda driver: len(driver.find_elements(By.CSS_SELECTOR, "#detector-catalog details")) == 6)
                family = browser.find_elements(By.CSS_SELECTOR, "#detector-catalog details")[0]
                family.find_element(By.TAG_NAME, "summary").click()
                next(button for button in family.find_elements(By.TAG_NAME, "button") if button.text == "Create rule").click()
                name = ready.until(lambda driver: driver.find_element(By.CSS_SELECTOR, "#detector-settings input[data-validation-path='Name']"))
                name.clear()
                name.send_keys("Popup browser test")
                browser.find_element(By.CSS_SELECTOR, "#detector-settings button[type='submit']").click()
                ready.until(lambda driver: driver.find_element(By.ID, "config-error-dialog").get_property("open"))
                popup = browser.find_element(By.ID, "config-error-dialog")
                assert "media_profiles.config" in popup.text and "TypoSetting" in popup.text
                assert "never-echo-this-secret" not in popup.text
                assert target.read_bytes() == before
                if args.screenshot:
                    browser.save_screenshot(str(args.screenshot))
                popup.find_element(By.TAG_NAME, "button").click()
                ready.until(lambda driver: not driver.find_element(By.ID, "config-error-dialog").get_property("open"))
                assert name.get_property("value") == "Popup browser test"
                name.clear()
                browser.find_element(By.CSS_SELECTOR, "#detector-settings button[type='submit']").click()
                ready.until(lambda driver: driver.find_element(By.ID, "config-error-dialog").get_property("open"))
                assert "/rule/Name" in browser.find_element(By.ID, "config-error-dialog").text
                browser.switch_to.active_element.send_keys("\ue00c")  # Escape dismisses the native dialog.
                ready.until(lambda driver: not driver.find_element(By.ID, "config-error-dialog").get_property("open"))
                name.send_keys("Popup browser test")
                del config["TypoSetting"]
                target.write_text(json.dumps(config))
                request("/admin/api/reboot", {"confirm": True}, expected=202, marker="reboot")
                wait(process, state["instance_id"])
                # Restart invalidates the browser session; sign in and re-enter the test draft.
                browser_login(browser)
                browser.get("http://127.0.0.1:8080/admin/#detectors")
                ready.until(lambda driver: len(driver.find_elements(By.CSS_SELECTOR, "#detector-catalog details")) == 6)
                family = browser.find_elements(By.CSS_SELECTOR, "#detector-catalog details")[0]
                family.find_element(By.TAG_NAME, "summary").click()
                next(button for button in family.find_elements(By.TAG_NAME, "button") if button.text == "Create rule").click()
                name = ready.until(lambda driver: driver.find_element(By.CSS_SELECTOR, "#detector-settings input[data-validation-path='Name']"))
                name.clear(); name.send_keys("Popup browser test")
                browser.find_element(By.CSS_SELECTOR, "#detector-settings button[type='submit']").click()
                ready.until(lambda _: any(rule["Name"] == "Popup browser test"
                                          for rule in json.loads(target.read_text())["Analytics"][0]["rules"]))
                ready.until(lambda driver: "Detector settings updated." in driver.find_element(By.ID, "detector-message").text)
                assert not browser.find_element(By.ID, "config-error-dialog").get_property("open")
                print("PASS Chromium: save opens schema error modal, no value leakage, draft retained, local validation modal, "
                      "Escape/close, and successful save after correction")
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
