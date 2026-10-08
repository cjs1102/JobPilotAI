"""HTTP transport and the internal AI task API client.

Standard library only: the worker must not depend on a database driver or a web
framework (ExecPlan 0013 section 6.5). The transport is injectable, so the test
suite serves recorded fixtures without a network call, mirroring
``source_adapters/jobpilot_sources/client.py``.

Two deliberate differences from the source-adapter client:

- the transport returns ``(status, headers, body)`` because the provider client
  must honour ``Retry-After``;
- retries are bounded and only happen for ``429``, ``5xx`` and transport
  failures (ExecPlan 0013 sections 6.3 and 6.4).

The internal token is only ever sent in the ``X-Internal-Token`` header; it is
never logged and never placed in an exception message.
"""

from __future__ import annotations

import json
import random
import socket
import time
import urllib.error
import urllib.request
from dataclasses import dataclass
from datetime import datetime, timezone
from email.utils import parsedate_to_datetime
from typing import Any, Callable, Mapping, Protocol
from urllib.parse import quote

DEFAULT_USER_AGENT = "jobpilot-ai-worker/0.1"
DEFAULT_TIMEOUT_SECONDS = 15.0
DEFAULT_MAX_RETRIES = 1
DEFAULT_BASE_DELAY_SECONDS = 0.5
DEFAULT_MAX_DELAY_SECONDS = 8.0
DEFAULT_JITTER_RATIO = 0.25
MAX_RETRY_AFTER_SECONDS = 30.0

INTERNAL_CLAIM_PATH = "/internal/ai/tasks/claim"


def is_retryable_status(status: int) -> bool:
    """Only rate limiting and server errors may be retried."""

    return status == 429 or status >= 500


class ClientError(Exception):
    """Base class for failures the worker can report without a traceback."""


class TransportError(ClientError):
    """A transport failure (DNS, refused, reset, timeout). Retryable."""

    def __init__(self, message: str, *, reason: str = "transport_error") -> None:
        super().__init__(message)
        self.reason = reason


@dataclass(frozen=True)
class HttpResponse:
    status: int
    headers: Mapping[str, str]
    body: bytes


class Transport(Protocol):
    def __call__(
        self,
        method: str,
        url: str,
        body: bytes | None,
        headers: Mapping[str, str],
        timeout: float,
    ) -> HttpResponse:
        ...


class UrllibTransport:
    """Default transport built on urllib."""

    def __init__(self, user_agent: str = DEFAULT_USER_AGENT) -> None:
        self._user_agent = user_agent

    def __call__(
        self,
        method: str,
        url: str,
        body: bytes | None,
        headers: Mapping[str, str],
        timeout: float,
    ) -> HttpResponse:
        request_headers = {"User-Agent": self._user_agent}
        request_headers.update(headers)
        request = urllib.request.Request(
            url,
            data=body,
            headers=request_headers,
            method=method,
        )

        try:
            with urllib.request.urlopen(
                request,
                timeout=timeout,
            ) as response:
                return HttpResponse(
                    response.status,
                    dict(response.headers),
                    response.read(),
                )
        except urllib.error.HTTPError as error:
            payload = b""

            try:
                payload = error.read()
            except Exception:  # pragma: no cover - defensive
                payload = b""

            return HttpResponse(
                error.code,
                dict(error.headers or {}),
                payload,
            )
        except urllib.error.URLError as error:
            raise TransportError(
                _transport_message(error),
                reason=_transport_reason(error),
            ) from error
        except (TimeoutError, socket.timeout) as error:
            raise TransportError("request timed out", reason="timeout") from error
        except OSError as error:
            raise TransportError(
                f"transport failed: {error}",
                reason="transport_error",
            ) from error


def _transport_reason(error: BaseException) -> str:
    reason = getattr(error, "reason", None)

    if isinstance(reason, (TimeoutError, socket.timeout)):
        return "timeout"

    if "timed out" in str(reason).casefold():
        return "timeout"

    return "transport_error"


def _transport_message(error: BaseException) -> str:
    return f"request failed: {getattr(error, 'reason', error)}"


@dataclass(frozen=True)
class RetryPolicy:
    """Bounded retries with exponential backoff plus jitter."""

    max_retries: int = DEFAULT_MAX_RETRIES
    base_delay: float = DEFAULT_BASE_DELAY_SECONDS
    max_delay: float = DEFAULT_MAX_DELAY_SECONDS
    jitter_ratio: float = DEFAULT_JITTER_RATIO
    retry_after_cap: float = MAX_RETRY_AFTER_SECONDS

    def total_attempts(self) -> int:
        return self.max_retries + 1


