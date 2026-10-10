"""Tests for the local capture publisher.

Covers normalization into the documented import payload, request construction,
every failure path (missing token, network error, HTTP error, inconsistent
response), duplicate handling, and the Python-side F14 invariants. No test
opens a socket: the transport is always injected.
"""

from __future__ import annotations

import json
import unittest
from typing import Any, Mapping

from jobpilot_sources.capture import (
    IMPORT_PATH,
    MANUAL_SOURCE,
    CaptureError,
    CapturedJob,
    ManualPublisher,
    PublishError,
    build_import_payload,
    build_import_payloads,
    parse_artifact,
    parse_captures,
    publish_captures,
    resolve_publish_token,
)
from jobpilot_sources.client import HttpError
from jobpilot_sources.config import ConfigError
from jobpilot_sources.registry import PlatformNameRegistry
from jobpilot_sources.tests.support import load_fixture

FRESHNESS_KEYS = (
    "last_seen_at",
    "last_verified_at",
    "source_status",
    "missing_count",
    "closed_at",
    "source_scope",
)


class RecordingTransport:
    def __init__(
        self,
        status: int = 200,
        body: bytes = b'{"created":1,"updated":0,"total":1}',
        error: Exception | None = None,
    ) -> None:
        self.status = status
        self.body = body
        self.error = error
        self.calls: list[dict[str, Any]] = []

    def __call__(
        self,
        method: str,
        url: str,
        body: bytes | None,
        headers: Mapping[str, str],
        timeout: float,
    ) -> tuple[int, bytes]:
        self.calls.append(
            {
                "method": method,
                "url": url,
                "body": body,
                "headers": dict(headers),
                "timeout": timeout,
            }
        )

        if self.error is not None:
            raise self.error

        return self.status, self.body


def publisher(transport: RecordingTransport, **overrides: Any) -> ManualPublisher:
    arguments: dict[str, Any] = {
        "base_url": "http://127.0.0.1:8080",
        "token": "test-token",
        "transport": transport,
    }
    arguments.update(overrides)

    return ManualPublisher(**arguments)


