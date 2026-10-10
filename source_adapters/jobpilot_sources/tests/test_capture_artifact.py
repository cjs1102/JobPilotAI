"""Tests for capture artifact parsing/validation (untrusted clipboard input)."""

from __future__ import annotations

import json
import unittest

from jobpilot_sources.capture import (
    CaptureError,
    MAX_CAPTURES_PER_RUN,
    parse_artifact,
    parse_captures,
)
from jobpilot_sources.tests.support import load_fixture


class CompleteArtifactTest(unittest.TestCase):
    def setUp(self) -> None:
        self.document = load_fixture("capture_complete.json")
        self.capture = parse_artifact(self.document)

    def test_every_field_is_read(self) -> None:
        self.assertEqual(self.capture.title, "C++ 后端开发工程师")
        self.assertEqual(self.capture.company, "示例科技有限公司")
        self.assertEqual(
            self.capture.source_url,
            "https://jobs.example-cn.invalid/campus/1001?utm_source=bookmark",
        )
        self.assertEqual(self.capture.location, "深圳市-南山区")
        self.assertEqual(self.capture.salary, "15-25K·14薪")
        self.assertEqual(self.capture.education, "本科及以上")
        self.assertEqual(self.capture.job_nature, "校招")
        self.assertEqual(self.capture.experience, "应届生")
        self.assertEqual(self.capture.deadline, "截止时间：2026-10-31")
        self.assertEqual(self.capture.published_at, "2026-09-20 08:00:00")
        self.assertIn("负责后端服务开发", self.capture.description or "")
        self.assertEqual(
            self.capture.skills, ("C++", "Linux", "MySQL")
        )
        self.assertEqual(
            self.capture.warnings,
            ("description is an excerpt of the visible page text",),
        )
        self.assertEqual(self.capture.page_url, self.document["page_url"])
        self.assertEqual(self.capture.captured_at, self.document["captured_at"])

    def test_bare_job_object_is_accepted(self) -> None:
        capture = parse_artifact(
            {
                "title": "T",
                "company": "C",
                "source_url": "https://x/1",
            }
        )

        self.assertEqual(capture.source_url, "https://x/1")
        self.assertIsNone(capture.location)

    def test_source_url_falls_back_to_page_url(self) -> None:
        capture = parse_artifact(
            {"page_url": "https://x/jobs/1", "job": {"title": "T", "company": "C"}}
        )

        self.assertEqual(capture.source_url, "https://x/jobs/1")

    def test_unknown_keys_are_tolerated(self) -> None:
        document = dict(self.document)
        document["future_field"] = {"anything": True}
        job = dict(document["job"])
        job["future_job_field"] = 42
        document["job"] = job

        capture = parse_artifact(document)

        self.assertEqual(capture.title, self.capture.title)


class MinimalArtifactTest(unittest.TestCase):
    def test_missing_optionals_are_omitted_not_invented(self) -> None:
        capture = parse_artifact(load_fixture("capture_minimal.json"))

        self.assertEqual(capture.title, "运维工程师")
        self.assertEqual(capture.company, "示例运维有限公司")
        self.assertIsNone(capture.salary)
        self.assertIsNone(capture.location)
        self.assertIsNone(capture.education)
        self.assertIsNone(capture.job_nature)
        self.assertIsNone(capture.experience)
        self.assertIsNone(capture.deadline)
        self.assertIsNone(capture.published_at)
        self.assertEqual(capture.source_url, "https://app.example-ats.invalid/social/2002")
        self.assertEqual(len(capture.warnings), 2)


