"""Internal API client: verbs, token header, bounded retries, honest errors."""

from __future__ import annotations

import unittest

from jobpilot_ai.client import (
    ApiError,
    InternalApiClient,
    MalformedResponse,
    RetryPolicy,
    RetryableApiError,
    TransportError,
    retry_after_seconds,
)

from .support import (
    TOKEN,
    RecordingSleep,
    RecordingTransport,
    json_response,
    raw_response,
)

TASK = {"id": 1024, "task_type": "job_match", "job_id": 20001, "retry_count": 1}


def make_client(
    transport: RecordingTransport,
    *,
    sleep: RecordingSleep | None = None,
    max_retries: int = 1,
) -> InternalApiClient:
    return InternalApiClient(
        "http://127.0.0.1:8080/",
        TOKEN,
        transport=transport,
        policy=RetryPolicy(max_retries=max_retries, base_delay=0.5),
        sleep=sleep or RecordingSleep(),
        jitter=lambda: 0.0,
    )


class ClaimTest(unittest.TestCase):
    def test_claim_returns_the_task(self) -> None:
        transport = RecordingTransport(
            {"tasks/claim": json_response({"task": TASK})}
        )

        task = make_client(transport).claim(
            "worker-a",
            lease_seconds=60,
            task_types=("job_match",),
        )

        self.assertEqual(task, TASK)
        self.assertEqual(transport.methods(), ["POST"])
        self.assertIn("/internal/ai/tasks/claim", transport.urls()[0])
        body = transport.post_bodies()[0]
        self.assertEqual(body["worker_id"], "worker-a")
        self.assertEqual(body["lease_seconds"], 60)
        self.assertEqual(body["task_types"], ["job_match"])

    def test_claim_sends_the_internal_token_header(self) -> None:
        transport = RecordingTransport({"tasks/claim": json_response({"task": None})})

        make_client(transport).claim("worker-a", lease_seconds=60)

        self.assertEqual(transport.last_headers()["X-Internal-Token"], TOKEN)

    def test_empty_queue_is_none(self) -> None:
        transport = RecordingTransport({"tasks/claim": json_response({"task": None})})

        self.assertIsNone(
            make_client(transport).claim("worker-a", lease_seconds=60)
        )

    def test_task_types_are_omitted_when_not_given(self) -> None:
        transport = RecordingTransport({"tasks/claim": json_response({"task": TASK})})

        make_client(transport).claim("worker-a", lease_seconds=60)

        self.assertNotIn("task_types", transport.post_bodies()[0])

    def test_envelope_style_success_is_unwrapped(self) -> None:
        transport = RecordingTransport(
            {"tasks/claim": json_response({"code": 0, "message": "success",
                                          "data": {"task": TASK}})}
        )

        self.assertEqual(
            make_client(transport).claim("worker-a", lease_seconds=60),
            TASK,
        )

    def test_task_must_be_an_object(self) -> None:
        transport = RecordingTransport(
            {"tasks/claim": json_response({"task": "not-an-object"})}
        )

        with self.assertRaises(MalformedResponse):
            make_client(transport).claim("worker-a", lease_seconds=60)


class InputTest(unittest.TestCase):
    def test_input_is_a_get_with_the_worker_id(self) -> None:
        transport = RecordingTransport(
            {"input": json_response({"task": TASK, "rule_result": {}})}
        )

        payload = make_client(transport).fetch_input(1024, worker_id="worker a/b")

        self.assertEqual(transport.methods(), ["GET"])
        self.assertIn("/internal/ai/tasks/1024/input", transport.urls()[0])
        self.assertIn("worker_id=worker%20a%2Fb", transport.urls()[0])
        self.assertEqual(payload["task"], TASK)

    def test_input_must_be_an_object(self) -> None:
        transport = RecordingTransport({"input": raw_response(b"[]")})

        with self.assertRaises(MalformedResponse):
            make_client(transport).fetch_input(1024, worker_id="worker-a")


class CompleteAndFailTest(unittest.TestCase):
    def test_complete_posts_the_result(self) -> None:
        transport = RecordingTransport(
            {"/complete": json_response({"status": 2})}
        )

        payload = make_client(transport).complete(
            1024,
            worker_id="worker-a",
            result={"score": 82, "ai": {"degraded": False}},
        )

        self.assertEqual(payload, {"status": 2})
        body = transport.post_bodies()[0]
        self.assertEqual(body["worker_id"], "worker-a")
        self.assertEqual(body["result"]["score"], 82)

    def test_fail_posts_retryable_and_error(self) -> None:
        transport = RecordingTransport({"/fail": json_response({"status": 0})})

        make_client(transport).fail(
            1024,
            worker_id="worker-a",
            error_msg="provider failure (provider_timeout)",
            retryable=True,
        )

        body = transport.post_bodies()[0]
        self.assertEqual(body["worker_id"], "worker-a")
        self.assertTrue(body["retryable"])
        self.assertIn("provider_timeout", body["error_msg"])

    def test_task_id_must_be_positive_integer(self) -> None:
        client = make_client(RecordingTransport())

        for bad in (0, -1, "1024", True, None):
            with self.subTest(bad=bad):
                with self.assertRaises(ValueError):
                    client.fetch_input(bad, worker_id="worker-a")


