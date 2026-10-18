"""Contract test for the 2026-10-19 nowcoder probe evidence.

The probe record is the audit trail behind ExecPlan 0018: robots, the bounded
20-row list, both record shapes and the two corrections to the earlier
session's claims. It must stay machine-readable, free of credentials/PII, and
consistent with the registered source list.
"""

from __future__ import annotations

import json
import re
import unittest
from pathlib import Path

from jobpilot_sources.config import SUPPORTED_PROVIDERS
from jobpilot_sources.tests.support import FIXTURES, text_fixture

PROBE_FILE = "cn_source_probe_2026-10-19.json"
README = Path(__file__).resolve().parents[2] / "README.md"

ISO_DATE_RE = re.compile(r"^\d{4}-\d{2}-\d{2}$")
CREDENTIAL_RE = re.compile(
    r"(token|secret|password|authorization|bearer|apikey|api_key)\s*[:=]\s*"
    r"[\"']?[A-Za-z0-9_\-]{8,}",
    re.IGNORECASE,
)
PII_RE = re.compile(r"\b1[3-9]\d{9}\b|[\w.+-]+@[\w-]+\.[\w.]{2,}")


def load_probe() -> dict:
    return json.loads(text_fixture(PROBE_FILE))


class ProbeRecordTest(unittest.TestCase):
    def setUp(self) -> None:
        self.probe = load_probe()

    def test_header(self) -> None:
        self.assertEqual(self.probe["probe_version"], 1)
        self.assertRegex(self.probe["probe_date"], ISO_DATE_RE)
        self.assertIn("jobpilot-source-adapters", self.probe["user_agent"])
        self.assertLessEqual(self.probe["budget"]["requests"], 20)
        self.assertLessEqual(
            self.probe["budget"]["request_budget_minutes"], 5
        )

    def test_robots_evidence(self) -> None:
        robots = self.probe["robots"]

        self.assertEqual(robots["status"], 200)
        self.assertEqual(
            robots["disallow"],
            ["/search", "/nccommon", "/ab/ab-test-flow"],
        )
        self.assertIsNone(robots["crawl_delay_seconds"])
        self.assertEqual(
            robots["verdicts"]["/jobs/school/jobs"], "allowed"
        )
        self.assertEqual(
            robots["verdicts"]["/jobs/detail/404338"], "allowed"
        )
        self.assertEqual(robots["verdicts"]["/search"], "disallowed")

    def test_list_is_bounded_and_incomplete(self) -> None:
        listing = self.probe["list"]

        self.assertEqual(listing["max_rows_observed"], 20)
        self.assertEqual(listing["pagination"], "none")
        self.assertFalse(listing["module_number_is_stable"])
        self.assertIn("complete=false", self.probe["conclusion"])

    def test_both_record_shapes_are_recorded(self) -> None:
        shapes = self.probe["record_shapes"]

        self.assertIn("ext", shapes["native"]["fields"])
        self.assertIn("description", shapes["aggregator"]["fields"])
        self.assertEqual(shapes["aggregator"]["observed_platform"], "moka")

    def test_detail_ssr_correction_is_recorded(self) -> None:
        detail = self.probe["detail"]

        self.assertTrue(detail["native"]["ssr"])
        self.assertTrue(detail["aggregator"]["ssr"])
        self.assertIn(
            "globalJobId", detail["aggregator"]["fields"]
        )

    def test_no_credential_or_pii_values(self) -> None:
        rendered = json.dumps(self.probe, ensure_ascii=False)

        self.assertIsNone(CREDENTIAL_RE.search(rendered), "credential-shaped value")
        self.assertIsNone(PII_RE.search(rendered), "PII-shaped value")

    def test_registered_provider_and_readme_pointer(self) -> None:
        self.assertIn("nowcoder", SUPPORTED_PROVIDERS)

        readme = README.read_text(encoding="utf-8")
        self.assertIn("nowcoder", readme.lower())
        self.assertIn("2026-10-19", readme)

    def test_fixture_is_plain_json(self) -> None:
        raw = (FIXTURES / PROBE_FILE).read_bytes()
        self.assertTrue(raw.startswith(b"{"))


if __name__ == "__main__":
    unittest.main()
