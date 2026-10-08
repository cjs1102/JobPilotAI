"""Provider client: OpenAI-compatible chat + embeddings, key from env only."""

from __future__ import annotations

import unittest

from jobpilot_ai.client import RetryPolicy, TransportError
from jobpilot_ai.provider import (
    ProviderClient,
    ProviderError,
    ProviderNotConfigured,
    cosine_similarity,
    parse_json_object,
)

from .support import (
    KEY_ENV,
    PROVIDER_KEY,
    RecordingSleep,
    RecordingTransport,
    fixture_response,
    json_response,
    make_provider_settings,
    raw_response,
)

MESSAGES = [{"role": "user", "content": "explain"}]
ENVIRON = {KEY_ENV: PROVIDER_KEY}


def chat_transport() -> RecordingTransport:
    return RecordingTransport(
        {"chat/completions": fixture_response("chat_valid.json")}
    )


def make_client(
    transport: RecordingTransport,
    *,
    environ=None,
    settings=None,
    sleep=None,
    max_retries: int = 1,
) -> ProviderClient:
    return ProviderClient(
        settings or make_provider_settings(),
        transport=transport,
        environ=ENVIRON if environ is None else environ,
        policy=RetryPolicy(max_retries=max_retries, base_delay=0.5),
        sleep=sleep or RecordingSleep(),
        jitter=lambda: 0.0,
    )


class ChatTest(unittest.TestCase):
    def test_chat_returns_the_message_content(self) -> None:
        transport = chat_transport()

        content = make_client(transport).chat(MESSAGES)

        self.assertIn("Kubernetes", content)
        self.assertEqual(
            transport.urls()[0],
            "https://api.example.invalid/v1/chat/completions",
        )
        self.assertEqual(transport.methods(), ["POST"])

    def test_chat_sends_the_key_as_a_bearer_token_and_never_in_the_body(self) -> None:
        transport = chat_transport()

        make_client(transport).chat(MESSAGES)

        self.assertEqual(
            transport.last_headers()["Authorization"],
            f"Bearer {PROVIDER_KEY}",
        )
        self.assertNotIn(PROVIDER_KEY, str(transport.requests[0]["body"]))

    def test_chat_sends_model_and_messages(self) -> None:
        transport = chat_transport()

        make_client(transport).chat(MESSAGES, temperature=0.1, max_tokens=64)

        body = transport.post_bodies()[0]
        self.assertEqual(body["model"], "fixture-model")
        self.assertEqual(body["messages"], MESSAGES)
        self.assertEqual(body["temperature"], 0.1)
        self.assertEqual(body["max_tokens"], 64)

    def test_local_server_without_key_env_sends_no_authorization(self) -> None:
        transport = chat_transport()
        settings = make_provider_settings(key_env=None)

        client = ProviderClient(settings, transport=transport, environ={})

        self.assertTrue(client.configured)
        client.chat(MESSAGES)
        self.assertNotIn("Authorization", transport.last_headers())

    def test_missing_key_is_a_clear_not_configured_error(self) -> None:
        client = make_client(RecordingTransport(), environ={})

        self.assertFalse(client.configured)

        with self.assertRaises(ProviderNotConfigured) as caught:
            client.chat(MESSAGES)

        message = str(caught.exception)
        self.assertIn(KEY_ENV, message)
        self.assertIn("rule baseline", message)
        self.assertNotIn(PROVIDER_KEY, message)
        self.assertFalse(caught.exception.retryable)

    def test_blank_key_is_treated_as_missing(self) -> None:
        client = make_client(RecordingTransport(), environ={KEY_ENV: "   "})

        self.assertFalse(client.configured)

    def test_key_is_never_read_from_the_process_environment(self) -> None:
        # A key exists in the process environment under the configured name, but
        # an explicitly injected (empty) environment must win.
        client = make_client(RecordingTransport(), environ={})

        with self.assertRaises(ProviderNotConfigured):
            client.chat(MESSAGES)

    def test_chat_response_without_content_is_schema_invalid(self) -> None:
        transport = RecordingTransport(
            {"chat/completions": json_response({"choices": [{"message": {}}]})}
        )

        with self.assertRaises(ProviderError) as caught:
            make_client(transport).chat(MESSAGES)

        self.assertEqual(caught.exception.reason, "provider_schema_invalid")
        self.assertTrue(caught.exception.retryable)

    def test_chat_response_without_choices_is_schema_invalid(self) -> None:
        transport = RecordingTransport(
            {"chat/completions": json_response({"choices": []})}
        )

        with self.assertRaises(ProviderError) as caught:
            make_client(transport).chat(MESSAGES)

        self.assertEqual(caught.exception.reason, "provider_schema_invalid")


