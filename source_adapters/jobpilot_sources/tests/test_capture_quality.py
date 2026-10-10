"""Mirrored 0015A pre-ingest quality checks (Python side of the C++ engine).

Every stable code, outcome and threshold here mirrors
``service/src/job/job_quality.cpp``; the 48 KiB cap is measured in UTF-8 bytes
and is never satisfied by a character count.
"""

from __future__ import annotations

import re
import unittest
from pathlib import Path

import jobpilot_sources
from jobpilot_sources.capture import (
    MAX_DESCRIPTION_BYTES,
    build_import_payload,
    evaluate_batch_quality,
    evaluate_item_quality,
    parse_artifact,
)
from jobpilot_sources.capture.quality import (
    CODE_COMPANY_MISSING,
    CODE_DESCRIPTION_MARKER_MINOR,
    CODE_DESCRIPTION_POLLUTED,
    CODE_DESCRIPTION_TOO_LARGE,
    CODE_DUPLICATE_CONTENT_IN_BATCH,
    CODE_EMPLOYER_IMPLAUSIBLE,
    CODE_EMPLOYER_IS_PLATFORM,
    CODE_SOURCE_URL_NOT_DETAIL,
    CODE_SOURCE_URL_UNRECOGNISED,
    CODE_SOURCE_URL_UNUSABLE,
    CODE_TITLE_EQUALS_COMPANY,
    OUTCOME_ADVISORY,
    OUTCOME_NEEDS_REVIEW,
    OUTCOME_REJECTED,
    capture_error_finding,
    is_all_ascii_digits,
    is_clearly_not_detail,
    marker_stats,
    utf8_size,
)
from jobpilot_sources.registry import PlatformNameRegistry
from jobpilot_sources.tests.support import load_fixture

PACKAGE_ROOT = Path(jobpilot_sources.__file__).resolve().parent
REPO_ROOT = PACKAGE_ROOT.parents[1]
CPP_HEADER = (
    REPO_ROOT / "service" / "include" / "jobpilot" / "service" / "job"
    / "job_quality.h"
)
BOOKMARKLET_JS = PACKAGE_ROOT / "capture" / "bookmarklet.js"

PAYLOAD = {
    "company": "示例科技有限公司",
    "title": "C++ 后端开发工程师",
    "source": "manual",
    "source_url": "https://jobs.example-cn.invalid/campus/1001",
    "location": "深圳-南山区",
    "description": "负责后端服务开发，参与需求评审。",
}

REGISTRY = PlatformNameRegistry.from_names(["牛客"])


def payload(**overrides) -> dict:
    document = dict(PAYLOAD)
    document.update(overrides)

    return document


def codes(item) -> list[str]:
    return [finding.code for finding in item.findings]


