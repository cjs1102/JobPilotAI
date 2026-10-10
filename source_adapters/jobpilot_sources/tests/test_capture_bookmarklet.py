"""Bookmarklet tests: static credential/network guards plus real JS execution.

Two layers:

* **Static guards** run always. They fail if the bookmarklet ever gains a
  credential reference, a network call, a storage/cookie read, or a raw
  ``javascript:`` payload that is not encoded.
* **Execution** runs the packaged ``bookmarklet.js`` under ``node`` (skipped when
  node is absent) against fake DOMs, then feeds the produced artifacts back
  through the Python parser. That is the contract test between the browser half
  and the publisher half, and it covers "complete" and "missing field" pages.
"""

from __future__ import annotations

import json
import re
import shutil
import subprocess
import unittest
from pathlib import Path

from jobpilot_sources.capture import (
    CaptureError,
    instructions_html,
    parse_artifact,
)
from jobpilot_sources.capture.bookmarklet import (
    BOOKMARKLET_FILENAME,
    bookmarklet_body,
    bookmarklet_href,
    bookmarklet_source,
)
from jobpilot_sources.tests.support import FIXTURES, text_fixture

NODE = shutil.which("node")
HARNESS = FIXTURES / "bookmarklet_harness.js"

COMMENT_RE = re.compile(r"/\*[\s\S]*?\*/|//[^\n]*")

FORBIDDEN_CODE_PATTERNS = (
    "fetch(",
    "XMLHttpRequest",
    "sendBeacon",
    "WebSocket",
    "EventSource",
    "navigator.sendBeacon",
    "document.cookie",
    "localStorage",
    "sessionStorage",
    "indexedDB",
    ".submit(",
    "new Image",
    "import(",
    "eval(",
    "Function(",
    "postMessage",
    "XMLHttpRequest(",
)

FORBIDDEN_CREDENTIAL_TOKENS = (
    "token",
    "authorization",
    "bearer",
    "x-internal",
    "internal_api",
    "api_key",
    "apikey",
    "secret_key",
    "private_key",
    "password",
)


def strip_comments(source: str) -> str:
    return COMMENT_RE.sub(" ", source)


class BookmarkletStaticGuardTest(unittest.TestCase):
    def setUp(self) -> None:
        self.source = bookmarklet_source()
        self.code = strip_comments(self.source)

    def test_packaged_resource_exists(self) -> None:
        self.assertTrue(self.source.strip())
        self.assertIn("jobpilotCapture", self.source)
        self.assertIn("JobPilotAI", self.source)
        self.assertEqual(
            (FIXTURES.parent.parent / "capture" / BOOKMARKLET_FILENAME).name,
            "bookmarklet.js",
        )

    def test_carries_no_credential_reference(self) -> None:
        # Scanned on the raw source (comments included): the bookmarklet must
        # never mention a credential concept at all, so a future edit cannot
        # quietly add token plumbing next to prose about credentials.
        lowered = self.source.lower()

        for marker in FORBIDDEN_CREDENTIAL_TOKENS:
            with self.subTest(marker=marker):
                self.assertNotIn(marker, lowered)

    def test_makes_no_network_or_page_state_call(self) -> None:
        for pattern in FORBIDDEN_CODE_PATTERNS:
            with self.subTest(pattern=pattern):
                self.assertNotIn(pattern, self.code)
                self.assertNotIn(pattern.lower(), self.code.lower())

    def test_never_contains_an_internal_endpoint(self) -> None:
        for marker in ("/internal/", "jobs/import", "source-sync", "http://", "https://"):
            with self.subTest(marker=marker):
                self.assertNotIn(marker, self.code)

    def test_reads_only_dom_apis_needed_for_extraction(self) -> None:
        for api in (
            "document",
            "querySelectorAll",
            "getAttribute",
            "textContent",
            "innerText",
            "title",
            "location",
        ):
            with self.subTest(api=api):
                self.assertIn(api, self.source)

    def test_href_is_an_encoded_javascript_url(self) -> None:
        href = bookmarklet_href()

        self.assertTrue(href.startswith("javascript:"))
        payload = href[len("javascript:") :]
        self.assertNotIn("{", payload)
        self.assertNotIn("}", payload)
        self.assertNotIn("\n", payload)
        self.assertNotIn(" ", payload)
        self.assertIn("%20", payload)  # spaces are encoded

    def test_body_wraps_the_source_and_runs_it(self) -> None:
        body = bookmarklet_body({"maxDescription": 100})

        self.assertIn("jobpilotCapture.run(document", body)
        self.assertIn("maxDescription", body)
        self.assertTrue(body.startswith("(function(){"))
        self.assertTrue(body.endswith("})()"))

    def test_instructions_page_has_no_script_and_embeds_the_link(self) -> None:
        page = instructions_html()
        href = bookmarklet_href()

        self.assertIn("<!DOCTYPE html>", page)
        self.assertNotIn("<script", page.lower())
        self.assertIn('href="javascript:', page)
        # quote(safe="") leaves nothing that needs HTML escaping, so the
        # percent-encoded URL is embedded verbatim.
        self.assertIn(href, page)
        self.assertIn("no network request", page)

    def test_python_packaging_module_never_reads_the_environment(self) -> None:
        module = (
            Path(__file__).resolve().parents[1]
            / "capture"
            / "bookmarklet.py"
        ).read_text(encoding="utf-8")

        for marker in ("os.environ", "getenv", "environ"):
            with self.subTest(marker=marker):
                self.assertNotIn(marker, module)


