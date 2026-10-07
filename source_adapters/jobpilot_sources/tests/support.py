"""Shared helpers: fixture loading, a recording transport, temp configs."""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any, Mapping

import yaml

FIXTURES = Path(__file__).resolve().parent / "fixtures"


def load_fixture(name: str) -> Any:
    with (FIXTURES / name).open(encoding="utf-8") as handle:
        return json.load(handle)


def fixture_response(name: str, status: int = 200) -> tuple[int, bytes]:
    return status, (FIXTURES / name).read_bytes()


def api_response(document: Mapping[str, Any], status: int = 200) -> tuple[int, bytes]:
    return status, json.dumps(document).encode("utf-8")


class RecordingTransport:
    """Transport stub that serves canned responses and records every call.

    An unexpected URL raises instead of silently hitting the network.
    """

    def __init__(
        self,
        responses: Mapping[str, tuple[int, bytes]] | None = None,
        default: tuple[int, bytes] | None = None,
    ) -> None:
        self.responses = dict(responses or {})
        self.default = default
        self.requests: list[dict[str, Any]] = []

    def __call__(
        self,
        method: str,
        url: str,
        body: bytes | None,
        headers: Mapping[str, str],
        timeout: float,
    ) -> tuple[int, bytes]:
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
                return response

        if self.default is not None:
            return self.default

        raise AssertionError(f"unexpected request to {url}")

    def methods(self) -> list[str]:
        return [request["method"] for request in self.requests]

    def posts(self) -> list[dict[str, Any]]:
        return [
            request
            for request in self.requests
            if request["method"] == "POST"
        ]


def write_config(directory: Path, document: Mapping[str, Any]) -> Path:
    path = directory / "config.yaml"

    with path.open("w", encoding="utf-8") as handle:
        yaml.safe_dump(dict(document), handle, allow_unicode=True)

    return path


def base_config(**overrides: Any) -> dict[str, Any]:
    document: dict[str, Any] = {
        "jobpilot": {
            "base_url": "http://127.0.0.1:8080",
            "internal_token_env": "JOBPILOT_TEST_TOKEN",
        },
        "sources": [
            {
                "type": "greenhouse",
                "board": "gitlab",
                "company": "GitLab",
                "enabled": True,
            },
            {
                "type": "lever",
                "board": "leverdemo",
                "company": "Lever Demo",
                "enabled": True,
            },
            {
                "type": "ashby",
                "board": "ramp",
                "company": "Ramp",
                "enabled": True,
            },
        ],
    }

    document.update(overrides)
    return document
