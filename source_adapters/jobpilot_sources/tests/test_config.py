"""Configuration loading and validation tests."""

from __future__ import annotations

import tempfile
import unittest
from dataclasses import fields
from pathlib import Path

from jobpilot_sources.config import (
    ConfigError,
    load_config,
    resolve_internal_token,
)

from .support import base_config, write_config


class LoadConfigTest(unittest.TestCase):
    def setUp(self) -> None:
        self._temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self._temporary.cleanup)
        self.directory = Path(self._temporary.name)

    def load(self, document):
        return load_config(write_config(self.directory, document))

    def test_loads_and_normalizes(self) -> None:
        config = self.load(base_config())

        self.assertEqual(config.base_url, "http://127.0.0.1:8080")
        self.assertEqual(config.internal_token_env, "JOBPILOT_TEST_TOKEN")
        self.assertEqual(len(config.sources), 3)
        self.assertEqual(
            [source.type for source in config.sources],
            ["greenhouse", "lever", "ashby"],
        )

    def test_base_url_trailing_slash_is_removed(self) -> None:
        document = base_config()
        document["jobpilot"]["base_url"] = "http://127.0.0.1:8080/"
        self.assertEqual(
            self.load(document).base_url,
            "http://127.0.0.1:8080",
        )

    def test_rejects_duplicate_type_and_board(self) -> None:
        document = base_config()
        document["sources"].append(
            {"type": "greenhouse", "board": "gitlab"}
        )

        with self.assertRaises(ConfigError):
            self.load(document)

    def test_rejects_unsupported_provider(self) -> None:
        document = base_config()
        document["sources"][0]["type"] = "workday"

        with self.assertRaises(ConfigError):
            self.load(document)

    def test_rejects_missing_jobpilot_section(self) -> None:
        with self.assertRaises(ConfigError):
            self.load({"sources": [{"type": "lever", "board": "x"}]})

    def test_rejects_bad_base_url(self) -> None:
        document = base_config()
        document["jobpilot"]["base_url"] = "127.0.0.1:8080"

        with self.assertRaises(ConfigError):
            self.load(document)

    def test_rejects_empty_sources(self) -> None:
        with self.assertRaises(ConfigError):
            self.load(base_config(sources=[]))

    def test_rejects_non_boolean_enabled(self) -> None:
        document = base_config()
        document["sources"][0]["enabled"] = "yes"

        with self.assertRaises(ConfigError):
            self.load(document)

    def test_rejects_missing_config_file(self) -> None:
        with self.assertRaises(ConfigError):
            load_config(self.directory / "absent.yaml")

    def test_rejects_invalid_yaml(self) -> None:
        path = self.directory / "broken.yaml"
        path.write_text("jobpilot: [", encoding="utf-8")

        with self.assertRaises(ConfigError):
            load_config(path)

    def test_enabled_sources_filters_and_selects_provider(self) -> None:
        document = base_config()
        document["sources"][1]["enabled"] = False
        config = self.load(document)

        self.assertEqual(len(config.enabled_sources()), 2)
        self.assertEqual(
            [source.type for source in config.enabled_sources("lever")],
            [],
        )
        self.assertEqual(
            [
                source.type
                for source in config.enabled_sources("ashby")
            ],
            ["ashby"],
        )


class InternalTokenTest(unittest.TestCase):
    def setUp(self) -> None:
        self._temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self._temporary.cleanup)
        self.directory = Path(self._temporary.name)
        self.config = load_config(
            write_config(self.directory, base_config())
        )

    def test_resolves_from_environment(self) -> None:
        token = resolve_internal_token(
            self.config,
            {"JOBPILOT_TEST_TOKEN": "  secret-token  "},
        )

        self.assertEqual(token, "secret-token")

    def test_missing_token_raises(self) -> None:
        with self.assertRaises(ConfigError):
            resolve_internal_token(self.config, {})

    def test_blank_token_raises(self) -> None:
        with self.assertRaises(ConfigError):
            resolve_internal_token(
                self.config,
                {"JOBPILOT_TEST_TOKEN": "   "},
            )

    def test_config_never_stores_the_secret(self) -> None:
        secret = "super-secret-value"

        for field in fields(self.config):
            self.assertNotIn(
                secret,
                str(getattr(self.config, field.name)),
            )


if __name__ == "__main__":
    unittest.main()
