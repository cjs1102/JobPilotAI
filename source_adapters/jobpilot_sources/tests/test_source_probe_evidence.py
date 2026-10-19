"""Contract test for the recorded Step 5 source-probe evidence.

The Step 5 verification produced no usable adapter, so what must be replayable
and auditable is the *evidence*: the per-candidate probe record, its verdicts,
and their consistency with the published 来源登记表 in ``README.md``. This test
also guards the record against credential/PII-shaped values and against silent
shape drift.
"""

from __future__ import annotations

import json
import re
import unittest
from pathlib import Path

from jobpilot_sources.tests.support import FIXTURES, text_fixture

PROBE_FILE = "cn_source_probe_2026-10-09.json"
README = Path(__file__).resolve().parents[2] / "README.md"

ISO_DATE_RE = re.compile(r"^\d{4}-\d{2}-\d{2}$")
CREDENTIAL_RE = re.compile(
    r"(token|secret|password|authorization|bearer|apikey|api_key)\s*[:=]\s*"
    r"[\"']?[A-Za-z0-9_\-]{8,}",
    re.IGNORECASE,
)
# 11-digit mainland mobile numbers, and any e-mail address.
PII_RE = re.compile(r"\b1[3-9]\d{9}\b|[\w.+-]+@[\w-]+\.[\w.]{2,}")


def load_probe() -> dict:
    return json.loads(text_fixture(PROBE_FILE))


class ProbeRecordShapeTest(unittest.TestCase):
    def setUp(self) -> None:
        self.probe = load_probe()

    def test_record_has_a_date_host_and_agent(self) -> None:
        self.assertEqual(self.probe["probe_version"], 1)
        self.assertRegex(self.probe["probe_date"], ISO_DATE_RE)
        self.assertIn("jobpilot-source-adapters", self.probe["user_agent"])
        self.assertIn("http", self.probe["user_agent"])

    def test_every_candidate_has_robots_and_a_verdict(self) -> None:
        candidates = self.probe["candidates"]

        self.assertEqual(len(candidates), 2)

        for candidate in candidates:
            with self.subTest(candidate=candidate["id"]):
                self.assertIn("robots", candidate)
                self.assertIn("status", candidate["robots"])
                self.assertIn("verdict", candidate)
                self.assertTrue(candidate["verdict"].strip())

    def test_no_credential_or_pii_values_are_recorded(self) -> None:
        rendered = json.dumps(self.probe, ensure_ascii=False)

        self.assertIsNone(CREDENTIAL_RE.search(rendered), "credential-shaped value")
        self.assertIsNone(PII_RE.search(rendered), "PII-shaped value")

    def test_budget_is_explicit_and_bounded(self) -> None:
        budget = self.probe["budget"]

        self.assertLessEqual(budget["iguopin_requests"], 40)
        self.assertLessEqual(budget["tsinghua_requests"], 40)


class ProbeVerdictTest(unittest.TestCase):
    def setUp(self) -> None:
        self.probe = load_probe()
        self.by_id = {entry["id"]: entry for entry in self.probe["candidates"]}

    def test_iguopin_is_out_because_the_list_is_signature_gated(self) -> None:
        entry = self.by_id["S2-iguopin"]

        contract = entry["list_contract_found_in_bundle"]
        signature = contract["client_side_signature"]

        self.assertEqual(contract["list"]["url"], "/api/jobs/v3/list")
        self.assertEqual(contract["list"]["method"], "post")
        self.assertIn("/api/jobs/v3", signature["applies_to_prefixes"])
        self.assertIn("Sign", signature["headers"])
        self.assertIn("g(p.MB", signature["secret_reference"])
        self.assertIn("bundled", signature["secret_reference"])
        self.assertTrue(signature["assessment"])

        statuses = {
            (probe["url"], probe["method"]): probe["status"]
            for probe in entry["unsigned_probe_results"]
        }
        self.assertEqual(
            statuses[("https://www.iguopin.com/api/jobs/v3/list", "POST")], 405
        )
        self.assertIn("OUT", entry["verdict"])

    def test_iguopin_robots_allows_paths_but_that_is_not_enough(self) -> None:
        entry = self.by_id["S2-iguopin"]

        self.assertEqual(entry["robots"]["status"], 200)
        self.assertEqual(entry["robots"]["verdict_all_paths"], "allowed")
        # Documented explicitly so a reader cannot mistake robots-allow for a
        # usable contract.
        self.assertIn("OUT", entry["verdict"])

    def test_tsinghua_fails_closed_on_the_job_host(self) -> None:
        entry = self.by_id["S3-tsinghua-career"]
        fallback = entry["fallback_host_probe"]

        self.assertEqual(fallback["host"], "career.cic.tsinghua.edu.cn")
        self.assertEqual(fallback["status"], 503)
        self.assertEqual(fallback["verdict_for_list_path"], "disallowed")
        self.assertFalse(fallback["list_path_read"])
        self.assertIn("robots", fallback["policy"].lower())
        self.assertIn(
            "career.cic.tsinghua.edu.cn",
            entry["job_list_is_on_another_host"]["url"],
        )

    def test_outcome_matches_the_plan_degradation_path(self) -> None:
        self.assertIn("NOT started", self.probe["outcome"])
        self.assertIn("stop condition 2", self.probe["outcome"])


class RegisterConsistencyTest(unittest.TestCase):
    """The README 来源登记表 must agree with the recorded probe."""

    def setUp(self) -> None:
        self.readme = README.read_text(encoding="utf-8")

    def test_readme_carries_the_probe_date_and_both_hosts(self) -> None:
        self.assertIn("2026-10-09", self.readme)
        self.assertIn("www.iguopin.com", self.readme)
        self.assertIn("career.cic.tsinghua.edu.cn", self.readme)

    def test_readme_records_the_signature_finding_for_iguopin(self) -> None:
        self.assertIn("signature", self.readme.lower())
        self.assertIn("OUT", self.readme)

    def test_readme_lists_the_registered_providers(self) -> None:
        # 0018 registered nowcoder after the 2026-10-19 probe; the four-place
        # allowlist test is the drift guard, this assertion records the intent.
        from jobpilot_sources.config import SUPPORTED_PROVIDERS

        self.assertEqual(
            SUPPORTED_PROVIDERS,
            ("greenhouse", "lever", "ashby", "nowcoder"),
        )

        from jobpilot_sources.providers import PROVIDERS

        self.assertEqual(
            sorted(PROVIDERS),
            ["ashby", "greenhouse", "lever", "nowcoder"],
        )

    def test_fixture_is_plain_json_without_binary(self) -> None:
        raw = (FIXTURES / PROBE_FILE).read_bytes()

        self.assertTrue(raw.startswith(b"{"))


if __name__ == "__main__":
    unittest.main()