class DescriptionByteCapTest(unittest.TestCase):
    def test_the_cap_is_48_kib(self) -> None:
        self.assertEqual(MAX_DESCRIPTION_BYTES, 49152)
        self.assertEqual(48 * 1024, MAX_DESCRIPTION_BYTES)

    def test_exactly_49152_bytes_is_accepted(self) -> None:
        item = evaluate_item_quality(payload(description="a" * 49152))

        self.assertEqual(item.outcome, "clean")
        self.assertEqual(item.findings, ())
        self.assertEqual(item.review_status, 0)

    def test_49153_bytes_is_rejected_with_limit_and_actual(self) -> None:
        item = evaluate_item_quality(payload(description="a" * 49153))

        self.assertEqual(item.outcome, OUTCOME_REJECTED)
        self.assertEqual(item.diagnostic_code, CODE_DESCRIPTION_TOO_LARGE)
        self.assertEqual(item.diagnostic_field, "description")
        self.assertEqual(item.review_status, 0)

        finding = item.findings[0]
        self.assertEqual(finding.code, CODE_DESCRIPTION_TOO_LARGE)
        self.assertEqual(finding.outcome, OUTCOME_REJECTED)
        self.assertEqual(finding.limit_bytes, 49152)
        self.assertEqual(finding.actual_bytes, 49153)
        self.assertIn("49153 bytes", finding.reason)
        self.assertIn("not truncated", finding.reason)

    def test_multibyte_cjk_proves_the_cap_is_bytes_not_characters(self) -> None:
        accepted = "汉" * 16384
        rejected = "汉" * 16385

        self.assertEqual(utf8_size(accepted), 49152)
        self.assertEqual(utf8_size(rejected), 49155)
        # A character-based check would accept `rejected` (16,385 < 49,152).
        self.assertLess(len(rejected), MAX_DESCRIPTION_BYTES)

        self.assertEqual(
            evaluate_item_quality(payload(description=accepted)).outcome,
            "clean",
        )

        item = evaluate_item_quality(payload(description=rejected))

        self.assertEqual(item.outcome, OUTCOME_REJECTED)
        self.assertEqual(item.findings[0].actual_bytes, 49155)
        self.assertEqual(item.findings[0].limit_bytes, 49152)

    def test_evaluation_never_mutates_or_truncates_the_description(self) -> None:
        description = "汉" * 16385
        document = payload(description=description)

        evaluate_item_quality(document)

        self.assertEqual(document["description"], description)
        self.assertEqual(utf8_size(document["description"]), 49155)

    def test_the_cap_matches_the_cpp_engine_constant(self) -> None:
        if not CPP_HEADER.is_file():
            self.skipTest("C++ header is not present")

        text = CPP_HEADER.read_text(encoding="utf-8")
        match = re.search(
            r"kMaxDescriptionBytes\s*=\s*(\d+)U\s*\*\s*(\d+)U", text
        )

        self.assertIsNotNone(match, "kMaxDescriptionBytes was not found")
        assert match is not None

        self.assertEqual(
            int(match.group(1)) * int(match.group(2)),
            MAX_DESCRIPTION_BYTES,
        )


class EmployerTest(unittest.TestCase):
    def test_missing_company_is_rejected(self) -> None:
        for company in ("", "   ", None):
            with self.subTest(company=company):
                item = evaluate_item_quality(payload(company=company))

                self.assertEqual(item.outcome, OUTCOME_REJECTED)
                self.assertEqual(item.diagnostic_code, CODE_COMPANY_MISSING)
                self.assertEqual(item.diagnostic_field, "company")

    def test_registered_platform_name_needs_review(self) -> None:
        item = evaluate_item_quality(
            payload(company=" 牛客网 "), registry=REGISTRY
        )

        self.assertEqual(item.outcome, OUTCOME_NEEDS_REVIEW)
        self.assertEqual(item.review_status, 1)
        self.assertIn(CODE_EMPLOYER_IS_PLATFORM, codes(item))

    def test_unknown_platform_name_fails_open(self) -> None:
        for company in ("某公司", "示例科技有限公司", "牛客科技"):
            with self.subTest(company=company):
                item = evaluate_item_quality(
                    payload(company=company), registry=REGISTRY
                )

                self.assertEqual(item.outcome, "clean")
                self.assertNotIn(CODE_EMPLOYER_IS_PLATFORM, codes(item))

    def test_without_a_registry_no_platform_finding_is_emitted(self) -> None:
        item = evaluate_item_quality(payload(company="牛客"))

        self.assertEqual(item.outcome, "clean")

    def test_implausible_employer_is_advisory_and_stays_clean(self) -> None:
        for company in ("5", "12345"):
            with self.subTest(company=company):
                item = evaluate_item_quality(payload(company=company))

                self.assertEqual(item.outcome, "clean")
                self.assertEqual(item.review_status, 0)
                self.assertIn(CODE_EMPLOYER_IMPLAUSIBLE, codes(item))
                self.assertEqual(
                    item.findings[0].outcome, OUTCOME_ADVISORY
                )

    def test_single_cjk_character_employer_is_plausible_like_cpp(self) -> None:
        # C++ compares `normalized.size() < 2` in BYTES, and one CJK character
        # is 3 bytes, so it is not implausible there; the Python side matches.
        item = evaluate_item_quality(payload(company="王"))

        self.assertNotIn(CODE_EMPLOYER_IMPLAUSIBLE, codes(item))
        self.assertEqual(item.outcome, "clean")

    def test_all_ascii_digits_helper(self) -> None:
        self.assertTrue(is_all_ascii_digits("123"))
        self.assertFalse(is_all_ascii_digits(""))
        self.assertFalse(is_all_ascii_digits("１２３"))  # fullwidth

    def test_title_equals_company_needs_review(self) -> None:
        item = evaluate_item_quality(
            payload(company="示例科技有限公司", title=" 示例科技有限公司 ")
        )

        self.assertEqual(item.outcome, OUTCOME_NEEDS_REVIEW)
        self.assertIn(CODE_TITLE_EQUALS_COMPANY, codes(item))