class RejectionTest(unittest.TestCase):
    def test_missing_required_company_is_diagnosable(self) -> None:
        with self.assertRaises(CaptureError) as context:
            parse_artifact(load_fixture("capture_missing_company.json"))

        self.assertIn("job.company", str(context.exception))
        self.assertIn("required", str(context.exception))

    def test_missing_title_is_rejected(self) -> None:
        with self.assertRaises(CaptureError) as context:
            parse_artifact({"company": "C", "source_url": "https://x/1"})

        self.assertIn("job.title", str(context.exception))

    def test_missing_source_url_is_rejected(self) -> None:
        with self.assertRaises(CaptureError) as context:
            parse_artifact({"title": "T", "company": "C"})

        self.assertIn("source_url", str(context.exception))

    def test_invalid_urls_are_rejected(self) -> None:
        for url in ("/jobs/1", "javascript:alert(1)", "ftp://x/1", "https://"):
            with self.subTest(url=url):
                with self.assertRaises(CaptureError):
                    parse_artifact(
                        {
                            "title": "T",
                            "company": "C",
                            "source_url": url,
                        }
                    )

    def test_wrong_types_are_rejected(self) -> None:
        cases = (
            {"title": 7, "company": "C", "source_url": "https://x/1"},
            {"title": "T", "company": None, "source_url": "https://x/1"},
            {"title": "T", "company": "C", "source_url": 12},
            {"title": "T", "company": "C", "source_url": "https://x/1", "skills": 3},
            {"title": "T", "company": "C", "source_url": "https://x/1", "skills": [1]},
        )

        for document in cases:
            with self.subTest(document=document):
                with self.assertRaises(CaptureError):
                    parse_artifact(document)

    def test_over_long_fields_are_rejected_never_truncated(self) -> None:
        cases = (
            ("title", "x" * 129),
            ("company", "x" * 129),
            ("location", "x" * 65),
            ("description", "x" * 65536),
        )

        for key, value in cases:
            with self.subTest(key=key):
                document = {
                    "title": "T",
                    "company": "C",
                    "source_url": "https://x/1",
                    key: value,
                }

                with self.assertRaises(CaptureError) as context:
                    parse_artifact(document)

                self.assertIn(key, str(context.exception))
                self.assertIn("over the", str(context.exception))

    def test_over_long_source_url_is_rejected(self) -> None:
        with self.assertRaises(CaptureError) as context:
            parse_artifact(
                {
                    "title": "T",
                    "company": "C",
                    "source_url": "https://x/" + "a" * 300,
                }
            )

        self.assertIn("source_url", str(context.exception))

    def test_unsupported_capture_version_is_rejected(self) -> None:
        with self.assertRaises(CaptureError) as context:
            parse_artifact(
                {
                    "capture_version": 2,
                    "title": "T",
                    "company": "C",
                    "source_url": "https://x/1",
                }
            )

        self.assertIn("capture_version", str(context.exception))

    def test_non_object_documents_are_rejected(self) -> None:
        for document in ([1, 2], "text", 5, None):
            with self.subTest(document=document):
                with self.assertRaises(CaptureError):
                    parse_artifact(document)


class BatchTest(unittest.TestCase):
    def test_single_object_list_and_wrapper_are_accepted(self) -> None:
        capture = load_fixture("capture_minimal.json")

        self.assertEqual(len(parse_captures(capture)), 1)
        self.assertEqual(len(parse_captures([capture])), 1)
        self.assertEqual(len(parse_captures({"captures": [capture]})), 1)

    def test_empty_batch_is_rejected(self) -> None:
        for document in ([], {"captures": []}):
            with self.subTest(document=document):
                with self.assertRaises(CaptureError):
                    parse_captures(document)

    def test_exact_duplicate_source_url_is_rejected_at_parse_time(self) -> None:
        capture = {
            "title": "T",
            "company": "C",
            "source_url": "https://x/1",
        }

        with self.assertRaises(CaptureError) as context:
            parse_captures([capture, dict(capture)])

        self.assertIn("repeats source_url", str(context.exception))

    def test_batch_ceiling(self) -> None:
        capture = {
            "title": "T",
            "company": "C",
            "source_url": "https://x/1",
        }

        with self.assertRaises(CaptureError) as context:
            parse_captures(
                [
                    dict(capture, source_url=f"https://x/{index}")
                    for index in range(MAX_CAPTURES_PER_RUN + 1)
                ]
            )

        self.assertIn("over the", str(context.exception))

    def test_index_is_reported_for_a_bad_entry(self) -> None:
        with self.assertRaises(CaptureError) as context:
            parse_captures(
                [
                    {"title": "T", "company": "C", "source_url": "https://x/1"},
                    {"title": "T", "company": "C"},
                ]
            )

        self.assertIn("capture[1]", str(context.exception))

    def test_json_round_trip_of_the_fixture_stays_valid(self) -> None:
        text = json.dumps(load_fixture("capture_complete.json"))

        self.assertEqual(len(parse_captures(json.loads(text))), 1)


if __name__ == "__main__":
    unittest.main()