class ErrorAndRetryTest(unittest.TestCase):
    def test_4xx_is_not_retried_and_keeps_the_code(self) -> None:
        transport = RecordingTransport(
            default=raw_response(b'{"code":20001,"message":"unauthorized","data":null}',
                                 status=401)
        )

        with self.assertRaises(ApiError) as caught:
            make_client(transport).claim("worker-a", lease_seconds=60)

        self.assertEqual(caught.exception.status, 401)
        self.assertEqual(caught.exception.code, 20001)
        self.assertIn("HTTP 401", str(caught.exception))
        self.assertEqual(len(transport.requests), 1)

    def test_429_is_retried_then_succeeds(self) -> None:
        sleep = RecordingSleep()
        transport = RecordingTransport(
            {
                "tasks/claim": [
                    raw_response(b'{"code":30001,"message":"slow down"}', status=429),
                    json_response({"task": TASK}),
                ]
            }
        )

        task = make_client(transport, sleep=sleep).claim(
            "worker-a", lease_seconds=60
        )

        self.assertEqual(task, TASK)
        self.assertEqual(len(transport.requests), 2)
        self.assertEqual(sleep.delays, [0.5])

    def test_429_gives_up_after_the_bounded_retries(self) -> None:
        sleep = RecordingSleep()
        transport = RecordingTransport(
            default=raw_response(b'{"code":30001}', status=429)
        )

        with self.assertRaises(RetryableApiError) as caught:
            make_client(transport, sleep=sleep, max_retries=2).claim(
                "worker-a", lease_seconds=60
            )

        self.assertEqual(caught.exception.status, 429)
        self.assertEqual(len(transport.requests), 3)
        self.assertEqual(len(sleep.delays), 2)

    def test_500_is_retried_then_succeeds(self) -> None:
        sleep = RecordingSleep()
        transport = RecordingTransport(
            {
                "tasks/claim": [
                    raw_response(b"", status=500),
                    json_response({"task": TASK}),
                ]
            }
        )

        task = make_client(transport, sleep=sleep).claim(
            "worker-a", lease_seconds=60
        )

        self.assertEqual(task, TASK)
        self.assertEqual(len(transport.requests), 2)

    def test_500_gives_up_as_retryable(self) -> None:
        transport = RecordingTransport(default=raw_response(b"", status=503))

        with self.assertRaises(RetryableApiError):
            make_client(transport).claim("worker-a", lease_seconds=60)

    def test_max_retries_zero_makes_a_single_attempt(self) -> None:
        transport = RecordingTransport(default=raw_response(b"", status=500))

        with self.assertRaises(RetryableApiError):
            make_client(transport, max_retries=0).claim(
                "worker-a", lease_seconds=60
            )

        self.assertEqual(len(transport.requests), 1)

    def test_transport_failure_is_retried_then_reported(self) -> None:
        class BrokenTransport:
            def __init__(self) -> None:
                self.calls = 0

            def __call__(self, method, url, body, headers, timeout):
                self.calls += 1

                raise TransportError("request failed: connection refused")

        transport = BrokenTransport()
        sleep = RecordingSleep()
        client = make_client(transport, sleep=sleep)

        with self.assertRaises(TransportError) as caught:
            client.claim("worker-a", lease_seconds=60)

        self.assertEqual(transport.calls, 2)
        self.assertEqual(len(sleep.delays), 1)
        message = str(caught.exception)
        self.assertIn("POST", message)
        self.assertIn("/internal/ai/tasks/claim", message)
        self.assertIn("connection refused", message)

    def test_retry_after_is_honoured_and_clamped(self) -> None:
        sleep = RecordingSleep()
        transport = RecordingTransport(
            {
                "tasks/claim": [
                    raw_response(
                        b'{"code":30001}',
                        status=429,
                        headers={"Retry-After": "2"},
                    ),
                    json_response({"task": TASK}),
                ]
            }
        )

        make_client(transport, sleep=sleep).claim("worker-a", lease_seconds=60)

        self.assertEqual(sleep.delays, [2.0])

    def test_retry_after_is_clamped_to_the_cap(self) -> None:
        sleep = RecordingSleep()
        transport = RecordingTransport(
            {
                "tasks/claim": [
                    raw_response(
                        b"",
                        status=429,
                        headers={"Retry-After": "600"},
                    ),
                    json_response({"task": TASK}),
                ]
            }
        )

        make_client(transport, sleep=sleep).claim("worker-a", lease_seconds=60)

        self.assertEqual(sleep.delays, [30.0])

    def test_retry_after_http_date_is_parsed(self) -> None:
        delay = retry_after_seconds(
            {"retry-after": "Wed, 21 Oct 2099 07:28:00 GMT"}
        )

        self.assertIsNotNone(delay)
        self.assertLessEqual(delay, 30.0)

    def test_retry_after_junk_is_ignored(self) -> None:
        self.assertIsNone(retry_after_seconds({"Retry-After": "later"}))

    def test_malformed_success_body_is_reported(self) -> None:
        transport = RecordingTransport(
            {"tasks/claim": raw_response(b"<html>not json</html>")}
        )

        with self.assertRaises(MalformedResponse):
            make_client(transport).claim("worker-a", lease_seconds=60)

    def test_client_requires_a_non_empty_token(self) -> None:
        for token in ("", "   ", None):
            with self.subTest(token=token):
                with self.assertRaises(ValueError):
                    InternalApiClient("http://127.0.0.1:8080", token)

    def test_error_messages_never_contain_the_token(self) -> None:
        transport = RecordingTransport(
            default=raw_response(b'{"code":20001,"message":"nope"}', status=401)
        )

        with self.assertRaises(ApiError) as caught:
            make_client(transport).claim("worker-a", lease_seconds=60)

        self.assertNotIn(TOKEN, str(caught.exception))


if __name__ == "__main__":
    unittest.main()