class ChatJsonTest(unittest.TestCase):
    def test_valid_json_content_is_parsed(self) -> None:
        transport = chat_transport()

        payload = make_client(transport).chat_json(MESSAGES)

        self.assertIn("explanation", payload)
        self.assertIn("suggestions", payload)

    def test_not_json_content_is_retryable_invalid_json(self) -> None:
        transport = RecordingTransport(
            {"chat/completions": fixture_response("chat_not_json.json")}
        )

        with self.assertRaises(ProviderError) as caught:
            make_client(transport).chat_json(MESSAGES)

        self.assertEqual(caught.exception.reason, "provider_invalid_json")
        self.assertTrue(caught.exception.retryable)

    def test_content_of_the_wrong_json_type_is_schema_invalid(self) -> None:
        transport = RecordingTransport(
            {
                "chat/completions": json_response(
                    {"choices": [{"message": {"content": "[1, 2]"}}]}
                )
            }
        )

        with self.assertRaises(ProviderError) as caught:
            make_client(transport).chat_json(MESSAGES)

        self.assertEqual(caught.exception.reason, "provider_schema_invalid")

    def test_code_fence_is_tolerated(self) -> None:
        self.assertEqual(
            parse_json_object('```json\n{"explanation": "ok"}\n```'),
            {"explanation": "ok"},
        )

    def test_parse_json_object_rejects_a_bare_string(self) -> None:
        with self.assertRaises(ProviderError) as caught:
            parse_json_object('"just text"')

        self.assertEqual(caught.exception.reason, "provider_schema_invalid")


class EmbeddingsTest(unittest.TestCase):
    def test_embeddings_are_returned(self) -> None:
        transport = RecordingTransport(
            {"embeddings": fixture_response("embeddings.json")}
        )

        client = make_client(
            transport,
            settings=make_provider_settings(embedding_model="fixture-embed"),
        )

        vectors = client.embed(["job text", "resume text"])

        self.assertEqual(len(vectors), 2)
        self.assertEqual(vectors[0], [1.0, 0.0, 0.0])
        self.assertEqual(
            transport.urls()[0],
            "https://api.example.invalid/v1/embeddings",
        )
        body = transport.post_bodies()[0]
        self.assertEqual(body["model"], "fixture-embed")
        self.assertEqual(body["input"], ["job text", "resume text"])

    def test_embedding_configured_flag(self) -> None:
        client = make_client(RecordingTransport())
        self.assertFalse(client.embedding_configured)

        client = make_client(
            RecordingTransport(),
            settings=make_provider_settings(embedding_model="fixture-embed"),
        )
        self.assertTrue(client.embedding_configured)

    def test_embed_without_a_model_is_a_programming_error(self) -> None:
        with self.assertRaises(ValueError):
            make_client(RecordingTransport()).embed(["text"])

    def test_mismatched_embedding_count_is_schema_invalid(self) -> None:
        transport = RecordingTransport(
            {"embeddings": json_response({"data": [{"embedding": [1.0]}]})}
        )
        client = make_client(
            transport,
            settings=make_provider_settings(embedding_model="fixture-embed"),
        )

        with self.assertRaises(ProviderError) as caught:
            client.embed(["one", "two"])

        self.assertEqual(caught.exception.reason, "provider_schema_invalid")

    def test_non_numeric_vector_is_schema_invalid(self) -> None:
        transport = RecordingTransport(
            {"embeddings": json_response({"data": [{"embedding": ["x"]}]})}
        )
        client = make_client(
            transport,
            settings=make_provider_settings(embedding_model="fixture-embed"),
        )

        with self.assertRaises(ProviderError):
            client.embed(["one"])