class PayloadNormalizationTest(unittest.TestCase):
    def setUp(self) -> None:
        self.capture = parse_artifact(load_fixture("capture_complete.json"))
        self.built = build_import_payload(self.capture)
        self.payload = self.built.payload

    def test_maps_every_0014_field(self) -> None:
        self.assertEqual(self.payload["source"], MANUAL_SOURCE)
        self.assertEqual(
            self.payload["source_url"],
            "https://jobs.example-cn.invalid/campus/1001",
        )
        self.assertEqual(self.payload["title"], "C++ 后端开发工程师")
        self.assertEqual(self.payload["company"], "示例科技有限公司")
        self.assertEqual(self.payload["location"], "深圳-南山区")
        self.assertEqual(self.payload["salary_min"], 15)
        self.assertEqual(self.payload["salary_max"], 25)
        self.assertEqual(self.payload["salary_currency"], "CNY")
        self.assertEqual(self.payload["education_required"], 3)
        self.assertEqual(self.payload["job_nature"], 1)
        self.assertEqual(self.payload["experience_years_min"], 0)
        self.assertNotIn("experience_years_max", self.payload)
        # F13: a date-only deadline stores Beijing noon in UTC.
        self.assertEqual(self.payload["deadline"], "2026-10-31 04:00:00")
        self.assertTrue(self.payload["deadline_date_only"])
        self.assertEqual(self.payload["published_at"], "2026-09-20 00:00:00")

    def test_uses_the_same_url_normalization_as_the_adapters(self) -> None:
        from jobpilot_sources.fetch import normalize_url

        self.assertEqual(
            self.payload["source_url"],
            normalize_url(self.capture.source_url),
        )

    def test_never_emits_freshness_or_lifecycle_keys(self) -> None:
        for key in FRESHNESS_KEYS:
            self.assertNotIn(key, self.payload)

    def test_skills_are_not_published(self) -> None:
        self.assertNotIn("requirements", self.payload)
        self.assertTrue(
            any("skills were captured" in note for note in self.built.notes)
        )

    def test_a_capture_cannot_express_the_source_sync_endpoint(self) -> None:
        self.assertEqual(IMPORT_PATH, "/internal/jobs/import")
        self.assertEqual(self.payload["source"], "manual")

    def test_optional_values_are_omitted_not_invented(self) -> None:
        capture = parse_artifact(load_fixture("capture_minimal.json"))
        payload = build_import_payload(capture).payload

        self.assertEqual(
            set(payload),
            {"title", "company", "source", "source_url", "description"},
        )

    def test_deadline_kinds_round_trip(self) -> None:
        date_only = build_import_payload(
            parse_artifact(
                {
                    "title": "T",
                    "company": "C",
                    "source_url": "https://x/1",
                    "deadline": "2026-10-31",
                }
            )
        ).payload
        explicit = build_import_payload(
            parse_artifact(
                {
                    "title": "T",
                    "company": "C",
                    "source_url": "https://x/2",
                    "deadline": "2026-10-31 18:30:00",
                }
            )
        ).payload

        self.assertEqual(date_only["deadline"], "2026-10-31 04:00:00")
        self.assertTrue(date_only["deadline_date_only"])
        self.assertEqual(explicit["deadline"], "2026-10-31 10:30:00")
        self.assertFalse(explicit["deadline_date_only"])

    def test_ambiguous_values_produce_notes_not_guesses(self) -> None:
        built = build_import_payload(
            parse_artifact(
                {
                    "title": "T",
                    "company": "C",
                    "source_url": "https://x/1",
                    "salary": "面议",
                    "education": "MBA",
                    "job_nature": "unknown",
                    "experience": "资深",
                    "deadline": "长期有效",
                }
            )
        )

        for key in (
            "salary_min",
            "education_required",
            "job_nature",
            "deadline",
            "experience_years_min",
        ):
            self.assertNotIn(key, built.payload)

        self.assertEqual(len(built.notes), 5)

    def test_over_long_optional_values_are_omitted_with_a_note(self) -> None:
        # Second line of defence: the parser bounds the raw fields, but
        # normalization can grow a value (e.g. joining city-district), so the
        # payload builder re-checks and omits with a note instead of truncating.
        built = build_import_payload(
            CapturedJob(
                source_url="https://x/1",
                title="T",
                company="C",
                location="x" * 65,
            )
        )

        self.assertNotIn("location", built.payload)
        self.assertTrue(
            any("location was omitted" in note for note in built.notes)
        )

    def test_location_length_is_bounded_by_the_parser(self) -> None:
        with self.assertRaises(CaptureError):
            parse_artifact(
                {
                    "title": "T",
                    "company": "C",
                    "source_url": "https://x/1",
                    "location": "x" * 65,
                }
            )


class DuplicateTest(unittest.TestCase):
    def test_fragment_collision_is_caught_after_normalization(self) -> None:
        captures = parse_captures(load_fixture("capture_duplicate_urls.json"))

        with self.assertRaises(CaptureError) as context:
            build_import_payloads(captures)

        self.assertIn("same source_url", str(context.exception))

    def test_distinct_urls_are_kept(self) -> None:
        captures = parse_captures(
            [
                {"title": "A", "company": "C", "source_url": "https://x/1"},
                {"title": "B", "company": "C", "source_url": "https://x/2"},
            ]
        )

        payloads, _notes = build_import_payloads(captures)

        self.assertEqual(len(payloads), 2)
        self.assertNotEqual(
            payloads[0]["source_url"], payloads[1]["source_url"]
        )

    def test_re_importing_the_same_capture_is_the_same_idempotency_key(self) -> None:
        capture = parse_artifact(load_fixture("capture_minimal.json"))

        first = build_import_payload(capture).payload
        second = build_import_payload(capture).payload

        self.assertEqual(first["source_url"], second["source_url"])
        self.assertEqual(first, second)

    def test_batch_ceiling_matches_the_api_limit(self) -> None:
        captures = parse_captures(
            [
                {"title": "T", "company": "C", "source_url": f"https://x/{i}"}
                for i in range(100)
            ]
        )

        payloads, _notes = build_import_payloads(captures)

        self.assertEqual(len(payloads), 100)