class SourceUrlTest(unittest.TestCase):
    def test_non_http_urls_are_rejected(self) -> None:
        for url in ("", "not-a-url", "ftp://x.invalid/1", "/jobs/1",
                    "javascript:alert(1)"):
            with self.subTest(url=url):
                item = evaluate_item_quality(payload(source_url=url))

                self.assertEqual(item.outcome, OUTCOME_REJECTED)
                self.assertEqual(
                    item.diagnostic_code, CODE_SOURCE_URL_UNUSABLE
                )

    def test_list_and_root_urls_need_review(self) -> None:
        for url in (
            "https://www.shixiseng.invalid/",
            "https://www.example.invalid/jobs",
            "https://x.invalid/search?q=1",
            "https://x.invalid?keyword=1",
        ):
            with self.subTest(url=url):
                item = evaluate_item_quality(payload(source_url=url))

                self.assertEqual(item.outcome, OUTCOME_NEEDS_REVIEW)
                self.assertIn(CODE_SOURCE_URL_NOT_DETAIL, codes(item))

    def test_detail_urls_stay_clean(self) -> None:
        for url in (
            "https://www.example-cn.invalid/campus/1001",
            "https://x.invalid/detail/software-engineer",
            "https://x.invalid/jobs/software-engineer",
        ):
            with self.subTest(url=url):
                item = evaluate_item_quality(payload(source_url=url))

                self.assertEqual(item.outcome, "clean")
                self.assertEqual(item.findings, ())

    def test_unrecognised_shape_is_advisory(self) -> None:
        item = evaluate_item_quality(
            payload(source_url="https://x.invalid/company/acme")
        )

        self.assertEqual(item.outcome, "clean")
        self.assertIn(CODE_SOURCE_URL_UNRECOGNISED, codes(item))
        self.assertEqual(item.findings[0].outcome, OUTCOME_ADVISORY)

    def test_helper_mirrors_the_cpp_predicate(self) -> None:
        self.assertTrue(is_clearly_not_detail("", False))
        self.assertTrue(is_clearly_not_detail("/", False))
        self.assertTrue(is_clearly_not_detail("/jobs/", False))
        self.assertTrue(is_clearly_not_detail("/list", False))
        # The query-only branch fires when the trimmed path carries no slash.
        self.assertTrue(is_clearly_not_detail("whatever", True))
        self.assertFalse(is_clearly_not_detail("/whatever", True))
        self.assertFalse(is_clearly_not_detail("/campus/1001", False))


