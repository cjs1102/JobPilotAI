"""Fetch orchestration for public China sources (ExecPlan 0014 section 5.2).

Responsibilities:

* **URL normalization** for the dedup key: conservative only (scheme/host case,
  default ports, tracking parameters, trailing slash). No fuzzy matching.
* **Outbound URL safety (SSRF)**. Every URL -- including every redirect target
  and the final URL of a response -- is checked: the host must resolve to
  addresses that are entirely public. Loopback, private, link-local, CGNAT,
  multicast, reserved and IPv4-mapped IPv6 ranges are refused, as are the cloud
  metadata addresses (which live in the link-local/CGNAT ranges). The host is
  re-resolved immediately before each request, which is the re-check that
  narrows DNS-rebinding windows to that single resolution.
* **Robots-first, per path.** A disallowed path aborts the source; the caller
  never retries it or falls back to another path.
* **Bounded retries**: only ``429``, ``5xx`` and transport errors, with
  exponential backoff plus jitter and a clamped ``Retry-After``. A non-429 4xx
  and a refused redirect are never retried.
* **Budgets**: per-response size cap, total request budget and a hard page
  ceiling. Reaching the page ceiling marks the collection truncated, which the
  caller must turn into ``complete=false`` (never a partial ``complete=true``).

Tests inject the transport, resolver, clock and sleep function, so the whole
module is exercised offline.
"""

from __future__ import annotations

import ipaddress
import random
import socket
import time
import urllib.error
import urllib.parse
import urllib.request
from dataclasses import dataclass, field
from typing import Any, Callable, Iterable, Mapping, Sequence

from .robots import (
    ALLOWED,
    DEFAULT_AGENT_TOKEN,
    RobotsCache,
    RobotsUnavailable,
)

DEFAULT_USER_AGENT = (
    "jobpilot-source-adapters/0.1 "
    "(+https://github.com/jobpilotai; public job board reader)"
)
DEFAULT_TIMEOUT_SECONDS = 10.0
DEFAULT_MAX_RESPONSE_BYTES = 2 * 1024 * 1024
DEFAULT_MAX_REQUESTS = 40
DEFAULT_MAX_PAGES = 20
DEFAULT_MIN_INTERVAL_SECONDS = 0.2

#: ``*`` and ``$`` stay; everything else that is not a path/query character is
#: percent-encoded leniently by urlsplit/urlunsplit already.
TRACKING_PARAMETER_PREFIXES = ("utm_", "gclid", "fbclid", "spm", "from", "ref")
TRACKING_PARAMETERS = {
    "utm_source",
    "utm_medium",
    "utm_campaign",
    "utm_term",
    "utm_content",
    "gclid",
    "fbclid",
    "spm",
    "scm",
    "share_token",
    "from",
    "ref",
    "referrer",
}


class FetchError(Exception):
    """Transport failure that survived the retry policy."""


class SsrfError(FetchError):
    """An outbound URL was refused by the SSRF guard."""


class ResponseTooLarge(FetchError):
    """A response body exceeded the configured size cap."""


class BudgetExceeded(FetchError):
    """The total request budget was exhausted."""


class RobotsDisallowed(FetchError):
    """robots.txt forbids the requested path (fail closed)."""


@dataclass(frozen=True)
class RetryPolicy:
    """ADR-0011 section 3 defaults."""

    retries: int = 2
    base_delay_ms: int = 500
    max_delay_ms: int = 8000
    jitter_ratio: float = 0.25


@dataclass(frozen=True)
class Response:
    status: int
    headers: dict[str, str]
    body: bytes
    url: str

    @property
    def text_encoding(self) -> str | None:
        return self.headers.get("content-type")


