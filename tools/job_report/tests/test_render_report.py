"""0015 phase C (C1): deterministic tests for the offline report generator.

These tests assert the rendered content from fixture data only:
  * one row per input item, in the API's order (no client-side re-sorting);
  * every row carries the original posting link;
  * score + score_version, skills, reasons, deal-breakers and deadline urgency
    are rendered, and a row without reasons gets an explicit marker;
  * the artifact is self-contained (no external asset, no script);
  * the generator is read-only: --input mode performs no network I/O at all,
    --url mode performs exactly one GET and never a mutating verb.
"""

import json
import os
import re
import sys
import tempfile
import unittest
import urllib.request

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

import render_report  # noqa: E402


def make_payload():
    """Two rows: a full explanation and a minimal one without reasons."""
    return {
        "code": 0,
        "message": "success",
        "data": {
            "list": [
                {
                    "id": 20002,
                    "company": "Beta",
                    "title": "C++ 后端",
                    "location": "深圳",
                    "salary_min": 20,
                    "salary_max": 35,
                    "freshness": "fresh",
                    "source_url": "https://example.invalid/jobs/20002",
                    "deadline_urgency": "urgent",
                    "score": 87.5,
                    "score_version": "rule_v1",
                    "matched_skills": ["C++", "Redis"],
                    "missing_skills": ["Kafka"],
                    "reasons": [
                        {
                            "code": "skills_matched",
                            "text": "匹配技能：C++、Redis",
                            "evidence": "skills",
                        },
                        {
                            "code": "location_mismatch",
                            "text": "岗位地点深圳，期望城市上海",
                            "evidence": "profile",
                        },
                    ],
                    "deal_breakers": [],
                },
                {
                    "id": 20001,
                    "company": "Acme",
                    "title": "C++ 后端 (older)",
                    "location": "深圳",
                    "freshness": "stale",
                    "source_url": "https://example.invalid/jobs/20001",
                    "deadline_urgency": "unknown",
                    "score": 42.0,
                    "score_version": "rule_v1",
                    "matched_skills": [],
                    "missing_skills": [],
                    "reasons": [],
                    "deal_breakers": [
                        {
                            "code": "salary_no_intersection",
                            "text": "岗位薪资与期望薪资无交集",
                        }
                    ],
                },
            ],
            "total": 2,
            "page": 1,
            "page_size": 20,
            "truncated": False,
        },
    }


class RenderReportTest(unittest.TestCase):
    def setUp(self):
        self.payload = make_payload()
        self.document = render_report.render_report(
            self.payload, "2026-01-01 00:00:00 UTC"
        )

    def test_rows_follow_the_api_order_with_positional_rank(self):
        first = self.document.index("https://example.invalid/jobs/20002")
        second = self.document.index("https://example.invalid/jobs/20001")

        # Row order is the input order; the rank column is positional.
        self.assertLess(first, second)
        self.assertIn("<td>1</td>", self.document)
        self.assertIn("<td>2</td>", self.document)

    def test_every_row_carries_the_original_link(self):
        for source_url in (
            "https://example.invalid/jobs/20002",
            "https://example.invalid/jobs/20001",
        ):
            self.assertIn('href="%s"' % source_url, self.document)

        # A row whose upstream payload has no link still renders explicitly.
        payload = make_payload()
        del payload["data"]["list"][1]["source_url"]
        rendered = render_report.render_report(payload, "t")
        self.assertIn(render_report.MISSING_MARKER, rendered)

    def test_score_version_skills_reasons_and_breakers(self):
        self.assertIn("87.50", self.document)
        self.assertIn("42.00", self.document)
        self.assertIn("rule_v1", self.document)
        self.assertIn("C++", self.document)
        self.assertIn("Kafka", self.document)
        self.assertIn("skills_matched", self.document)
        self.assertIn("匹配技能：C++、Redis", self.document)
        self.assertIn("[profile]", self.document)
        self.assertIn("salary_no_intersection", self.document)
        self.assertIn("岗位薪资与期望薪资无交集", self.document)

    def test_row_without_reasons_gets_an_explicit_marker(self):
        # The second fixture row has no reasons: never a blank cell.
        self.assertIn(render_report.NO_REASONS_MARKER, self.document)
        self.assertIn(render_report.NO_DEAL_BREAKERS_MARKER, self.document)

    def test_deadline_urgency_is_rendered(self):
        self.assertIn("urgent", self.document)
        self.assertIn("unknown", self.document)

    def test_artifact_is_self_contained(self):
        self.assertNotIn("<script", self.document)
        self.assertNotIn("src=", self.document)
        self.assertNotIn("<link", self.document)
        self.assertIn("<style>", self.document)

        # The only URLs in the artifact are the job links themselves: no
        # external stylesheet, script, font or image.
        urls = re.findall(r'(?:src|href)="([^"]+)"', self.document)

        self.assertEqual(len(urls), 2)
        for url in urls:
            self.assertTrue(
                url.startswith("https://example.invalid/jobs/"), url
            )

    def test_empty_list_renders_an_explicit_empty_state(self):
        payload = {"data": {"list": [], "total": 0}}
        rendered = render_report.render_report(payload, "t")
        self.assertIn("响应中没有岗位行", rendered)

    def test_render_is_deterministic(self):
        again = render_report.render_report(
            make_payload(), "2026-01-01 00:00:00 UTC"
        )
        self.assertEqual(self.document, again)

    def test_sort_basis_is_recorded_not_applied(self):
        # The plan requires the row's rank/sort basis to be visible; the tool
        # only RECORDS it and still preserves the input order.
        rendered = render_report.render_report(
            make_payload(),
            "2026-01-01 00:00:00 UTC",
            sort_basis="score DESC (rule_v1)",
        )
        self.assertIn("排序基准 score DESC (rule_v1)", rendered)
        self.assertLess(
            rendered.index("https://example.invalid/jobs/20002"),
            rendered.index("https://example.invalid/jobs/20001"),
        )
        self.assertIn("API 返回顺序（未重排）", self.document)