class DescriptionCapTest(unittest.TestCase):
    """0015A 5.5.3: 48 KiB in bytes, rejected instead of omitted/truncated."""

    def test_an_accepted_description_is_published_verbatim(self) -> None:
        value = "a" * 49152
        built = build_import_payload(
            CapturedJob(
                source_url="https://x/1",
                title="T",
                company="C",
                description=value,
            )
        )

        self.assertEqual(built.payload["description"], value)
        self.assertFalse(
            any("description" in note and "omitted" in note
                for note in built.notes)
        )

    def test_an_over_cap_description_is_never_omitted_or_truncated(self) -> None:
        value = "a" * 49153
        built = build_import_payload(
            CapturedJob(
                source_url="https://x/1",
                title="T",
                company="C",
                description=value,
            )
        )

        # Line 1 of defence: the payload keeps it verbatim.
        self.assertEqual(built.payload["description"], value)

        # Line 2: publishing refuses it with the byte counts, before any request.
        transport = RecordingTransport()

        with self.assertRaises(PublishError) as context:
            publisher(transport).publish([built.payload])

        message = str(context.exception)

        self.assertIn("49153", message)
        self.assertIn("49152", message)
        self.assertIn("never truncated", message)
        self.assertEqual(transport.calls, [])

    def test_publish_captures_refuses_an_item_the_engine_would_reject(self) -> None:
        capture = CapturedJob(source_url="https://x/1", title="T", company="")
        transport = RecordingTransport()

        with self.assertRaises(CaptureError) as context:
            publish_captures(
                [capture],
                base_url="http://127.0.0.1:8080",
                token="test-token",
                transport=transport,
                registry=PlatformNameRegistry.from_names(["牛客"]),
            )

        self.assertEqual(context.exception.code, "company_missing")
        self.assertEqual(context.exception.field, "company")
        self.assertEqual(transport.calls, [])

    def test_publish_captures_still_publishes_a_needs_review_item(self) -> None:
        # A homepage-looking URL is storable (hidden + queued), not rejected.
        capture = CapturedJob(
            source_url="https://www.example.invalid/",
            title="T",
            company="C",
        )
        transport = RecordingTransport()

        outcome = publish_captures(
            [capture],
            base_url="http://127.0.0.1:8080",
            token="test-token",
            transport=transport,
            registry=PlatformNameRegistry.from_names(["牛客"]),
        )

        self.assertTrue(outcome.ok)
        self.assertEqual(len(transport.calls), 1)


class RequestConstructionTest(unittest.TestCase):
    def test_posts_to_the_existing_import_endpoint(self) -> None:
        transport = RecordingTransport()
        capture = parse_capture()

        outcome = publisher(transport).publish(
            build_import_payloads([capture])[0]
        )

        self.assertTrue(outcome.ok)
        self.assertEqual(outcome.created, 1)
        self.assertEqual(len(transport.calls), 1)

        call = transport.calls[0]
        self.assertEqual(call["method"], "POST")
        self.assertEqual(call["url"], "http://127.0.0.1:8080/internal/jobs/import")
        self.assertEqual(call["headers"]["Content-Type"], "application/json")
        self.assertIn("User-Agent", call["headers"])
        self.assertEqual(call["timeout"], publisher(transport).timeout)

        document = json.loads(call["body"].decode("utf-8"))
        self.assertEqual(len(document["jobs"]), 1)
        self.assertEqual(document["jobs"][0]["source"], "manual")

    def test_token_is_sent_only_in_the_header(self) -> None:
        transport = RecordingTransport()
        payloads, _notes = build_import_payloads([parse_capture()])

        publisher(transport, token="super-secret").publish(payloads)

        call = transport.calls[0]
        self.assertEqual(call["headers"]["X-Internal-Token"], "super-secret")
        self.assertNotIn(b"super-secret", call["body"])
        self.assertNotIn("super-secret", call["url"])

    def test_dry_run_never_calls_the_transport(self) -> None:
        transport = RecordingTransport()
        payloads, notes = build_import_payloads([parse_capture()])

        outcome = publisher(transport).publish(
            payloads, dry_run=True, notes=notes
        )

        self.assertTrue(outcome.ok)
        self.assertTrue(outcome.dry_run)
        self.assertEqual(outcome.submitted, 1)
        self.assertEqual(transport.calls, [])

    def test_empty_batch_is_refused(self) -> None:
        with self.assertRaises(PublishError):
            publisher(RecordingTransport()).publish([])

    def test_missing_token_is_refused_before_any_request(self) -> None:
        transport = RecordingTransport()

        for token in ("", "   "):
            with self.subTest(token=token):
                with self.assertRaises(PublishError):
                    publisher(transport, token=token)

        self.assertEqual(transport.calls, [])

    def test_invalid_base_url_is_refused(self) -> None:
        for base_url in ("", "   ", "127.0.0.1:8080", "ftp://x"):
            with self.subTest(base_url=base_url):
                with self.assertRaises(PublishError):
                    publisher(RecordingTransport(), base_url=base_url)