@dataclass
class FetchBudget:
    """Hard limits for one sync."""

    max_requests: int = DEFAULT_MAX_REQUESTS
    max_pages: int = DEFAULT_MAX_PAGES
    max_response_bytes: int = DEFAULT_MAX_RESPONSE_BYTES
    requests: int = 0
    pages: int = 0
    truncated: bool = False
    #: Set when a limit stopped the run, for the run log.
    truncation_reason: str | None = None

    def account_request(self) -> None:
        self.requests += 1

        if self.requests > self.max_requests:
            self.truncated = True
            self.truncation_reason = (
                f"request budget {self.max_requests} exhausted"
            )
            raise BudgetExceeded(self.truncation_reason)

    def account_page(self) -> None:
        self.pages += 1

        if self.pages > self.max_pages:
            self.truncated = True
            self.truncation_reason = (
                f"page ceiling {self.max_pages} reached"
            )
            raise BudgetExceeded(self.truncation_reason)

    def mark_truncated(self, reason: str) -> None:
        self.truncated = True
        self.truncation_reason = reason


def normalize_url(url: str) -> str:
    """Conservative URL normalization for the ``source_url`` dedup key."""

    if not isinstance(url, str) or not url.strip():
        raise ValueError("url must be a non-empty string")

    parts = urllib.parse.urlsplit(url.strip())

    if not parts.scheme or not parts.netloc:
        raise ValueError(f"url is not absolute: {url!r}")

    scheme = parts.scheme.lower()

    if scheme not in {"http", "https"}:
        raise ValueError(f"unsupported url scheme: {scheme!r}")

    host = (parts.hostname or "").lower()
    port = parts.port

    if port is not None and not (
        (scheme == "http" and port == 80)
        or (scheme == "https" and port == 443)
    ):
        netloc = f"{host}:{port}"
    else:
        netloc = host

    path = parts.path or "/"

    if len(path) > 1 and path.endswith("/"):
        path = path.rstrip("/") or "/"

    query_pairs = [
        (key, value)
        for key, value in urllib.parse.parse_qsl(
            parts.query, keep_blank_values=True
        )
        if key.lower() not in TRACKING_PARAMETERS
    ]
    query_pairs.sort()

    return urllib.parse.urlunsplit(
        (
            scheme,
            netloc,
            path,
            urllib.parse.urlencode(query_pairs, doseq=True),
            "",
        )
    )


def _resolved_addresses(
    host: str,
    port: int,
    resolver: Callable[..., Any],
) -> list[str]:
    try:
        infos = resolver(host, port, type=socket.SOCK_STREAM)
    except (socket.gaierror, OSError) as error:
        raise SsrfError(f"cannot resolve {host!r}: {error}") from error

    addresses: list[str] = []

    for info in infos:
        if isinstance(info, tuple) and len(info) >= 5:
            sockaddr = info[4]

            if isinstance(sockaddr, tuple) and sockaddr:
                addresses.append(str(sockaddr[0]))

    if not addresses:
        raise SsrfError(f"cannot resolve {host!r}")

    return addresses


def is_public_address(address: str) -> bool:
    """True only for addresses that are entirely public (SSRF allowlist)."""

    try:
        parsed = ipaddress.ip_address(address)
    except ValueError:
        return False

    if isinstance(parsed, ipaddress.IPv6Address) and parsed.ipv4_mapped:
        parsed = parsed.ipv4_mapped

    return bool(parsed.is_global) and not parsed.is_multicast


def assert_url_allowed(
    url: str,
    *,
    resolver: Callable[..., Any] = socket.getaddrinfo,
) -> None:
    """Refuse the URL unless every resolved address is public.

    Raises :class:`SsrfError` so the caller can log one explicit refusal and
    skip the source instead of retrying it.
    """

    parts = urllib.parse.urlsplit(url)

    if parts.scheme not in {"http", "https"}:
        raise SsrfError(f"unsupported scheme {parts.scheme!r}")

    host = parts.hostname

    if not host:
        raise SsrfError(f"url has no host: {url!r}")

    port = parts.port or (443 if parts.scheme == "https" else 80)
    addresses = _resolved_addresses(host, port, resolver)

    for address in addresses:
        if not is_public_address(address):
            raise SsrfError(
                f"refusing {host!r}: {address} is not a public address"
            )