@unittest.skipUnless(NODE, "node is required to execute the bookmarklet")
class BookmarkletExecutionTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        source_path = (
            Path(__file__).resolve().parents[1] / "capture" / BOOKMARKLET_FILENAME
        )
        result = subprocess.run(
            [NODE, str(HARNESS), str(source_path)],
            capture_output=True,
            text=True,
            timeout=30,
        )

        if result.returncode != 0:
            raise AssertionError(
                f"node harness failed: {result.returncode}\n{result.stderr}"
            )

        cls.output = json.loads(result.stdout)

    def test_rich_page_is_fully_extracted(self) -> None:
        artifact = self.output["rich"]
        job = artifact["job"]

        self.assertEqual(artifact["capture_version"], 1)
        self.assertEqual(job["title"], "C++ 后端开发工程师")
        self.assertEqual(job["company"], "示例科技有限公司")
        self.assertEqual(job["location"], "深圳市-南山区")
        self.assertEqual(job["salary"], "15-25K")
        self.assertEqual(job["education"], "本科")
        self.assertEqual(job["job_nature"], "实习")
        self.assertEqual(job["deadline"], "2026-10-31")
        self.assertEqual(job["published_at"], "2026-09-20T08:00:00+08:00")
        # The fragment is dropped so the URL can serve as the dedup key.
        self.assertEqual(
            job["source_url"],
            "https://jobs.example.invalid/campus/1001",
        )
        self.assertIn("负责后端服务开发", job["description"])
        self.assertEqual(artifact["page_host"], "jobs.example.invalid")

    def test_rich_page_artifact_is_accepted_by_the_python_parser(self) -> None:
        capture = parse_artifact(self.output["rich"])

        self.assertEqual(capture.company, "示例科技有限公司")
        self.assertEqual(capture.deadline, "2026-10-31")
        self.assertEqual(capture.job_nature, "实习")
        self.assertEqual(
            capture.source_url,
            "https://jobs.example.invalid/campus/1001",
        )

    def test_broken_json_ld_block_is_skipped_not_guessed(self) -> None:
        artifact = self.output["rich"]

        # Only the valid block contributed fields; the malformed one is ignored.
        self.assertEqual(artifact["job"]["title"], "C++ 后端开发工程师")
        self.assertEqual(artifact["warnings"], [])

    def test_minimal_page_uses_visible_text_and_warns(self) -> None:
        artifact = self.output["minimal"]
        job = artifact["job"]

        self.assertEqual(job["title"], "某岗位 - 登录可见")
        self.assertEqual(job["source_url"], "https://app.example-ats.invalid/social/2002")
        self.assertNotIn("company", job)
        self.assertNotIn("salary", job)
        self.assertNotIn("location", job)
        self.assertTrue(
            any("company name was not present" in warning for warning in artifact["warnings"])
        )
        self.assertTrue(
            any("excerpt of the visible page text" in warning for warning in artifact["warnings"])
        )

    def test_minimal_page_artifact_is_rejected_for_the_missing_company(self) -> None:
        # The publisher must refuse it and say what is missing rather than
        # inventing a company name.
        with self.assertRaises(CaptureError) as context:
            parse_artifact(self.output["minimal"])

        self.assertIn("job.company", str(context.exception))

    def test_description_is_truncated_with_a_warning(self) -> None:
        artifact = self.output["truncated"]

        self.assertEqual(len(artifact["job"]["description"]), 50)
        self.assertTrue(
            any("truncated to 50" in warning for warning in artifact["warnings"])
        )

    def test_static_plain_text_helper_strips_markup_and_scripts(self) -> None:
        self.assertEqual(self.output["plainText"], "A&B\nC")

    def test_version_matches_the_python_constant(self) -> None:
        from jobpilot_sources.capture import MAX_CAPTURE_VERSION

        self.assertEqual(self.output["version"], MAX_CAPTURE_VERSION)

    def test_executed_artifact_contains_no_credential(self) -> None:
        rendered = json.dumps(self.output)

        for marker in FORBIDDEN_CREDENTIAL_TOKENS:
            with self.subTest(marker=marker):
                self.assertNotIn(marker, rendered.lower())


class HarnessFixtureTest(unittest.TestCase):
    def test_harness_is_a_recorded_fixture_and_offline(self) -> None:
        harness = text_fixture("bookmarklet_harness.js")

        # The harness may contain fixture URLs, but it must not open a network
        # connection or spawn anything: the only require is the bookmarklet path.
        for pattern in (
            "fetch(",
            "XMLHttpRequest",
            'require("http',
            'require("https',
            'require("net',
            'require("dns',
            'require("child_process',
            "process.exit(0)",
        ):
            with self.subTest(pattern=pattern):
                self.assertNotIn(pattern, harness)

        self.assertIn("require(path)", harness)
        self.assertEqual(harness.count("require("), 1)


if __name__ == "__main__":
    unittest.main()