class FailureMappingTest(unittest.TestCase):
    def test_timeout_maps_to_provider_timeout(self) -> None:
        class TimeoutTransport:
            def __call__(self, method, url, body, headers, timeout):
                raise TransportError("request failed: timed out", reason="timeout")

        client = make_client(TimeoutTransport())

        with self.assertRaises(ProviderError) as caught:
            client.chat(MESSAGES)

        self.assertEqual(caught.exception.reason, "provider_timeout")
        self.assertTrue(caught.exception.retryable)

    def test_transport_failure_maps_to_provider_unavailable(self) -> None:
        class BrokenTransport:
            def __call__(self, method, url, body, headers, timeout):
                raise TransportError("request failed: connection refused")

        client = make_client(BrokenTransport())

        with self.assertRaises(ProviderError) as caught:
            client.chat(MESSAGES)

        self.assertEqual(caught.exception.reason, "provider_unavailable")

    def test_429_is_retried_then_succeeds(self) -> None:
        sleep = RecordingSleep()
        transport = RecordingTransport(
            {
                "chat/completions": [
                    fixture_response("provider_error_429.json", status=429),
                    fixture_response("chat_valid.json"),
                ]
            }
        )

        content = make_client(transport, sleep=sleep).chat(MESSAGES)

        self.assertIn("Kubernetes", content)
        self.assertEqual(len(transport.requests), 2)
        self.assertEqual(sleep.delays, [0.5])

    def test_429_gives_up_as_rate_limited(self) -> None:
        transport = RecordingTransport(
            default=fixture_response("provider_error_429.json", status=429)
        )

        with self.assertRaises(ProviderError) as caught:
            make_client(transport).chat(MESSAGES)

        self.assertEqual(caught.exception.reason, "provider_rate_limited")
        self.assertTrue(caught.exception.retryable)
        self.assertEqual(len(transport.requests), 2)

    def test_500_gives_up_as_server_error(self) -> None:
        transport = RecordingTransport(
            default=fixture_response("provider_error_500.json", status=500)
        )

        with self.assertRaises(ProviderError) as caught:
            make_client(transport).chat(MESSAGES)

        self.assertEqual(caught.exception.reason, "provider_server_error")
        self.assertTrue(caught.exception.retryable)

    def test_401_is_permanent_and_not_retried(self) -> None:
        transport = RecordingTransport(
            default=fixture_response("provider_error_401.json", status=401)
        )

        with self.assertRaises(ProviderError) as caught:
            make_client(transport).chat(MESSAGES)

        self.assertEqual(caught.exception.reason, "provider_unauthorized")
        self.assertFalse(caught.exception.retryable)
        self.assertEqual(len(transport.requests), 1)

    def test_400_is_permanent(self) -> None:
        transport = RecordingTransport(
            default=json_response({"error": {"message": "bad request"}}, status=400)
        )

        with self.assertRaises(ProviderError) as caught:
            make_client(transport).chat(MESSAGES)

        self.assertEqual(caught.exception.reason, "provider_request_rejected")
        self.assertFalse(caught.exception.retryable)

    def test_invalid_json_body_is_retryable(self) -> None:
        transport = RecordingTransport({"chat/completions": raw_response(b"<html>")})

        with self.assertRaises(ProviderError) as caught:
            make_client(transport).chat(MESSAGES)

        self.assertEqual(caught.exception.reason, "provider_invalid_json")
        self.assertTrue(caught.exception.retryable)

    def test_errors_never_leak_the_key(self) -> None:
        transport = RecordingTransport(
            default=fixture_response("provider_error_401.json", status=401)
        )

        with self.assertRaises(ProviderError) as caught:
            make_client(transport).chat(MESSAGES)

        self.assertNotIn(PROVIDER_KEY, str(caught.exception))


class CosineSimilarityTest(unittest.TestCase):
    def test_identical_vectors_are_one(self) -> None:
        self.assertAlmostEqual(cosine_similarity([1.0, 2.0], [1.0, 2.0]), 1.0)

    def test_orthogonal_vectors_are_zero(self) -> None:
        self.assertAlmostEqual(cosine_similarity([1.0, 0.0], [0.0, 1.0]), 0.0)

    def test_mismatched_sizes_are_rejected(self) -> None:
        with self.assertRaises(ValueError):
            cosine_similarity([1.0], [1.0, 2.0])

    def test_zero_vectors_are_rejected(self) -> None:
        with self.assertRaises(ValueError):
            cosine_similarity([0.0, 0.0], [1.0, 2.0])


if __name__ == "__main__":
    unittest.main()
