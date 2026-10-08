"""Shared test helpers: fixtures, a recording transport, config builders.

Everything here is offline: the recording transport serves canned responses and
raises on an unexpected URL, so a test can never reach the network by accident.
"""

from __future__ import annotations

import copy
import json
from pathlib import Path
from typing import Any, Mapping

import yaml

from jobpilot_ai.client import HttpResponse
from jobpilot_ai.config import ProviderSettings, WorkerConfig

FIXTURES = Path(__file__).resolve().parent / "fixtures"

TOKEN = "super-secret-internal-token"
PROVIDER_KEY = "super-secret-provider-key"
KEY_ENV = "JOBPILOT_TEST_PROVIDER_KEY"
TOKEN_ENV = "JOBPILOT_TEST_TOKEN"


def fixture_path(name: str) -> Path:
    return FIXTURES / name


def load_fixture(name: str) -> Any:
    with fixture_path(name).open(encoding="utf-8") as handle:
        return json.load(handle)


def fixture_response(name: str, status: int = 200) -> HttpResponse:
    return HttpResponse(status, {}, fixture_path(name).read_bytes())


def json_response(
    document: Mapping[str, Any],
    status: int = 200,
    headers: Mapping[str, str] | None = None,
) -> HttpResponse:
    return HttpResponse(
        status,
        dict(headers or {}),
        json.dumps(document).encode("utf-8"),
    )


def raw_response(
    payload: bytes,
    status: int = 200,
    headers: Mapping[str, str] | None = None,
) -> HttpResponse:
    return HttpResponse(status, dict(headers or {}), payload)


class RecordingTransport:
    """Transport stub that serves canned responses and records every call."""

    def __init__(
        self,
        responses: Mapping[str, Any] | None = None,
        default: HttpResponse | None = None,
    ) -> None:
        self.responses: dict[str, Any] = dict(responses or {})
        self.default = default
        self.requests: list[dict[str, Any]] = []

    def __call__(
        self,
        method: str,
        url: str,
        body: bytes | None,
        headers: Mapping[str, str],
        timeout: float,
    ) -> HttpResponse:
        self.requests.append(
            {
                "method": method,
                "url": url,
                "body": body,
                "headers": dict(headers),
                "timeout": timeout,
            }
        )

        for fragment, response in self.responses.items():
            if fragment in url:
                if isinstance(response, list):
                    if not response:
                        raise AssertionError(
                            f"no queued response left for {fragment}"
                        )

                    return response.pop(0)

                return response

        if self.default is not None:
            return self.default

        raise AssertionError(f"unexpected request to {url}")

    def urls(self) -> list[str]:
        return [request["url"] for request in self.requests]

    def methods(self) -> list[str]:
        return [request["method"] for request in self.requests]

    def posts(self) -> list[dict[str, Any]]:
        return [
            request
            for request in self.requests
            if request["method"] == "POST"
        ]

    def post_bodies(self) -> list[Any]:
        bodies: list[Any] = []

        for request in self.posts():
            raw = request["body"]

            if raw is None:
                bodies.append(None)
            else:
                bodies.append(json.loads(raw.decode("utf-8")))

        return bodies

    def last_headers(self) -> Mapping[str, str]:
        return self.requests[-1]["headers"] if self.requests else {}


class RecordingSleep:
    """Sleep stub that records the requested delays instead of waiting."""

    def __init__(self) -> None:
        self.delays: list[float] = []

    def __call__(self, seconds: float) -> None:
        self.delays.append(seconds)


def input_bundle(**overrides: Any) -> dict[str, Any]:
    document = load_fixture("input_bundle.json")

    return _merge(document, overrides)


def _merge(
    document: Mapping[str, Any],
    overrides: Mapping[str, Any],
) -> dict[str, Any]:
    merged = copy.deepcopy(dict(document))

    for key, value in overrides.items():
        if isinstance(value, Mapping) and isinstance(merged.get(key), Mapping):
            merged[key] = _merge(merged[key], value)
        else:
            merged[key] = copy.deepcopy(value)

    return merged


def jobpilot_document(**overrides: Any) -> dict[str, Any]:
    document: dict[str, Any] = {
        "base_url": "http://127.0.0.1:8080",
        "internal_token_env": TOKEN_ENV,
        "worker_id": "test-worker",
        "lease_seconds": 60,
        "interval_seconds": 0.25,
        "max_attempts": 3,
        "task_types": ["job_match"],
        "request_timeout_seconds": 2.0,
        "max_retries": 1,
    }
    document.update(overrides)

    return document


def provider_document(**overrides: Any) -> dict[str, Any]:
    document: dict[str, Any] = {
        "base_url": "https://api.example.invalid/v1",
        "model": "fixture-model",
        "key_env": KEY_ENV,
        "embedding_model": None,
        "timeout_seconds": 5.0,
        "max_retries": 1,
        "allow_private_endpoint": False,
    }
    document.update(overrides)

    return document


def config_document(
    jobpilot: Mapping[str, Any] | None = None,
    provider: Mapping[str, Any] | None = None,
) -> dict[str, Any]:
    document: dict[str, Any] = {
        "jobpilot": jobpilot_document(**(jobpilot or {}))
    }

    if provider is not None:
        document["provider"] = provider_document(**provider)

    return document


def make_provider_settings(**overrides: Any) -> ProviderSettings:
    fields: dict[str, Any] = {
        "base_url": "https://api.example.invalid/v1",
        "model": "fixture-model",
        "key_env": KEY_ENV,
    }
    fields.update(overrides)

    return ProviderSettings(**fields)


def make_worker_config(**overrides: Any) -> WorkerConfig:
    fields: dict[str, Any] = {
        "base_url": "http://127.0.0.1:8080",
        "internal_token_env": TOKEN_ENV,
        "worker_id": "test-worker",
        "lease_seconds": 60,
        "interval_seconds": 0.25,
        "max_attempts": 3,
        "task_types": ("job_match",),
        "request_timeout_seconds": 2.0,
        "max_retries": 1,
        "provider": make_provider_settings(),
    }
    fields.update(overrides)

    return WorkerConfig(**fields)


def write_config(
    directory: Path,
    document: Mapping[str, Any],
) -> Path:
    path = directory / "config.yaml"

    with path.open("w", encoding="utf-8") as handle:
        yaml.safe_dump(dict(document), handle, allow_unicode=True)

    return path
