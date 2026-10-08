"""Golden evaluation set: recorded responses with asserted properties.

Amendment 3 of ExecPlan 0013 asks for more than plumbing tests: recorded
provider responses plus asserted properties - schema validity, no fabricated
facts - and a real degradation path. Every case here runs fully offline.
"""

from __future__ import annotations

import copy
import json
import unittest
from typing import Any

from jobpilot_ai.client import RetryPolicy
from jobpilot_ai.provider import ProviderClient
from jobpilot_ai.worker import Worker

from .support import (
    KEY_ENV,
    PROVIDER_KEY,
    RecordingSleep,
    RecordingTransport,
    fixture_response,
    input_bundle,
    make_provider_settings,
    make_worker_config,
)
from .test_worker import FakeInternalClient, TASK

PROTECTED = ("score_version", "score", "source", "breakdown",
             "matched_skills", "missing_skills")


def run_case(
    chat_fixture: str,
    *,
    retry_count: int = 3,
) -> dict[str, Any]:
    bundle = input_bundle(task={"retry_count": retry_count})
    baseline = copy.deepcopy(bundle["rule_result"])
    client = FakeInternalClient(tasks=[TASK], bundles={1024: bundle})
    transport = RecordingTransport(
        {"chat/completions": fixture_response(chat_fixture)}
    )
    provider = ProviderClient(
        make_provider_settings(),
        transport=transport,
        environ={KEY_ENV: PROVIDER_KEY},
        policy=RetryPolicy(max_retries=0),
        sleep=RecordingSleep(),
        jitter=lambda: 0.0,
    )

    code = Worker(
        make_worker_config(),
        client=client,
        provider=provider,
        sleep=RecordingSleep(),
        log=lambda line: None,
    ).run(once=True)

    if code != 0:
        raise AssertionError(f"golden case {chat_fixture} exited {code}")

    result = client.completions[0]["result"]

    for key in PROTECTED:
        if result[key] != baseline[key]:
            raise AssertionError(
                f"golden case {chat_fixture} changed protected field {key}"
            )

    return result


class GoldenCaseTest(unittest.TestCase):
    def test_valid_recorded_response_is_merged_and_evidence_bound(self) -> None:
        result = run_case("chat_valid.json")

        self.assertIn("explanation", result)
        self.assertEqual(len(result["suggestions"]), 2)
        self.assertEqual(result["ai"], {"degraded": False})
        self.assertIn("C++", result["explanation"])

    def test_recorded_fabrication_is_never_persisted(self) -> None:
        result = run_case("chat_fabricated.json")

        self.assertNotIn("explanation", result)
        self.assertNotIn("suggestions", result)

        serialized = json.dumps(result)

        for fabricated in ("Netflix", "Google", "12 years", "streaming team"):
            self.assertNotIn(fabricated, serialized)

    def test_recorded_score_override_is_rejected_and_degrades(self) -> None:
        result = run_case("chat_extra_field.json")

        self.assertEqual(result["score"], 82)
        self.assertNotIn("explanation", result)
        self.assertEqual(
            result["ai"],
            {"degraded": True, "reason": "provider_schema_invalid"},
        )
        self.assertNotIn("99", json.dumps(result["breakdown"]))

    def test_recorded_non_json_reply_degrades(self) -> None:
        result = run_case("chat_not_json.json")

        self.assertEqual(result["score"], 82)
        self.assertEqual(
            result["ai"],
            {"degraded": True, "reason": "provider_invalid_json"},
        )
        self.assertNotIn("explanation", result)

    def test_a_single_retryable_failure_still_requests_a_retry(self) -> None:
        bundle = input_bundle(task={"retry_count": 1})
        client = FakeInternalClient(tasks=[TASK], bundles={1024: bundle})
        transport = RecordingTransport(
            {"chat/completions": fixture_response("chat_not_json.json")}
        )
        provider = ProviderClient(
            make_provider_settings(),
            transport=transport,
            environ={KEY_ENV: PROVIDER_KEY},
            policy=RetryPolicy(max_retries=0),
            sleep=RecordingSleep(),
            jitter=lambda: 0.0,
        )

        code = Worker(
            make_worker_config(),
            client=client,
            provider=provider,
            sleep=RecordingSleep(),
            log=lambda line: None,
        ).run(once=True)

        self.assertEqual(code, 0)
        self.assertEqual(client.completions, [])
        self.assertEqual(len(client.failures), 1)
        self.assertTrue(client.failures[0]["retryable"])


if __name__ == "__main__":
    unittest.main()