@dataclass(frozen=True)
class TransportResponse:
    status: int
    headers: Mapping[str, str]
    body: bytes
    url: str


RichTransport = Callable[
    [str, str, "bytes | None", Mapping[str, str], float],
    TransportResponse,
]


def adapt_transport(transport: Any) -> RichTransport:
    """Accept either the 0012 ``Transport`` protocol or a rich transport."""

    def call(
        method: str,
        url: str,
        body: bytes | None,
        headers: Mapping[str, str],
        timeout: float,
    ) -> TransportResponse:
        result = transport(method, url, body, headers, timeout)

        if isinstance(result, TransportResponse):
            return result

        status, payload = result

        return TransportResponse(
            status=status,
            headers={},
            body=payload,
            url=url,
        )

    return call


class _GuardedRedirectHandler(urllib.request.HTTPRedirectHandler):
    """Validates every redirect target before urllib follows it."""

    def __init__(self, guard: Callable[[str], None]) -> None:
        super().__init__()
        self._guard = guard

    def redirect_request(self, req, fp, code, msg, headers, newurl):  # type: ignore[override]
        self._guard(newurl)
        return super().redirect_request(
            req, fp, code, msg, headers, newurl
        )


def guarded_redirect_handler(
    guard: Callable[[str], None],
) -> urllib.request.HTTPRedirectHandler:
    """Public factory so the redirect guard can be tested without a network."""

    return _GuardedRedirectHandler(guard)


class UrllibTransport:
    """Default rich transport: guarded redirects, capped reads, real headers."""

    def __init__(
        self,
        *,
        max_response_bytes: int = DEFAULT_MAX_RESPONSE_BYTES,
        guard: Callable[[str], None] | None = None,
    ) -> None:
        self._max_response_bytes = max_response_bytes
        self._guard = guard or assert_url_allowed

    def __call__(
        self,
        method: str,
        url: str,
        body: bytes | None,
        headers: Mapping[str, str],
        timeout: float,
    ) -> TransportResponse:
        self._guard(url)

        opener = urllib.request.build_opener(
            guarded_redirect_handler(self._guard)
        )
        request = urllib.request.Request(
            url,
            data=body,
            headers=dict(headers),
            method=method,
        )

        try:
            with opener.open(request, timeout=timeout) as response:
                payload = response.read(self._max_response_bytes + 1)
                final_url = response.geturl()

                if len(payload) > self._max_response_bytes:
                    raise ResponseTooLarge(
                        f"{final_url} exceeded "
                        f"{self._max_response_bytes} bytes"
                    )

                self._guard(final_url)

                return TransportResponse(
                    status=response.status,
                    headers={
                        key.lower(): value
                        for key, value in response.headers.items()
                    },
                    body=payload,
                    url=final_url,
                )
        except urllib.error.HTTPError as error:
            payload = error.read(self._max_response_bytes + 1)

            return TransportResponse(
                status=error.code,
                headers={
                    key.lower(): value
                    for key, value in (error.headers or {}).items()
                },
                body=payload,
                url=error.geturl() if hasattr(error, "geturl") else url,
            )
        except urllib.error.URLError as error:
            raise FetchError(f"request failed: {error.reason}") from error


def _retry_after_seconds(headers: Mapping[str, str]) -> float | None:
    value = None

    for key, candidate in headers.items():
        if key.lower() == "retry-after":
            value = candidate
            break

    if value is None:
        return None

    try:
        parsed = float(str(value).strip())
    except (TypeError, ValueError):
        return None

    if parsed < 0:
        return None

    return parsed


