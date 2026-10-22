#!/usr/bin/env python3
"""0022 optional real-browser smoke via geckodriver (Firefox).

This is intentionally optional and not a CI dependency: it proves that the
native modules execute in a real browser and that the visible controls drive
the same flows as the HTTP smoke. The HTTP smoke (`web-smoke.sh`) remains the
portable gate; this runner is used when Firefox/geckodriver are available.

    python3 tools/dev/web_browser_smoke.py \
        --web-url http://127.0.0.1:18081 \
        --email web-smoke-...@example.invalid --password ...

The caller must already have imported `docs/examples/job-import.json` and
created the account (the HTTP smoke does both). The script logs in through the
form, uploads a resume, searches a job, opens the detail, favourite it, and
advances the status board.
"""

import argparse
import base64
import json
import os
import subprocess
import sys
import time
import urllib.error
import urllib.request


class WebDriverError(RuntimeError):
    pass


class Driver:
    def __init__(self, port, geckodriver):
        self.base = "http://127.0.0.1:%d" % port
        self.process = subprocess.Popen(
            [geckodriver, "--port", str(port)],
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
        )
        self.session_id = None
        self._wait_ready()
        self._new_session()

    def _wait_ready(self):
        for _ in range(60):
            try:
                self._request("GET", "/status")
                return
            except (OSError, urllib.error.URLError):
                time.sleep(0.25)
        raise WebDriverError("geckodriver did not become ready")

    def _request(self, method, path, body=None):
        data = None if body is None else json.dumps(body).encode("utf-8")
        request = urllib.request.Request(
            self.base + path,
            data=data,
            method=method,
            headers={"Content-Type": "application/json"},
        )
        try:
            with urllib.request.urlopen(request, timeout=60) as response:
                payload = json.load(response)
        except urllib.error.HTTPError as error:
            detail = error.read().decode("utf-8", errors="replace")
            raise WebDriverError("%s %s -> HTTP %s: %s" % (method, path, error.code, detail))
        if isinstance(payload, dict) and payload.get("value") is None:
            return None
        return payload.get("value") if isinstance(payload, dict) else payload

    def _new_session(self):
        value = self._request("POST", "/session", {
            "capabilities": {
                "alwaysMatch": {
                    "browserName": "firefox",
                    "moz:firefoxOptions": {"args": ["-headless"]},
                }
            }
        })
        self.session_id = value["sessionId"]

    def close(self):
        if self.session_id:
            try:
                self._request("DELETE", "/session/%s" % self.session_id)
            except Exception:
                pass
            self.session_id = None
        if self.process.poll() is None:
            self.process.terminate()
            try:
                self.process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                self.process.kill()

    def _session(self, path):
        return "/session/%s%s" % (self.session_id, path)

    def navigate(self, url):
        self._request("POST", self._session("/url"), {"url": url})

    def current_url(self):
        return self._request("GET", self._session("/url"))

    def find(self, selector):
        return self._request(
            "POST", self._session("/element"),
            {"using": "css selector", "value": selector},
        )["element-6066-11e4-a52e-4f735466cecf"]

    def find_all(self, selector):
        values = self._request(
            "POST", self._session("/elements"),
            {"using": "css selector", "value": selector},
        )
        return [item["element-6066-11e4-a52e-4f735466cecf"] for item in values]

    def find_xpath(self, expression):
        return self._request(
            "POST", self._session("/element"),
            {"using": "xpath", "value": expression},
        )["element-6066-11e4-a52e-4f735466cecf"]

    def text(self, element_id):
        return self._request("GET", self._session("/element/%s/text" % element_id))

    def click(self, element_id):
        self._request("POST", self._session("/element/%s/click" % element_id), {})

    def send_keys(self, element_id, value):
        self._request(
            "POST", self._session("/element/%s/value" % element_id),
            {"text": value},
        )

    def body_text(self):
        return self._request("POST", self._session("/execute/sync"), {
            "script": "return document.body.innerText;",
            "args": [],
        })

    def clear_storage(self):
        self._request("POST", self._session("/execute/sync"), {
            "script": "window.localStorage.clear();",
            "args": [],
        })

    def screenshot(self, path):
        value = self._request("GET", self._session("/screenshot"))
        with open(path, "wb") as handle:
            handle.write(base64.b64decode(value))

    def scroll_into_view(self, element_id):
        self._request("POST", self._session("/execute/sync"), {
            "script": "arguments[0].scrollIntoView({block: 'center'});",
            "args": [{"element-6066-11e4-a52e-4f735466cecf": element_id}],
        })


def wait_for(predicate, description, timeout=20.0):
    deadline = time.time() + timeout
    last = None
    while time.time() < deadline:
        try:
            value = predicate()
            if value:
                return value
        except Exception as error:  # element not found / transient navigation
            last = error
        time.sleep(0.25)
    raise WebDriverError("timed out waiting for %s%s" % (
        description, ": %s" % last if last else ""))


def wait_text(driver, needle, timeout=20.0):
    return wait_for(
        lambda: needle in driver.body_text(),
        "text %r" % needle,
        timeout,
    )


def login(driver, web_url, email, password):
    driver.navigate(web_url + "/#/login")
    wait_text(driver, "进入 JobPilotAI")
    driver.send_keys(driver.find("#auth-email"), email)
    driver.send_keys(driver.find("#auth-password"), password)
    driver.click(driver.find_xpath("//form//button[@type='submit']"))
    wait_for(
        lambda: "岗位" in driver.body_text()
        and "申请状态板" in driver.body_text(),
        "authenticated navigation",
    )


