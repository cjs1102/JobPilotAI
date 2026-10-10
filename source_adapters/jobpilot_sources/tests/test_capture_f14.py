"""Python-side F14 evidence: the manual channel cannot weaken a manual record.

The authoritative enforcement of "a source-sync observation never overwrites a
manual record" lives in the C++ observation upsert and is proven there by
``SourceSyncDaoTest.ManualRecordIsUnchangedByACollidingObservation`` and
``SourceSyncHttpTest.ManualRecordWinsOverObservation`` (0014 task-9). The Python
side owns the precondition those tests rely on: **both channels must derive the
same ``source_url`` idempotency key**, and the manual payload must be unable to
express anything the observation path would then be allowed to overwrite
(freshness, lifecycle, or the source attribution).
"""

from __future__ import annotations

import unittest
from pathlib import Path

from jobpilot_sources import normalize_cn
from jobpilot_sources.capture import (
    IMPORT_PATH,
    MANUAL_SOURCE,
    build_import_payload,
    build_import_payloads,
    parse_artifact,
)
from jobpilot_sources.fetch import normalize_url
from jobpilot_sources.normalizer import build_job
from jobpilot_sources.publisher import SYNC_PATH, build_batches
from jobpilot_sources.tests.support import load_fixture

FRESHNESS_AND_LIFECYCLE_KEYS = (
    "last_seen_at",
    "last_verified_at",
    "source_status",
    "missing_count",
    "closed_at",
    "source_scope",
    "observed_at",
    "complete",
)

DOCUMENTED_IMPORT_KEYS = {
    "company",
    "title",
    "location",
    "salary_min",
    "salary_max",
    "salary_currency",
    "description",
    "source",
    "source_url",
    "education_required",
    "experience_years_min",
    "experience_years_max",
    "job_nature",
    "deadline",
    "deadline_date_only",
    "published_at",
    "requirements",
}


class ManualChannelIsolationTest(unittest.TestCase):
    def test_payload_uses_manual_source_and_no_sync_fields(self) -> None:
        payloads, _notes = build_import_payloads(
            [parse_artifact(load_fixture("capture_complete.json"))]
        )

        for payload in payloads:
            self.assertEqual(payload["source"], MANUAL_SOURCE)
            self.assertTrue(set(payload).issubset(DOCUMENTED_IMPORT_KEYS))

            for key in FRESHNESS_AND_LIFECYCLE_KEYS:
                self.assertNotIn(key, payload)

    def test_the_two_channels_use_different_endpoints(self) -> None:
        self.assertEqual(IMPORT_PATH, "/internal/jobs/import")
        self.assertEqual(SYNC_PATH, "/internal/jobs/source-sync")
        self.assertNotEqual(IMPORT_PATH, SYNC_PATH)

    def test_capture_package_never_references_the_sync_endpoint(self) -> None:
        package = Path(__file__).resolve().parents[1] / "capture"

        for path in sorted(package.rglob("*.py")):
            text = path.read_text(encoding="utf-8")

            with self.subTest(module=path.name):
                self.assertNotIn("source-sync", text)
                self.assertNotIn(SYNC_PATH, text)


class IdempotencyKeyParityTest(unittest.TestCase):
    """A collision is only detectable when both channels agree on the key."""

    URLS = (
        "https://jobs.example.invalid/campus/1001?utm_source=bookmark",
        "HTTPS://Jobs.Example.INVALID:443/campus/1001/",
        "https://jobs.example.invalid/campus/1001?b=2&a=1#apply",
    )

    def test_manual_payload_key_equals_the_shared_normalizer(self) -> None:
        for url in self.URLS:
            with self.subTest(url=url):
                capture = parse_artifact(
                    {
                        "title": "T",
                        "company": "C",
                        "source_url": url,
                    }
                )
                payload = build_import_payload(capture).payload

                self.assertEqual(payload["source_url"], normalize_url(url))

    def test_adapter_observation_carries_the_same_key(self) -> None:
        url = "HTTPS://Jobs.Example.INVALID:443/campus/1001/?utm_source=x"
        expected = normalize_url(url)

        manual = build_import_payload(
            parse_artifact(
                {"title": "T", "company": "C", "source_url": url}
            )
        ).payload

        # Build the observation-side job exactly as an adapter would.
        adapter_job = build_job(
            source="greenhouse",
            source_url=expected,
            title="T",
            company="C",
        )
        batches = build_batches(
            source="greenhouse",
            scope="example-board",
            observed_at="2026-10-09 12:00:00",
            jobs=[adapter_job],
            complete=False,
        )

        observation_url = batches[0]["jobs"][0]["source_url"]

        self.assertEqual(manual["source_url"], expected)
        self.assertEqual(observation_url, expected)
        self.assertEqual(manual["source_url"], observation_url)

    def test_a_capture_normalization_cannot_diverge_from_the_adapters(self) -> None:
        # The capture publisher must call the shared normalizer rather than
        # re-implementing URL cleanup, or F14 collisions would silently miss.
        text = (
            Path(__file__).resolve().parents[1]
            / "capture"
            / "publisher.py"
        ).read_text(encoding="utf-8")

        self.assertIn("normalize_url", text)
        self.assertIn("from ..fetch import normalize_url", text)


class NormalizationReuseTest(unittest.TestCase):
    """The manual path reuses the adapters' deterministic mappings."""

    def test_matches_the_shared_chinese_mappings(self) -> None:
        payload = build_import_payload(
            parse_artifact(
                {
                    "title": "T",
                    "company": "C",
                    "source_url": "https://x/1",
                    "education": "硕士",
                    "job_nature": "社招",
                    "experience": "3-5年",
                    "location": "深圳市-南山区",
                }
            )
        ).payload

        self.assertEqual(
            payload["education_required"],
            normalize_cn.normalize_education("硕士"),
        )
        self.assertEqual(
            payload["job_nature"],
            normalize_cn.normalize_job_nature("社招"),
        )
        self.assertEqual(payload["experience_years_min"], 3)
        self.assertEqual(payload["experience_years_max"], 5)
        self.assertEqual(payload["location"], "深圳-南山区")


class LiveEvidencePointerTest(unittest.TestCase):
    def test_opt_in_e2e_verifier_is_committed_and_compiles(self) -> None:
        import py_compile

        tool = (
            Path(__file__).resolve().parents[2]
            / "tools"
            / "verify_manual_capture_e2e.py"
        )

        self.assertTrue(tool.is_file(), tool)

        # Compiles without executing (the script needs a running service).
        py_compile.compile(str(tool), doraise=True, cfile="/tmp/_e2e_check.pyc")

        text = tool.read_text(encoding="utf-8")
        self.assertIn("skipped_manual", text)
        self.assertIn("byte-identical", text)


if __name__ == "__main__":
    unittest.main()
