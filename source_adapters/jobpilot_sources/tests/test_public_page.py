"""Fixture contract tests for the generic public-page provider.

These tests are the Step 3 "per source fixture contract" deliverable: a recorded
public response goes in, a payload that satisfies the C++ source-sync contract
comes out. No test touches the network.
"""

from __future__ import annotations

import json
import unittest

from jobpilot_sources.normalizer import build_job
from jobpilot_sources.providers import public_page
from jobpilot_sources.providers.base import ProviderError
from jobpilot_sources.tests.support import text_fixture
from jobpilot_sources.timestamps import is_date_only_stored

ALLOWED_JOB_KEYS = {
    "company",
    "title",
    "source",
    "source_url",
    "requirements",
    "external_id",
    "published_at",
    "location",
    "description",
    "salary_currency",
    "salary_min",
    "salary_max",
    "education_required",
    "experience_years_min",
    "experience_years_max",
    "job_nature",
    "deadline",
    "deadline_date_only",
}


def assert_contract(test: unittest.TestCase, jobs) -> None:
    for job in jobs:
        test.assertTrue(set(job).issubset(ALLOWED_JOB_KEYS), set(job))
        test.assertEqual(job["source"], public_page.PROVIDER)
        test.assertTrue(job["title"])
        test.assertTrue(job["company"])
        test.assertLessEqual(len(job["company"].encode("utf-8")), 128)
        test.assertLessEqual(len(job["title"].encode("utf-8")), 128)
        test.assertLessEqual(len(job["source_url"].encode("utf-8")), 255)

        if "location" in job:
            test.assertLessEqual(len(job["location"].encode("utf-8")), 64)

        if "description" in job:
            test.assertLessEqual(
                len(job["description"].encode("utf-8")), 65535
            )

        if "education_required" in job:
            test.assertIn(job["education_required"], (1, 2, 3, 4, 5))

        if "job_nature" in job:
            test.assertIn(job["job_nature"], (1, 2, 3))

        if "experience_years_min" in job:
            test.assertGreaterEqual(job["experience_years_min"], 0)
            test.assertLessEqual(job["experience_years_min"], 32767)

        if "experience_years_max" in job:
            test.assertGreaterEqual(job["experience_years_max"], 0)
            test.assertLessEqual(job["experience_years_max"], 32767)

        if "deadline" in job:
            test.assertIn("deadline_date_only", job)
            test.assertEqual(job["deadline"][10], " ")

            if job["deadline_date_only"]:
                test.assertEqual(job["deadline"][11:], "04:00:00")

        test.assertEqual(job["requirements"], [])