class ReadOnlyTest(unittest.TestCase):
    def test_input_mode_performs_no_network_io(self):
        def explode(*args, **kwargs):
            raise AssertionError("network access in --input mode")

        original = urllib.request.urlopen
        urllib.request.urlopen = explode
        try:
            with tempfile.TemporaryDirectory() as directory:
                source = os.path.join(directory, "captured.json")
                output = os.path.join(directory, "report.html")

                with open(source, "w", encoding="utf-8") as handle:
                    json.dump(make_payload(), handle)

                code = render_report.main(
                    [
                        "--input", source,
                        "--output", output,
                        "--generated-at", "2026-01-01 00:00:00 UTC",
                    ]
                )

                self.assertEqual(code, 0)
                self.assertEqual(
                    sorted(os.listdir(directory)),
                    ["captured.json", "report.html"],
                )

                with open(output, "r", encoding="utf-8") as handle:
                    written = handle.read()

                self.assertEqual(
                    written,
                    render_report.render_report(
                        make_payload(), "2026-01-01 00:00:00 UTC"
                    ),
                )
        finally:
            urllib.request.urlopen = original

    def test_url_mode_uses_one_get_with_the_bearer_token(self):
        captured = {}

        class FakeResponse:
            def __init__(self, body):
                self.body = body

            def read(self):
                return self.body

            def __enter__(self):
                return self

            def __exit__(self, *args):
                return False

        def fake_urlopen(request, timeout=None):
            captured["method"] = request.get_method()
            captured["url"] = request.full_url
            captured["authorization"] = request.get_header("Authorization")
            return FakeResponse(json.dumps(make_payload()).encode("utf-8"))

        original = urllib.request.urlopen
        urllib.request.urlopen = fake_urlopen
        try:
            with tempfile.TemporaryDirectory() as directory:
                output = os.path.join(directory, "report.html")
                code = render_report.main(
                    [
                        "--url",
                        "http://127.0.0.1:8080/api/v1/jobs?with_match=true",
                        "--token", "test-token",
                        "--output", output,
                        "--generated-at", "2026-01-01 00:00:00 UTC",
                    ]
                )
        finally:
            urllib.request.urlopen = original

        self.assertEqual(code, 0)
        self.assertEqual(captured["method"], "GET")
        self.assertEqual(captured["authorization"], "Bearer test-token")
        self.assertIn("with_match=true", captured["url"])


if __name__ == "__main__":
    unittest.main()