class FailureTest(unittest.TestCase):
    def payloads(self):
        return build_import_payloads([parse_capture()])[0]

    def test_http_error_status_is_a_failure_with_the_server_message(self) -> None:
        transport = RecordingTransport(
            status=400,
            body=b'{"code":10001,"message":"company is invalid","data":null}',
        )

        with self.assertRaises(PublishError) as context:
            publisher(transport).publish(self.payloads())

        message = str(context.exception)
        self.assertIn("HTTP 400", message)
        self.assertIn("10001", message)
        self.assertIn("company is invalid", message)

    def test_unauthorized_is_a_failure(self) -> None:
        transport = RecordingTransport(
            status=401, body=b'{"code":20001,"message":"unauthorized"}'
        )

        with self.assertRaises(PublishError) as context:
            publisher(transport).publish(self.payloads())

        self.assertIn("HTTP 401", str(context.exception))

    def test_server_error_is_a_failure(self) -> None:
        transport = RecordingTransport(
            status=500, body=b'{"code":10004,"message":"database error"}'
        )

        with self.assertRaises(PublishError):
            publisher(transport).publish(self.payloads())

    def test_network_and_timeout_errors_are_failures(self) -> None:
        for error in (HttpError("connection refused"), TimeoutError("timed out"), OSError("reset")):
            with self.subTest(error=type(error).__name__):
                transport = RecordingTransport(error=error)

                with self.assertRaises(PublishError):
                    publisher(transport).publish(self.payloads())

    def test_unparsable_or_misshapen_response_is_a_failure(self) -> None:
        responses = (
            b"not json",
            b"[]",
            b'{"created":1}',
            b'{"created":"1","updated":0,"total":1}',
            b'{"created":-1,"updated":0,"total":1}',
        )

        for body in responses:
            with self.subTest(body=body):
                transport = RecordingTransport(status=200, body=body)

                with self.assertRaises(PublishError):
                    publisher(transport).publish(self.payloads())

    def test_inconsistent_counters_can_never_look_like_success(self) -> None:
        responses = (
            b'{"created":1,"updated":1,"total":1}',
            b'{"created":0,"updated":0,"total":0}',
            b'{"created":1,"updated":0,"total":9}',
        )

        for body in responses:
            with self.subTest(body=body):
                transport = RecordingTransport(status=200, body=body)

                with self.assertRaises(PublishError):
                    publisher(transport).publish(self.payloads())

    def test_server_error_body_without_json_still_yields_a_diagnosis(self) -> None:
        transport = RecordingTransport(status=502, body=b"<html>bad gateway</html>")

        with self.assertRaises(PublishError) as context:
            publisher(transport).publish(self.payloads())

        self.assertIn("bad gateway", str(context.exception))


class TokenBoundaryTest(unittest.TestCase):
    def test_token_comes_from_the_named_environment_variable(self) -> None:
        self.assertEqual(
            resolve_publish_token(
                "MY_TOKEN", {"MY_TOKEN": "abc"}
            ),
            "abc",
        )

    def test_missing_or_blank_token_env_is_a_config_error(self) -> None:
        for environ in ({}, {"MY_TOKEN": ""}, {"MY_TOKEN": "   "}):
            with self.subTest(environ=environ):
                with self.assertRaises(ConfigError):
                    resolve_publish_token("MY_TOKEN", environ)

    def test_publisher_rejects_a_blank_token_after_resolution(self) -> None:
        with self.assertRaises(PublishError):
            publisher(RecordingTransport(), token="")


def parse_capture():
    return parse_artifact(load_fixture("capture_complete.json"))


if __name__ == "__main__":
    unittest.main()