def upload_resume(driver, web_url, resume_file):
    driver.navigate(web_url + "/#/applications")
    wait_text(driver, "简历设置")
    driver.send_keys(driver.find("#resume-title"), "browser smoke resume")
    driver.send_keys(driver.find("#resume-file"), resume_file)
    driver.click(driver.find_xpath("//button[normalize-space()='上传并设为当前版本']"))
    wait_text(driver, "当前简历版本 id", timeout=30)


def capture_missing_skills_card(driver, screenshot_dir=None):
    card = wait_for(
        lambda: driver.find_xpath(
            "//article[contains(@class,'job-card')][contains(., '缺少技能')]"
        ),
        "a job card with missing skills",
        timeout=25,
    )
    if screenshot_dir:
        os.makedirs(screenshot_dir, exist_ok=True)
        driver.scroll_into_view(card)
        time.sleep(0.5)
        driver.screenshot(os.path.join(screenshot_dir, "web-jobs.png"))
    card_text = driver.text(card)
    for label in ("缺少技能", "命中技能"):
        count = card_text.count(label)
        if count > 1:
            raise WebDriverError(
                "%s appears %d times in one job card; expected at most once"
                % (label, count)
            )
    return card


def search_and_open(driver, web_url, keyword, expected_title, screenshot_dir=None):
    driver.navigate(web_url + "/#/jobs")
    wait_text(driver, "岗位列表")
    capture_missing_skills_card(driver, screenshot_dir)
    keyword_input = driver.find("#keyword")
    driver.send_keys(keyword_input, keyword)
    driver.click(driver.find_xpath("//form[contains(@class,'filters')]//button[@type='submit']"))
    wait_for(
        lambda: expected_title in driver.body_text(),
        "job row %r" % expected_title,
        timeout=25,
    )
    driver.click(driver.find_xpath(
        "//article[contains(@class,'job-card')][contains(., '%s')]"
        "//a[contains(@class,'job-title')]" % expected_title
    ))
    wait_text(driver, "岗位描述")
    wait_text(driver, "打开原始公告")
    body = driver.body_text()
    if "未提供链接" not in body:
        raise WebDriverError("expected the audit sample to have no apply_url")
    return None


def favourite(driver, web_url, expected_title):
    driver.navigate(web_url + "/#/jobs")
    wait_text(driver, "岗位列表")
    wait_text(driver, expected_title, timeout=25)
    driver.click(driver.find_xpath(
        "//article[contains(@class,'job-card')][contains(., '%s')]//button[normalize-space()='收藏']"
        % expected_title
    ))
    wait_text(driver, "已收藏", timeout=20)


def advance_status(driver, web_url, expected_title):
    driver.navigate(web_url + "/#/applications")
    wait_text(driver, "我的投递")
    wait_text(driver, expected_title, timeout=25)
    driver.click(driver.find_xpath(
        "//div[contains(@class,'application-row')][contains(., '%s')]"
        "//button[contains(., '推进到')]" % expected_title
    ))
    wait_for(
        lambda: "推进到 已投递" in driver.body_text(),
        "status advanced to 待投递",
        timeout=20,
    )


def main(argv=None):
    parser = argparse.ArgumentParser(description="Real-browser Web UI smoke")
    parser.add_argument("--web-url", required=True)
    parser.add_argument("--email", required=True)
    parser.add_argument("--password", required=True)
    parser.add_argument("--keyword", default="Distributed")
    parser.add_argument("--expected-title", default="Distributed Systems Engineer")
    parser.add_argument("--resume-file", default=None)
    parser.add_argument("--geckodriver", default="geckodriver")
    parser.add_argument("--port", type=int, default=4444)
    parser.add_argument("--screenshot-dir", default=None)
    args = parser.parse_args(argv)

    resume_file = args.resume_file
    if not resume_file:
        # Firefox is often a snap with a private /tmp; use the user home so
        # the file input can read the path.
        resume_file = os.path.join(os.path.expanduser("~"), "jobpilot-browser-resume.txt")
        with open(resume_file, "w", encoding="utf-8") as handle:
            handle.write("Browser smoke resume: C++ backend engineer, Linux, MySQL.\n")

    driver = Driver(args.port, args.geckodriver)
    try:
        login(driver, args.web_url, args.email, args.password)
        upload_resume(driver, args.web_url, resume_file)
        search_and_open(driver, args.web_url, args.keyword, args.expected_title, args.screenshot_dir)
        if args.screenshot_dir:
            os.makedirs(args.screenshot_dir, exist_ok=True)
            driver.screenshot(os.path.join(args.screenshot_dir, "web-detail.png"))
        favourite(driver, args.web_url, args.expected_title)
        advance_status(driver, args.web_url, args.expected_title)
        if args.screenshot_dir:
            driver.screenshot(os.path.join(args.screenshot_dir, "web-status.png"))
        print("BROWSER_SMOKE_OK")
        return 0
    except Exception as error:
        print("BROWSER_SMOKE_FAILED: %s" % error, file=sys.stderr)
        try:
            print(driver.body_text()[:2000], file=sys.stderr)
            if args.screenshot_dir:
                os.makedirs(args.screenshot_dir, exist_ok=True)
                driver.screenshot(os.path.join(args.screenshot_dir, "web-failure.png"))
        except Exception:
            pass
        return 1
    finally:
        driver.close()


if __name__ == "__main__":
    sys.exit(main())