@dataclass
class Fetcher:
    """Robots-aware, retrying, budgeted reader for one sync run."""

    transport: Any = None
    user_agent: str = DEFAULT_USER_AGENT
    timeout: float = DEFAULT_TIMEOUT_SECONDS
    retry: RetryPolicy = field(default_factory=RetryPolicy)
    budget: FetchBudget = field(default_factory=FetchBudget)
    min_interval_seconds: float = DEFAULT_MIN_INTERVAL_SECONDS
    resolver: Callable[..., Any] = socket.getaddrinfo
    sleep: Callable[[float], None] = time.sleep
    clock: Callable[[], float] = time.monotonic
    random_source: random.Random = field(default_factory=random.Random)

    def __post_init__(self) -> None:
        if self.transport is None:
            self.transport = UrllibTransport(
                max_response_bytes=self.budget.max_response_bytes,
                guard=lambda url: assert_url_allowed(
                    url, resolver=self.resolver
                ),
            )

        self._last_request_at: dict[str, float] = {}
        self._robots = RobotsCache(
            self._fetch_robots,
            user_agent=self.user_agent,
            clock=self.clock,
        )
        self._transport = adapt_transport(self.transport)

    # -- public API -----------------------------------------------------

    @property
    def robots(self) -> RobotsCache:
        return self._robots

    def get(self, url: str, *, check_robots: bool = True) -> Response:
        """Fetch ``url`` once (with retries) and return the response."""

        assert_url_allowed(url, resolver=self.resolver)

        if check_robots:
            verdict = self._robots.verdict_for(url)

            if verdict != ALLOWED:
                raise RobotsDisallowed(
                    f"robots.txt disallows {url}"
                )

        policy = self.retry
        attempts = policy.retries + 1
        last_error: Exception | None = None

        for attempt in range(attempts):
            self.budget.account_request()
            self._respect_interval(url)

            delay: float | None = None

            try:
                response = self._transport(
                    "GET",
                    url,
                    None,
                    {
                        "Accept": "text/html,application/json,"
                        "application/xml,text/xml;q=0.9,*/*;q=0.8",
                        "User-Agent": self.user_agent,
                    },
                    self.timeout,
                )
            except ResponseTooLarge:
                raise
            except SsrfError:
                raise
            except FetchError as error:
                last_error = error
                delay = self._backoff_delay(attempt)
            else:
                if len(response.body) > self.budget.max_response_bytes:
                    raise ResponseTooLarge(
                        f"{url} exceeded "
                        f"{self.budget.max_response_bytes} bytes"
                    )

                if 200 <= response.status < 300:
                    final_url = response.url or url

                    if check_robots and final_url != url:
                        # The path that was actually read is the final URL, so
                        # it gets its own per-path verdict: a redirect into a
                        # disallowed path aborts the read (no retry).
                        if self._robots.verdict_for(final_url) != ALLOWED:
                            raise RobotsDisallowed(
                                f"robots.txt disallows redirect target "
                                f"{final_url}"
                            )

                    return Response(
                        status=response.status,
                        headers=dict(response.headers),
                        body=response.body,
                        url=final_url,
                    )

                if response.status == 429 or response.status >= 500:
                    last_error = FetchError(
                        f"{url} returned HTTP {response.status}"
                    )

                    delay = self._backoff_delay(attempt)
                    retry_after = _retry_after_seconds(response.headers)

                    if retry_after is not None:
                        # Retry-After is honoured but clamped: a hostile or
                        # broken value must not stall the run.
                        delay = min(
                            max(retry_after, delay),
                            policy.max_delay_ms / 1000.0,
                        )
                else:
                    # Any other status (non-429 4xx, a redirect the transport
                    # handed back) is final: never retried.
                    return Response(
                        status=response.status,
                        headers=dict(response.headers),
                        body=response.body,
                        url=response.url or url,
                    )

            if attempt + 1 < attempts and delay is not None:
                self.sleep(delay)

        raise FetchError(
            f"{url} failed after {attempts} attempt(s): {last_error}"
        )

    def get_text(
        self,
        url: str,
        *,
        check_robots: bool = True,
    ) -> tuple[str, Response]:
        """Fetch and decode ``url`` using the explicit charset precedence."""

        from .decoding import decode_body

        response = self.get(url, check_robots=check_robots)

        return (
            decode_body(
                response.body,
                content_type=response.headers.get("content-type"),
            ),
            response,
        )

    # -- internals ------------------------------------------------------

    def _fetch_robots(self, url: str) -> tuple[int, bytes]:
        """robots.txt loader used by the cache (bypasses the robots check)."""

        self.budget.account_request()

        try:
            response = self._transport(
                "GET",
                url,
                None,
                {
                    "Accept": "text/plain,*/*;q=0.8",
                    "User-Agent": self.user_agent,
                },
                self.timeout,
            )
        except FetchError as error:
            raise RobotsUnavailable(str(error)) from error

        return response.status, response.body

    def _respect_interval(self, url: str) -> None:
        origin = _origin(url)
        declared = self._robots.crawl_delay(url)
        interval = self.min_interval_seconds

        if declared is not None:
            interval = max(interval, declared)

        now = self.clock()
        previous = self._last_request_at.get(origin)

        if previous is not None:
            elapsed = now - previous
            remaining = interval - elapsed

            if remaining > 0:
                self.sleep(remaining)

        self._last_request_at[origin] = self.clock()

    def _backoff_delay(self, attempt: int) -> float:
        policy = self.retry
        maximum = policy.max_delay_ms / 1000.0
        base = policy.base_delay_ms / 1000.0 * (2**attempt)
        capped = min(base, maximum)
        jitter = capped * policy.jitter_ratio

        if jitter <= 0:
            return capped

        jittered = capped - jitter + self.random_source.random() * 2 * jitter

        # The cap is a hard bound, so the jittered value is clamped too.
        return min(max(jittered, 0.0), maximum)


