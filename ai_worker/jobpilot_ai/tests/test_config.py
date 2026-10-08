"""Config parsing/validation and the provider base_url guard."""

from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

from jobpilot_ai.config import (
    ConfigError,
    ProviderSettings,
    load_config,
    parse_config,
    resolve_internal_token,
    validate_provider_base_url,
)

from .support import config_document, jobpilot_document, write_config


class ParseJobPilotTest(unittest.TestCase):
    def test_defaults_are_applied(self) -> None:
        config = parse_config(
            {"jobpilot": {"base_url": "http://127.0.0.1:8080/",
                          "internal_token_env": "INTERNAL_API_TOKEN"}}
        )

        self.assertEqual(config.base_url, "http://127.0.0.1:8080")
        self.assertEqual(config.worker_id, "jobpilot-ai-worker")
        self.assertEqual(config.lease_seconds, 120)
        self.assertEqual(config.interval_seconds, 5.0)
        self.assertEqual(config.max_attempts, 3)
        self.assertEqual(config.task_types, ("job_match",))
        self.assertEqual(config.max_retries, 1)
        self.assertIsNone(config.provider)

    def test_explicit_values_are_kept(self) -> None:
        config = parse_config(config_document(provider={}))

        self.assertEqual(config.worker_id, "test-worker")
        self.assertEqual(config.lease_seconds, 60)
        self.assertEqual(config.interval_seconds, 0.25)
        self.assertEqual(config.max_attempts, 3)
        self.assertEqual(config.task_types, ("job_match",))
        self.assertEqual(config.request_timeout_seconds, 2.0)
        self.assertIsInstance(config.provider, ProviderSettings)
        self.assertEqual(config.provider.model, "fixture-model")
        self.assertFalse(config.provider.allow_private_endpoint)

    def test_task_types_must_be_a_non_empty_list(self) -> None:
        for value in ([], "job_match", [""], [3]):
            with self.subTest(value=value):
                with self.assertRaises(ConfigError):
                    parse_config(
                        config_document(jobpilot={"task_types": value})
                    )

    def test_numeric_fields_are_validated(self) -> None:
        bad_values = (
            {"lease_seconds": 0},
            {"lease_seconds": "60"},
            {"lease_seconds": True},
            {"max_attempts": 0},
            {"interval_seconds": -1},
            {"max_retries": -1},
            {"request_timeout_seconds": 0},
        )

        for overrides in bad_values:
            with self.subTest(overrides=overrides):
                with self.assertRaises(ConfigError):
                    parse_config(config_document(jobpilot=overrides))

    def test_unknown_fields_are_rejected(self) -> None:
        with self.assertRaises(ConfigError) as caught:
            parse_config(config_document(jobpilot={"retries": 3}))

        self.assertIn("retries", str(caught.exception))

    def test_unknown_root_fields_are_rejected(self) -> None:
        document = config_document()
        document["worker"] = {}

        with self.assertRaises(ConfigError):
            parse_config(document)

    def test_missing_jobpilot_mapping_is_rejected(self) -> None:
        with self.assertRaises(ConfigError):
            parse_config({"provider": None})

    def test_base_url_must_be_http(self) -> None:
        with self.assertRaises(ConfigError):
            parse_config(
                config_document(jobpilot={"base_url": "ftp://127.0.0.1"})
            )


class ProviderSectionTest(unittest.TestCase):
    def test_provider_is_optional(self) -> None:
        config = parse_config({"jobpilot": jobpilot_document()})

        self.assertIsNone(config.provider)

    def test_key_fields_are_rejected_by_name(self) -> None:
        for field in ("key", "api_key", "token", "secret", "password"):
            with self.subTest(field=field):
                with self.assertRaises(ConfigError) as caught:
                    parse_config(
                        config_document(provider={field: "sk-should-not-be-here"})
                    )

                message = str(caught.exception)
                self.assertIn(field, message)
                self.assertIn("key_env", message)
                self.assertNotIn("sk-should-not-be-here", message)

    def test_unknown_provider_fields_are_rejected(self) -> None:
        with self.assertRaises(ConfigError) as caught:
            parse_config(config_document(provider={"temperature": 0.5}))

        self.assertIn("temperature", str(caught.exception))

    def test_embedding_model_is_optional(self) -> None:
        config = parse_config(
            config_document(provider={"embedding_model": "fixture-embed"})
        )

        self.assertIsNotNone(config.provider)
        self.assertEqual(config.provider.embedding_model, "fixture-embed")

    def test_key_env_may_be_null_for_a_local_server(self) -> None:
        config = parse_config(
            config_document(
                provider={
                    "base_url": "http://127.0.0.1:11434/v1",
                    "key_env": None,
                    "allow_private_endpoint": True,
                }
            )
        )

        self.assertIsNotNone(config.provider)
        self.assertIsNone(config.provider.key_env)

    def test_allow_private_endpoint_must_be_boolean(self) -> None:
        with self.assertRaises(ConfigError):
            parse_config(
                config_document(provider={"allow_private_endpoint": "yes"})
            )