def retry_after_seconds(
    headers: Mapping[str, str],
    *,
    cap: float = MAX_RETRY_AFTER_SECONDS,
) -> float | None:
    """Return the clamped ``Retry-After`` delay, or None when absent/unusable."""

    raw: Any = None

    for key, value in headers.items():
        if str(key).casefold() == "retry-after":
            raw = value
            break

    if raw is None:
        return None

    text = str(raw).strip()

    if not text:
        return None

    try:
        seconds = float(text)
    except ValueError:
        try:
            moment = parsedate_to_datetime(text)
        except (TypeError, ValueError):
            return None

        if moment.tzinfo is None:
            moment = moment.replace(tzinfo=timezone.utc)

        seconds = (moment - datetime.now(timezone.utc)).total_seconds()

    return min(max(seconds, 0.0), cap)


def backoff_delay(
    attempt: int,
    policy: RetryPolicy,
    jitter: Callable[[], float],
) -> float:
    delay = min(policy.base_delay * (2 ** (attempt - 1)), policy.max_delay)
    fraction = jitter()

    if isinstance(fraction, bool) or not isinstance(fraction, (int, float)):
        fraction = 0.0

    bounded = min(max(float(fraction), 0.0), 1.0)

    return delay + delay * policy.jitter_ratio * bounded


def request_with_retries(
    transport: Transport,
    *,
    method: str,
    url: str,
    body: bytes | None = None,
    headers: Mapping[str, str] | None = None,
    timeout: float = DEFAULT_TIMEOUT_SECONDS,
    policy: RetryPolicy | None = None,
    sleep: Callable[[float], None] = time.sleep,
    jitter: Callable[[], float] = random.random,
) -> HttpResponse:
    """Send one request, retrying only 429/5xx/transport failures.

    Returns the final response (possibly the last 429/5xx one) so the caller can
    turn it into a typed error. Raises the last ``TransportError`` when the
    transport keeps failing.
    """

    active = policy or RetryPolicy()

    for attempt in range(1, active.total_attempts() + 1):
        try:
            response = transport(
                method,
                url,
                body,
                dict(headers or {}),
                timeout,
            )
        except TransportError:
            if attempt > active.max_retries:
                raise

            sleep(backoff_delay(attempt, active, jitter))
            continue

        if is_retryable_status(response.status):
            if attempt > active.max_retries:
                return response

            delay = retry_after_seconds(
                response.headers,
                cap=active.retry_after_cap,
            )
            sleep(
                delay
                if delay is not None
                else backoff_delay(attempt, active, jitter)
            )
            continue

        return response

    raise AssertionError("retry loop fell through")  # pragma: no cover


def api_error(url: str, response: HttpResponse) -> "ApiError":
    """Build a typed error from the ``{code,message,data}`` error envelope."""

    code: Any = None
    message: Any = None

    try:
        document = json.loads(response.body.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError):
        document = None

    if isinstance(document, Mapping):
        code = document.get("code")
        message = document.get("message")

    detail = f"HTTP {response.status}"

    if isinstance(code, int) and not isinstance(code, bool):
        detail += f" code {code}"

    if isinstance(message, str) and message.strip():
        detail += f": {message.strip()[:200]}"

    text = f"request to {url} failed with {detail}"
    safe_code = (
        code if isinstance(code, int) and not isinstance(code, bool) else None
    )

    if is_retryable_status(response.status):
        return RetryableApiError(
            text,
            status=response.status,
            code=safe_code,
        )

    return ApiError(
        text,
        status=response.status,
        code=safe_code,
    )


class HttpError(ClientError):
    """A request that got a decision (non-retryable by itself)."""


class ApiError(HttpError):
    def __init__(
        self,
        message: str,
        *,
        status: int | None = None,
        code: int | None = None,
        reason: str = "api_error",
    ) -> None:
        super().__init__(message)
        self.status = status
        self.code = code
        self.reason = reason


class RetryableApiError(ApiError):
    def __init__(
        self,
        message: str,
        *,
        status: int | None = None,
        code: int | None = None,
        reason: str = "api_retryable",
    ) -> None:
        super().__init__(message, status=status, code=code, reason=reason)


class MalformedResponse(HttpError):
    def __init__(self, message: str) -> None:
        super().__init__(message)
        self.reason = "malformed_response"