class JsonBoardTest(unittest.TestCase):
    def setUp(self) -> None:
        self.result = public_page.parse_board(
            text_fixture("cn_json_board.json"),
            board="https://jobs.example-cn.invalid/campus",
            company=None,
        )

    def test_total_equal_to_rows_proves_completeness(self) -> None:
        self.assertTrue(self.result.complete)
        self.assertEqual(self.result.total, 3)
        self.assertEqual(len(self.result.jobs), 3)
        self.assertEqual(self.result.source, "public_page")

    def test_first_job_maps_every_0014_field(self) -> None:
        job = self.result.jobs[0]

        self.assertEqual(job["title"], "C++ 后端开发工程师")
        self.assertEqual(job["company"], "示例科技有限公司")
        self.assertEqual(job["external_id"], "CN-1001")
        self.assertEqual(job["location"], "深圳-南山区")
        self.assertEqual(job["salary_min"], 15)
        self.assertEqual(job["salary_max"], 25)
        self.assertEqual(job["salary_currency"], "CNY")
        self.assertEqual(job["education_required"], 3)
        self.assertEqual(job["job_nature"], 1)
        self.assertEqual(job["experience_years_min"], 0)
        self.assertNotIn("experience_years_max", job)
        # F13: a date-only deadline is stored as Beijing noon in UTC.
        self.assertEqual(job["deadline"], "2026-10-31 04:00:00")
        self.assertTrue(job["deadline_date_only"])
        self.assertTrue(is_date_only_stored(job["deadline"]))
        # published 2026-09-20 08:00 Beijing -> 00:00 UTC.
        self.assertEqual(job["published_at"], "2026-09-20 00:00:00")
        self.assertEqual(job["description"], "负责后端服务开发\n熟悉 Linux")

    def test_daily_salary_is_omitted_not_converted(self) -> None:
        job = self.result.jobs[1]

        self.assertNotIn("salary_min", job)
        self.assertNotIn("salary_max", job)
        self.assertNotIn("salary_currency", job)
        self.assertEqual(job["job_nature"], 3)

    def test_explicit_deadline_keeps_the_real_instant(self) -> None:
        job = self.result.jobs[1]

        self.assertEqual(job["deadline"], "2026-10-31 10:30:00")
        self.assertFalse(job["deadline_date_only"])
        self.assertNotIn("experience_years_min", job)
        self.assertNotIn("experience_years_max", job)

    def test_negotiable_salary_and_date_only_publication(self) -> None:
        job = self.result.jobs[2]

        self.assertNotIn("salary_min", job)
        self.assertEqual(job["education_required"], 4)
        self.assertEqual(job["job_nature"], 2)
        self.assertEqual(job["experience_years_min"], 3)
        self.assertEqual(job["experience_years_max"], 5)
        self.assertEqual(job["published_at"], "2026-09-18 04:00:00")
        self.assertEqual(job["deadline"], "2026-11-15 15:59:59")
        self.assertFalse(job["deadline_date_only"])

    def test_generated_payload_satisfies_the_cpp_contract(self) -> None:
        assert_contract(self, self.result.jobs)

    def test_total_mismatch_cannot_claim_completeness(self) -> None:
        payload = json.dumps({"total": 5, "data": [{"title": "A", "url": "https://x/1", "company": "C"}]})
        result = public_page.parse_board(payload, board="https://x/", company=None)

        self.assertFalse(result.complete)
        self.assertEqual(len(result.jobs), 1)

    def test_bare_list_never_claims_completeness(self) -> None:
        payload = json.dumps([{"title": "A", "url": "https://x/1", "company": "C"}])
        result = public_page.parse_board(payload, board="https://x/", company=None)

        self.assertFalse(result.complete)

    def test_configured_company_is_the_fallback(self) -> None:
        payload = json.dumps({"jobs": [{"title": "A", "url": "https://x/1"}]})
        result = public_page.parse_board(
            payload, board="https://x/", company="配置公司"
        )

        self.assertEqual(result.jobs[0]["company"], "配置公司")

    def test_rows_without_a_company_are_skipped(self) -> None:
        payload = json.dumps({"jobs": [{"title": "A", "url": "https://x/1"}]})
        result = public_page.parse_board(
            payload, board="https://x/", company=None
        )

        self.assertEqual(result.jobs, [])