def _decode(response: Response) -> str:
    from .decoding import decode_body

    return decode_body(
        response.body,
        content_type=response.headers.get("content-type"),
    )


def _origin(url: str) -> str:
    parts = urllib.parse.urlsplit(url)
    return f"{parts.scheme}://{parts.netloc}".lower()


def collect_pages(
    fetcher: Fetcher,
    urls: Iterable[str],
) -> tuple[list[Response], bool]:
    """Fetch list pages until the page ceiling stops the walk.

    Returns ``(responses, truncated)``. ``truncated=True`` means a hard limit
    stopped the walk, so the caller MUST report ``complete=false``.
    """

    responses: list[Response] = []
    truncated = False

    for url in urls:
        try:
            fetcher.budget.account_page()
        except BudgetExceeded:
            truncated = True
            break

        responses.append(fetcher.get(url))

    return responses, truncated


def detect_truncation(budget: FetchBudget) -> bool:
    """True when any hard limit was reached during the run."""

    return budget.truncated


def complete_from_budget(budget: FetchBudget, claimed_complete: bool) -> bool:
    """A truncated run can never claim completeness."""

    return bool(claimed_complete) and not budget.truncated


def summarize_requests(responses: Sequence[Response]) -> dict[str, int]:
    return {
        "responses": len(responses),
        "bytes": sum(len(response.body) for response in responses),
    }


__all__ = [
    "BudgetExceeded",
    "FetchBudget",
    "FetchError",
    "Fetcher",
    "Response",
    "ResponseTooLarge",
    "RetryPolicy",
    "RobotsDisallowed",
    "SsrfError",
    "TransportResponse",
    "UrllibTransport",
    "adapt_transport",
    "assert_url_allowed",
    "collect_pages",
    "complete_from_budget",
    "detect_truncation",
    "is_public_address",
    "normalize_url",
    "summarize_requests",
]