class DescriptionPollutionTest(unittest.TestCase):
    POLLUTED = "推荐岗位相关职位热门职位猜你喜欢"

    def test_above_both_thresholds_needs_review(self) -> None:
        item = evaluate_item_quality(payload(description=self.POLLUTED))

        self.assertEqual(item.outcome, OUTCOME_NEEDS_REVIEW)
        self.assertIn(CODE_DESCRIPTION_POLLUTED, codes(item))

    def test_one_marker_is_advisory_only(self) -> None:
        item = evaluate_item_quality(
            payload(description="推荐岗位" + "x" * 1000)
        )

        self.assertEqual(item.outcome, "clean")
        self.assertIn(CODE_DESCRIPTION_MARKER_MINOR, codes(item))

    def test_distinct_markers_below_the_share_threshold_are_advisory(self) -> None:
        item = evaluate_item_quality(
            payload(description=self.POLLUTED + "x" * 1000)
        )

        self.assertEqual(item.outcome, "clean")
        self.assertIn(CODE_DESCRIPTION_MARKER_MINOR, codes(item))
        self.assertNotIn(CODE_DESCRIPTION_POLLUTED, codes(item))

    def test_the_share_is_measured_in_bytes(self) -> None:
        distinct, share = marker_stats(self.POLLUTED)

        self.assertEqual(distinct, 4)
        self.assertEqual(share, 100)

        # Four markers (48 bytes) inside 6,000 bytes of CJK text (18,000 bytes)
        # is under 20 %, so it can never queue.
        text = self.POLLUTED + "汉" * 2000

        self.assertLess(marker_stats(text)[1], 20)

    def test_no_description_no_finding(self) -> None:
        document = payload()
        document.pop("description")

        item = evaluate_item_quality(document)

        self.assertEqual(item.findings, ())

    def test_empty_description_no_finding(self) -> None:
        item = evaluate_item_quality(payload(description=""))

        self.assertEqual(item.findings, ())


class BatchTest(unittest.TestCase):
    def test_duplicate_content_under_different_urls_needs_review(self) -> None:
        first = payload(source_url="https://x.invalid/campus/1001")
        second = payload(source_url="https://x.invalid/campus/1002")

        report = evaluate_batch_quality([first, second])

        self.assertEqual(report.counters.needs_review, 2)
        self.assertEqual(report.counters.rejected, 0)

        for item in report.items:
            self.assertIn(CODE_DUPLICATE_CONTENT_IN_BATCH, codes(item))

    def test_same_url_is_not_a_duplicate_finding_here(self) -> None:
        report = evaluate_batch_quality([payload(), payload()])

        self.assertEqual(report.counters.clean, 2)

    def test_different_employer_is_not_a_duplicate(self) -> None:
        report = evaluate_batch_quality(
            [
                payload(source_url="https://x.invalid/campus/1001"),
                payload(
                    company="另一家公司",
                    source_url="https://x.invalid/campus/1002",
                ),
            ]
        )

        self.assertEqual(report.counters.clean, 2)

    def test_counters_and_status_derivation(self) -> None:
        report = evaluate_batch_quality(
            [
                payload(),
                payload(
                    company="牛客网",
                    source_url="https://x.invalid/campus/2002",
                ),
                payload(company="", source_url="https://x.invalid/campus/3003"),
            ],
            registry=REGISTRY,
        )

        self.assertEqual(report.counters.clean, 1)
        self.assertEqual(report.counters.needs_review, 1)
        self.assertEqual(report.counters.rejected, 1)
        self.assertEqual(report.counters.advisory, 0)

        self.assertEqual(report.items[0].review_status, 0)
        self.assertEqual(report.items[1].review_status, 1)
        self.assertEqual(report.items[2].outcome, OUTCOME_REJECTED)
        self.assertEqual(report.items[2].review_status, 0)
        self.assertEqual(len(report.rejected_items), 1)
        self.assertEqual(len(report.needs_review_items), 1)

    def test_reject_wins_over_needs_review(self) -> None:
        report = evaluate_batch_quality(
            [payload(company="   ", title="anything")], registry=REGISTRY
        )

        item = report.items[0]

        self.assertEqual(item.outcome, OUTCOME_REJECTED)
        self.assertEqual(item.diagnostic_code, CODE_COMPANY_MISSING)
        self.assertEqual(item.review_status, 0)

    def test_json_shape(self) -> None:
        report = evaluate_batch_quality(
            [payload(description="a" * 49153)], registry=REGISTRY
        )
        document = report.to_json()

        self.assertEqual(
            set(document), {"counters", "items"}
        )
        self.assertEqual(
            document["counters"],
            {"clean": 0, "needs_review": 0, "rejected": 1, "advisory": 0},
        )

        item = document["items"][0]

        self.assertEqual(
            set(item),
            {
                "index",
                "source_url",
                "outcome",
                "review_status",
                "diagnostic_code",
                "diagnostic_field",
                "diagnostic_reason",
                "findings",
            },
        )

        finding = item["findings"][0]

        self.assertEqual(
            set(finding),
            {
                "code",
                "reason",
                "outcome",
                "review_status",
                "field",
                "limit_bytes",
                "actual_bytes",
            },
        )
        self.assertEqual(finding["code"], CODE_DESCRIPTION_TOO_LARGE)
        self.assertEqual(finding["limit_bytes"], 49152)
        self.assertEqual(finding["actual_bytes"], 49153)
        self.assertEqual(finding["review_status"], 0)