class ProviderBaseUrlGuardTest(unittest.TestCase):
    def test_loopback_private_and_metadata_hosts_are_refused(self) -> None:
        refused = (
            "http://127.0.0.1:11434/v1",
            "http://localhost:11434/v1",
            "http://10.1.2.3/v1",
            "http://172.16.0.9/v1",
            "http://192.168.1.5/v1",
            "http://169.254.169.254/latest/meta-data",
            "http://100.64.1.1/v1",
            "http://0.0.0.0/v1",
            "http://[::1]:11434/v1",
            "http://[fd00::1]/v1",
            "http://metadata.google.internal/v1",
            "http://[::ffff:127.0.0.1]/v1",
        )

        for url in refused:
            with self.subTest(url=url):
                with self.assertRaises(ConfigError) as caught:
                    validate_provider_base_url(url)

                self.assertIn("allow_private_endpoint", str(caught.exception))

    def test_the_guard_applies_through_the_config_loader(self) -> None:
        with self.assertRaises(ConfigError):
            parse_config(
                config_document(provider={"base_url": "http://127.0.0.1:11434/v1"})
            )

    def test_public_hosts_are_accepted(self) -> None:
        accepted = (
            "https://api.deepseek.com/v1",
            "https://api.openai.com/v1/",
            "https://8.8.8.8/v1",
        )

        for url in accepted:
            with self.subTest(url=url):
                self.assertEqual(
                    validate_provider_base_url(url),
                    url.rstrip("/"),
                )

    def test_opt_in_allows_a_local_provider_server(self) -> None:
        self.assertEqual(
            validate_provider_base_url(
                "http://127.0.0.1:11434/v1",
                allow_private_endpoint=True,
            ),
            "http://127.0.0.1:11434/v1",
        )

    def test_embedded_credentials_are_refused(self) -> None:
        with self.assertRaises(ConfigError) as caught:
            validate_provider_base_url(
                "https://user:secret@api.example.com/v1"
            )

        self.assertNotIn("secret", str(caught.exception))
        self.assertIn("key_env", str(caught.exception))

    def test_query_and_fragment_are_refused(self) -> None:
        for url in (
            "https://api.example.com/v1?key=abc",
            "https://api.example.com/v1#fragment",
        ):
            with self.subTest(url=url):
                with self.assertRaises(ConfigError):
                    validate_provider_base_url(url)

    def test_non_http_schemes_are_refused(self) -> None:
        for url in ("ftp://api.example.com/v1", "file:///etc/passwd"):
            with self.subTest(url=url):
                with self.assertRaises(ConfigError):
                    validate_provider_base_url(url)

    def test_host_is_required(self) -> None:
        with self.assertRaises(ConfigError):
            validate_provider_base_url("https:///v1")


class TokenResolutionTest(unittest.TestCase):
    def _config(self):
        return parse_config({"jobpilot": jobpilot_document()})

    def test_token_is_read_from_the_named_environment_variable(self) -> None:
        token = resolve_internal_token(
            self._config(),
            {self._config().internal_token_env: "  abc  "},
        )

        self.assertEqual(token, "abc")

    def test_missing_token_is_a_config_error(self) -> None:
        with self.assertRaises(ConfigError) as caught:
            resolve_internal_token(self._config(), {})

        self.assertIn("JOBPILOT_TEST_TOKEN", str(caught.exception))

    def test_blank_token_is_a_config_error(self) -> None:
        with self.assertRaises(ConfigError):
            resolve_internal_token(
                self._config(),
                {self._config().internal_token_env: "   "},
            )


class LoadConfigTest(unittest.TestCase):
    def setUp(self) -> None:
        self._temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self._temporary.cleanup)
        self.directory = Path(self._temporary.name)

    def test_loads_a_written_config(self) -> None:
        path = write_config(self.directory, config_document())

        config = load_config(path)

        self.assertEqual(config.worker_id, "test-worker")

    def test_missing_file_is_a_config_error(self) -> None:
        with self.assertRaises(ConfigError):
            load_config(self.directory / "absent.yaml")

    def test_invalid_yaml_is_a_config_error(self) -> None:
        path = self.directory / "broken.yaml"
        path.write_text("jobpilot: [unclosed", encoding="utf-8")

        with self.assertRaises(ConfigError):
            load_config(path)

    def test_empty_file_is_a_config_error(self) -> None:
        path = self.directory / "empty.yaml"
        path.write_text("", encoding="utf-8")

        with self.assertRaises(ConfigError):
            load_config(path)


if __name__ == "__main__":
    unittest.main()