def decode_json(response: HttpResponse, *, url: str) -> Any:
    if response.status < 200 or response.status >= 300:
        raise api_error(url, response)

    try:
        return json.loads(response.body.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise MalformedResponse(
            f"{url} returned a non-JSON body (HTTP {response.status})"
        ) from error


def _unwrap_envelope(payload: Any) -> Any:
    """Accept both compact bodies and a ``{code,message,data}`` envelope."""

    if (
        isinstance(payload, Mapping)
        and "data" in payload
        and isinstance(payload.get("data"), Mapping)
    ):
        return payload["data"]

    return payload


class InternalApiClient:
    """Client for the ``/internal/ai/tasks/*`` worker API."""

    def __init__(
        self,
        base_url: str,
        token: str,
        *,
        transport: Transport | None = None,
        timeout: float = DEFAULT_TIMEOUT_SECONDS,
        policy: RetryPolicy | None = None,
        sleep: Callable[[float], None] = time.sleep,
        jitter: Callable[[], float] = random.random,
        user_agent: str = DEFAULT_USER_AGENT,
    ) -> None:
        if not isinstance(base_url, str) or not base_url.strip():
            raise ValueError("base_url must be a non-empty string")

        if not isinstance(token, str) or not token.strip():
            raise ValueError("token must be a non-empty string")

        self._base_url = base_url.strip().rstrip("/")
        self._token = token.strip()
        self._transport = transport or UrllibTransport(user_agent=user_agent)
        self._timeout = timeout
        self._policy = policy or RetryPolicy()
        self._sleep = sleep
        self._jitter = jitter
        self._user_agent = user_agent

    @property
    def base_url(self) -> str:
        return self._base_url

    def claim(
        self,
        worker_id: str,
        *,
        lease_seconds: int,
        task_types: tuple[str, ...] | None = None,
    ) -> dict[str, Any] | None:
        document: dict[str, Any] = {
            "worker_id": worker_id,
            "lease_seconds": int(lease_seconds),
        }

        if task_types:
            document["task_types"] = list(task_types)

        payload = _unwrap_envelope(
            self._post(INTERNAL_CLAIM_PATH, document)
        )

        if not isinstance(payload, Mapping):
            raise MalformedResponse("claim response must be a JSON object")

        task = payload.get("task")

        if task is None:
            return None

        if not isinstance(task, Mapping):
            raise MalformedResponse(
                "claim response 'task' must be an object or null"
            )

        return dict(task)

    def fetch_input(
        self,
        task_id: int,
        *,
        worker_id: str,
    ) -> dict[str, Any]:
        path = (
            f"{self._task_path(task_id)}/input"
            f"?worker_id={quote(str(worker_id), safe='')}"
        )
        payload = _unwrap_envelope(self._get(path))

        if not isinstance(payload, Mapping):
            raise MalformedResponse("input response must be a JSON object")

        return dict(payload)

    def complete(
        self,
        task_id: int,
        *,
        worker_id: str,
        result: Mapping[str, Any],
    ) -> Any:
        return self._post(
            f"{self._task_path(task_id)}/complete",
            {"worker_id": worker_id, "result": dict(result)},
        )

    def fail(
        self,
        task_id: int,
        *,
        worker_id: str,
        error_msg: str,
        retryable: bool,
    ) -> Any:
        return self._post(
            f"{self._task_path(task_id)}/fail",
            {
                "worker_id": worker_id,
                "error_msg": str(error_msg),
                "retryable": bool(retryable),
            },
        )

    @staticmethod
    def _task_path(task_id: int) -> str:
        if isinstance(task_id, bool) or not isinstance(task_id, int):
            raise ValueError("task_id must be an integer")

        if task_id <= 0:
            raise ValueError("task_id must be positive")

        return f"/internal/ai/tasks/{task_id}"

    def _headers(self) -> dict[str, str]:
        return {
            "Accept": "application/json",
            "X-Internal-Token": self._token,
            "User-Agent": self._user_agent,
        }

    def _send(self, method: str, path: str, body: bytes | None) -> Any:
        url = self._base_url + path
        headers = self._headers()

        if body is not None:
            headers["Content-Type"] = "application/json"

        try:
            response = request_with_retries(
                self._transport,
                method=method,
                url=url,
                body=body,
                headers=headers,
                timeout=self._timeout,
                policy=self._policy,
                sleep=self._sleep,
                jitter=self._jitter,
            )
        except TransportError as error:
            raise TransportError(
                f"{method} {url} failed: {error}",
                reason=error.reason,
            ) from error

        return decode_json(response, url=url)

    def _get(self, path: str) -> Any:
        return self._send("GET", path, None)

    def _post(self, path: str, document: Mapping[str, Any]) -> Any:
        body = json.dumps(document, ensure_ascii=False).encode("utf-8")

        return self._send("POST", path, body)
