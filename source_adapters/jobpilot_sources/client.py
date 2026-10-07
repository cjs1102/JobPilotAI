"""Minimal HTTP client for ATS providers and the JobPilotAI internal API.

Standard library only: no web framework and no database driver is introduced
(ExecPlan 0012 section 4). The transport is injectable so tests can serve
recorded fixtures without touching the network.
"""

from __future__ import annotations

import json
import urllib.error
import urllib.request
from typing import Any, Mapping, Protocol

DEFAULT_USER_AGENT = "jobpilot-source-adapters/0.1"
DEFAULT_TIMEOUT_SECONDS = 15.0


class HttpError(Exception):
    """Raised for transport failures or unexpected HTTP status codes."""


class Transport(Protocol):
    def __call__(
        self,
        method: str,
        url: str,
        body: bytes | None,
        headers: Mapping[str, str],
        timeout: float,
    ) -> tuple[int, bytes]:
        ...


class UrllibTransport:
    """Default transport built on urllib."""

    def __call__(
        self,
        method: str,
        url: str,
        body: bytes | None,
        headers: Mapping[str, str],
        timeout: float,
    ) -> tuple[int, bytes]:
        request = urllib.request.Request(
            url,
            data=body,
            headers=dict(headers),
            method=method,
        )

        try:
            with urllib.request.urlopen(
                request,
                timeout=timeout,
            ) as response:
                return response.status, response.read()
        except urllib.error.HTTPError as error:
            try:
                payload = error.read()
            except Exception:  # pragma: no cover - defensive
                payload = b""

            return error.code, payload
        except urllib.error.URLError as error:
            raise HttpError(f"request failed: {error.reason}") from error


class HttpClient:
    def __init__(
        self,
        transport: Transport | None = None,
        timeout: float = DEFAULT_TIMEOUT_SECONDS,
        user_agent: str = DEFAULT_USER_AGENT,
    ) -> None:
        self._transport = transport or UrllibTransport()
        self._timeout = timeout
        self._user_agent = user_agent

    def get_json(self, url: str) -> Any:
        headers = {
            "Accept": "application/json",
            "User-Agent": self._user_agent,
        }

        status, payload = self._transport(
            "GET",
            url,
            None,
            headers,
            self._timeout,
        )

        return self._decode(url, status, payload)

    def post_json(
        self,
        url: str,
        document: Mapping[str, Any],
        headers: Mapping[str, str] | None = None,
    ) -> Any:
        body = json.dumps(document).encode("utf-8")
        request_headers = {
            "Accept": "application/json",
            "Content-Type": "application/json",
            "User-Agent": self._user_agent,
        }

        if headers:
            request_headers.update(headers)

        status, payload = self._transport(
            "POST",
            url,
            body,
            request_headers,
            self._timeout,
        )

        return self._decode(url, status, payload)

    @staticmethod
    def _decode(
        url: str,
        status: int,
        payload: bytes,
    ) -> Any:
        if status < 200 or status >= 300:
            raise HttpError(
                f"request to {url} returned HTTP {status}"
            )

        try:
            return json.loads(payload.decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError) as error:
            raise HttpError(
                f"request to {url} returned invalid JSON"
            ) from error