class HtmlAndFeedTest(unittest.TestCase):
    def test_json_ld_page_is_never_complete(self) -> None:
        result = public_page.parse_board(
            text_fixture("cn_jsonld_page.html"),
            board="https://jobs.example-cn.invalid/campus",
            company=None,
        )

        self.assertFalse(result.complete)
        self.assertEqual(len(result.jobs), 2)
        assert_contract(self, result.jobs)

        self.assertEqual(result.jobs[0]["location"], "杭州")
        self.assertEqual(result.jobs[0]["deadline"], "2026-10-31 04:00:00")
        self.assertTrue(result.jobs[0]["deadline_date_only"])
        self.assertEqual(result.jobs[1]["deadline"], "2026-11-20 10:00:00")
        self.assertFalse(result.jobs[1]["deadline_date_only"])

    def test_feed_entries_become_jobs(self) -> None:
        result = public_page.parse_board(
            text_fixture("cn_feed.xml"),
            board="https://jobs.example-cn.invalid/campus",
            company="示例公司",
        )

        self.assertFalse(result.complete)
        # The third fixture entry has no link and is skipped.
        self.assertEqual(len(result.jobs), 2)
        self.assertEqual(result.jobs[0]["company"], "示例公司")
        self.assertEqual(result.jobs[0]["published_at"], "2026-10-08 04:00:00")
        self.assertEqual(result.jobs[0]["description"], "共 120 个岗位")
        assert_contract(self, result.jobs)

    def test_feed_without_a_company_publishes_nothing(self) -> None:
        result = public_page.parse_board(
            text_fixture("cn_feed.xml"),
            board="https://jobs.example-cn.invalid/campus",
            company=None,
        )

        self.assertEqual(result.jobs, [])
        self.assertFalse(result.complete)

    def test_sitemap_yields_no_jobs(self) -> None:
        result = public_page.parse_board(
            text_fixture("cn_sitemap.xml"),
            board="https://jobs.example-cn.invalid/sitemap.xml",
            company="示例公司",
        )

        self.assertEqual(result.jobs, [])
        self.assertFalse(result.complete)

    def test_unknown_and_malformed_payloads_never_raise(self) -> None:
        for payload in ("", "   ", "<html><body>no jobs</body></html>", "{oops"):
            with self.subTest(payload=payload):
                result = public_page.parse_board(
                    payload, board="https://x/", company="C"
                )
                self.assertEqual(result.jobs, [])
                self.assertFalse(result.complete)


    def test_preparsed_mapping_payload_is_accepted(self) -> None:
        document = {
            "total": 1,
            "data": [{"title": "A", "url": "https://x/1", "company": "C"}],
        }

        result = public_page.parse_board(
            document, board="https://x/", company=None
        )

        self.assertTrue(result.complete)
        self.assertEqual(len(result.jobs), 1)

    def test_non_text_non_mapping_payload_raises(self) -> None:
        with self.assertRaises(ProviderError):
            public_page.parse_board(42, board="https://x/", company="C")


class BoardUrlTest(unittest.TestCase):
    def test_absolute_http_url_is_required(self) -> None:
        self.assertEqual(
            public_page.board_url("https://x/jobs"), "https://x/jobs"
        )

        for bad in ("", "/jobs", "ftp://x/jobs", None):
            with self.subTest(board=bad):
                with self.assertRaises(ProviderError):
                    public_page.board_url(bad)


class BuildJobDomainTest(unittest.TestCase):
    """The shared builder must never emit a value the server rejects."""

    def test_out_of_domain_values_are_omitted(self) -> None:
        job = build_job(
            source="public_page",
            source_url="https://x/1",
            title="T",
            company="C",
            education_required=9,
            job_nature=0,
            experience_years_min=5,
            experience_years_max=2,
            deadline="2026-10-31 10:30:00",
            deadline_date_only=True,
        )

        self.assertNotIn("education_required", job)
        self.assertNotIn("job_nature", job)
        self.assertNotIn("experience_years_min", job)
        self.assertNotIn("experience_years_max", job)
        # A contradictory date-only flag drops the pair rather than inventing
        # an instant.
        self.assertNotIn("deadline", job)
        self.assertNotIn("deadline_date_only", job)

    def test_consistent_pairs_are_kept(self) -> None:
        date_only = build_job(
            source="public_page",
            source_url="https://x/1",
            title="T",
            company="C",
            deadline="2026-10-31 04:00:00",
            deadline_date_only=True,
        )
        explicit = build_job(
            source="public_page",
            source_url="https://x/1",
            title="T",
            company="C",
            deadline="2026-10-31 10:30:00",
            deadline_date_only=False,
        )

        self.assertTrue(date_only["deadline_date_only"])
        self.assertFalse(explicit["deadline_date_only"])

    def test_year_overflow_is_omitted(self) -> None:
        job = build_job(
            source="public_page",
            source_url="https://x/1",
            title="T",
            company="C",
            experience_years_min=40000,
        )

        self.assertNotIn("experience_years_min", job)


if __name__ == "__main__":
    unittest.main()