class IntegrationWithTheCapturePipelineTest(unittest.TestCase):
    def test_a_real_capture_payload_is_clean(self) -> None:
        capture = parse_artifact(load_fixture("capture_complete.json"))
        built = build_import_payload(capture)

        report = evaluate_batch_quality(
            [built.payload],
            registry=PlatformNameRegistry.load(
                PACKAGE_ROOT / "data" / "platform_names.yaml"
            ),
        )

        self.assertEqual(report.counters.clean, 1)
        self.assertEqual(report.counters.rejected, 0)
        self.assertEqual(report.items[0].findings, ())

    def test_a_capture_object_works_as_an_input(self) -> None:
        capture = parse_artifact(
            {
                "title": "运维工程师",
                "company": "牛客",
                "source_url": "https://x.invalid/campus/2002",
            }
        )

        item = evaluate_item_quality(capture, registry=REGISTRY)

        self.assertEqual(item.outcome, OUTCOME_NEEDS_REVIEW)
        self.assertIn(CODE_EMPLOYER_IS_PLATFORM, codes(item))


class BookmarkletDefaultTest(unittest.TestCase):
    def test_the_character_default_is_unchanged(self) -> None:
        source = BOOKMARKLET_JS.read_text(encoding="utf-8")

        self.assertIn("var DEFAULT_MAX_DESCRIPTION = 4000;", source)
        self.assertIn(
            "job.description.length > maxDescription", source
        )
        self.assertNotIn("49152", source)


class CaptureErrorProjectionTest(unittest.TestCase):
    def test_a_structured_error_becomes_a_finding(self) -> None:
        from jobpilot_sources.capture import CaptureError

        error = CaptureError(
            "job.description is too large",
            code=CODE_DESCRIPTION_TOO_LARGE,
            field="description",
            limit_bytes=49152,
            actual_bytes=49153,
        )

        finding = capture_error_finding(error)

        self.assertIsNotNone(finding)
        assert finding is not None

        self.assertEqual(finding["code"], CODE_DESCRIPTION_TOO_LARGE)
        self.assertEqual(finding["outcome"], OUTCOME_REJECTED)
        self.assertEqual(finding["limit_bytes"], 49152)
        self.assertEqual(finding["actual_bytes"], 49153)

    def test_an_unstructured_error_has_no_finding(self) -> None:
        from jobpilot_sources.capture import CaptureError

        self.assertIsNone(
            capture_error_finding(CaptureError("no code attached"))
        )

    def test_outcome_vocabulary_matches_the_database_values(self) -> None:
        self.assertEqual(
            (OUTCOME_REJECTED, OUTCOME_NEEDS_REVIEW, OUTCOME_ADVISORY),
            ("rejected", "needs_review", "accepted_advisory"),
        )


if __name__ == "__main__":
    unittest.main()
