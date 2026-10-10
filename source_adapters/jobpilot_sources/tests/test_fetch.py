"""Offline tests for the fetch layer: SSRF, retries, budgets, page ceiling.

Every test injects the transport, resolver, clock and sleep function, so nothing
here touches the network (ExecPlan 0014 section 5.2).
"""

from __future__ import annotations

import random
import socket
import unittest
import urllib.request
from email.message import Message

from jobpilot_sources.fetch import (
    BudgetExceeded,
    FetchBudget,
    FetchError,
    Fetcher,
    ResponseTooLarge,
    RetryPolicy,
    RobotsDisallowed,
    SsrfError,
    TransportResponse,
    UrllibTransport,
    assert_url_allowed,
    collect_pages,
    complete_from_budget,
    guarded_redirect_handler,
    is_public_address,
    normalize_url,
)
from jobpilot_sources.tests.support import (
    RichRecordingTransport,
    public_resolver,
)

ROBOTS_ALLOW_ALL = b"User-agent: *\nAllow: /\n"


def private_resolver(*_args, **_kwargs):
    return [(2, 1, 6, "", ("10.0.0.5", 80))]


def loopback_resolver(*_args, **_kwargs):
    return [(2, 1, 6, "", ("127.0.0.1", 80))]


def metadata_resolver(*_args, **_kwargs):
    return [(2, 1, 6, "", ("169.254.169.254", 80))]


def mixed_resolver(*_args, **_kwargs):
    return [
        (2, 1, 6, "", ("93.184.216.34", 80)),
        (2, 1, 6, "", ("192.168.1.10", 80)),
    ]


def failing_resolver(*_args, **_kwargs):
    raise socket.gaierror("name or service not known")


class Clock:
    def __init__(self) -> None:
        self.now = 1000.0
        self.slept: list[float] = []

    def __call__(self) -> float:
        return self.now

    def sleep(self, seconds: float) -> None:
        self.slept.append(seconds)
        self.now += seconds


def make_fetcher(
    responses,
    *,
    default=None,
    resolver=public_resolver,
    policy: RetryPolicy | None = None,
    budget: FetchBudget | None = None,
    clock: Clock | None = None,
    min_interval: float = 0.0,
) -> tuple[Fetcher, RichRecordingTransport, Clock]:
    transport = RichRecordingTransport(responses, default=default)
    time_source = clock or Clock()
    fetcher = Fetcher(
        transport=transport,
        retry=policy or RetryPolicy(),
        budget=budget or FetchBudget(),
        resolver=resolver,
        sleep=time_source.sleep,
        clock=time_source,
        random_source=random.Random(7),
        min_interval_seconds=min_interval,
    )

    return fetcher, transport, time_source


class PublicAddressTest(unittest.TestCase):
    def test_refuses_loopback_private_link_local_and_metadata(self) -> None:
        blocked = (
            "127.0.0.1",
            "127.1.2.3",
            "10.1.2.3",
            "172.16.0.1",
            "192.168.1.1",
            "169.254.169.254",  # cloud metadata (link-local)
            "100.100.100.200",  # Alibaba metadata (CGNAT)
            "100.64.0.1",
            "0.0.0.0",
            "224.0.0.1",
            "::1",
            "fc00::1",
            "fe80::1",
            "::ffff:127.0.0.1",
            "not-an-address",
        )

        for address in blocked:
            with self.subTest(address=address):
                self.assertFalse(is_public_address(address))

    def test_allows_public_addresses(self) -> None:
        for address in ("8.8.8.8", "93.184.216.34", "2606:4700::1111"):
            with self.subTest(address=address):
                self.assertTrue(is_public_address(address))


class UrlGuardTest(unittest.TestCase):
    def test_refuses_private_and_mixed_resolutions(self) -> None:
        for resolver in (
            private_resolver,
            loopback_resolver,
            metadata_resolver,
            mixed_resolver,
            failing_resolver,
        ):
            with self.subTest(resolver=resolver.__name__):
                with self.assertRaises(SsrfError):
                    assert_url_allowed(
                        "https://example.com/jobs",
                        resolver=resolver,
                    )

    def test_allows_public_resolution(self) -> None:
        assert_url_allowed(
            "https://example.com/jobs", resolver=public_resolver
        )

    def test_refuses_unsupported_scheme_and_missing_host(self) -> None:
        with self.assertRaises(SsrfError):
            assert_url_allowed("file:///etc/passwd", resolver=public_resolver)

        with self.assertRaises(SsrfError):
            assert_url_allowed("https:///jobs", resolver=public_resolver)

    def test_default_transport_guards_before_connecting(self) -> None:
        transport = UrllibTransport(
            guard=lambda url: assert_url_allowed(url, resolver=private_resolver)
        )

        with self.assertRaises(SsrfError):
            transport("GET", "https://example.com/x", None, {}, 1.0)

    def test_redirect_handler_guards_the_target(self) -> None:
        seen: list[str] = []
        handler = guarded_redirect_handler(
            lambda url: (
                seen.append(url),
                assert_url_allowed(url, resolver=private_resolver),
            )
        )

        with self.assertRaises(SsrfError):
            handler.redirect_request(
                urllib.request.Request("https://example.com/start"),
                None,
                302,
                "Found",
                Message(),
                "http://169.254.169.254/latest/meta-data/",
            )

        self.assertEqual(seen, ["http://169.254.169.254/latest/meta-data/"])

    def test_redirect_handler_allows_a_public_target(self) -> None:
        handler = guarded_redirect_handler(
            lambda url: assert_url_allowed(url, resolver=public_resolver)
        )

        request = handler.redirect_request(
            urllib.request.Request("https://example.com/start"),
            None,
            302,
            "Found",
            Message(),
            "https://example.com/next",
        )

        self.assertIsNotNone(request)
        self.assertEqual(request.full_url, "https://example.com/next")

    def test_host_is_re_resolved_on_every_request(self) -> None:
        calls: list[int] = []

        def flipping_resolver(*_args, **_kwargs):
            calls.append(1)
            address = "93.184.216.34" if len(calls) == 1 else "10.0.0.9"
            return [(2, 1, 6, "", (address, 443))]

        fetcher, transport, _ = make_fetcher(
            {"/robots.txt": (200, {}, ROBOTS_ALLOW_ALL)},
            default=(200, {}, b"ok"),
            resolver=flipping_resolver,
        )

        self.assertEqual(fetcher.get("https://example.com/a").status, 200)
        # The second request re-resolves and refuses the rebound address.
        with self.assertRaises(SsrfError):
            fetcher.get("https://example.com/b")

        self.assertEqual(len(calls), 2)


class NormalizeUrlTest(unittest.TestCase):
    def test_normalizes_for_the_dedup_key(self) -> None:
        self.assertEqual(
            normalize_url(
                "HTTPS://Example.COM:443/jobs/1/"
                "?utm_source=x&b=2&a=1#frag"
            ),
            "https://example.com/jobs/1?a=1&b=2",
        )

    def test_keeps_non_default_port_and_rejects_bad_urls(self) -> None:
        self.assertEqual(
            normalize_url("http://Example.com:8080/jobs"),
            "http://example.com:8080/jobs",
        )

        for bad in ("", "/jobs", "ftp://example.com/x"):
            with self.subTest(url=bad):
                with self.assertRaises(ValueError):
                    normalize_url(bad)


class RetryPolicyTest(unittest.TestCase):
    def test_429_is_retried_then_succeeds(self) -> None:
        calls = {"n": 0}

        def transport_callable(method, url, body, headers, timeout):
            if url.endswith("/robots.txt"):
                return TransportResponse(200, {}, ROBOTS_ALLOW_ALL, url)

            calls["n"] += 1

            if calls["n"] == 1:
                return TransportResponse(429, {"retry-after": "1"}, b"", url)

            return TransportResponse(200, {}, b"[]", url)

        clock = Clock()
        fetcher = Fetcher(
            transport=transport_callable,
            resolver=public_resolver,
            min_interval_seconds=0.0,
            sleep=clock.sleep,
            clock=clock,
            random_source=random.Random(1),
        )

        response = fetcher.get("https://example.com/jobs")

        self.assertEqual(response.status, 200)
        self.assertEqual(calls["n"], 2)
        self.assertEqual(len(clock.slept), 1)
        # Retry-After 1s is honoured, and stays inside the 8s cap.
        self.assertLessEqual(clock.slept[0], 8.0)
        self.assertGreaterEqual(clock.slept[0], 1.0)

    def test_retry_after_is_clamped_to_max_delay(self) -> None:
        calls = {"n": 0}

        def transport_callable(method, url, body, headers, timeout):
            if url.endswith("/robots.txt"):
                return TransportResponse(200, {}, ROBOTS_ALLOW_ALL, url)

            calls["n"] += 1

            if calls["n"] == 1:
                return TransportResponse(
                    429, {"retry-after": "86400"}, b"", url
                )

            return TransportResponse(200, {}, b"[]", url)

        clock = Clock()
        fetcher = Fetcher(
            transport=transport_callable,
            resolver=public_resolver,
            min_interval_seconds=0.0,
            sleep=clock.sleep,
            clock=clock,
            random_source=random.Random(1),
        )

        fetcher.get("https://example.com/jobs")

        self.assertEqual(clock.slept, [8.0])

    def test_server_errors_exhaust_the_retry_budget(self) -> None:
        calls = {"n": 0}

        def transport_callable(method, url, body, headers, timeout):
            if url.endswith("/robots.txt"):
                return TransportResponse(200, {}, ROBOTS_ALLOW_ALL, url)

            calls["n"] += 1
            return TransportResponse(503, {}, b"", url)

        clock = Clock()
        fetcher = Fetcher(
            transport=transport_callable,
            resolver=public_resolver,
            min_interval_seconds=0.0,
            retry=RetryPolicy(retries=2, base_delay_ms=100, max_delay_ms=1000),
            sleep=clock.sleep,
            clock=clock,
            random_source=random.Random(1),
        )

        with self.assertRaises(FetchError):
            fetcher.get("https://example.com/jobs")

        self.assertEqual(calls["n"], 3)
        self.assertEqual(len(clock.slept), 2)
        self.assertTrue(all(0 < delay <= 1.0 for delay in clock.slept))

    def test_non_429_client_errors_are_never_retried(self) -> None:
        calls = {"n": 0}

        def transport_callable(method, url, body, headers, timeout):
            if url.endswith("/robots.txt"):
                return TransportResponse(200, {}, ROBOTS_ALLOW_ALL, url)

            calls["n"] += 1
            return TransportResponse(403, {}, b"", url)

        clock = Clock()
        fetcher = Fetcher(
            transport=transport_callable,
            resolver=public_resolver,
            min_interval_seconds=0.0,
            sleep=clock.sleep,
            clock=clock,
        )

        response = fetcher.get("https://example.com/jobs")

        self.assertEqual(response.status, 403)
        self.assertEqual(calls["n"], 1)
        self.assertEqual(clock.slept, [])

    def test_transport_errors_are_retried(self) -> None:
        calls = {"n": 0}

        def transport_callable(method, url, body, headers, timeout):
            if url.endswith("/robots.txt"):
                return TransportResponse(200, {}, ROBOTS_ALLOW_ALL, url)

            calls["n"] += 1

            if calls["n"] == 1:
                raise FetchError("connection reset")

            return TransportResponse(200, {}, b"ok", url)

        clock = Clock()
        fetcher = Fetcher(
            transport=transport_callable,
            resolver=public_resolver,
            min_interval_seconds=0.0,
            sleep=clock.sleep,
            clock=clock,
            random_source=random.Random(1),
        )

        self.assertEqual(fetcher.get("https://example.com/jobs").status, 200)
        self.assertEqual(calls["n"], 2)
        self.assertEqual(len(clock.slept), 1)

    def test_backoff_grows_exponentially_and_is_capped(self) -> None:
        attempts = {"n": 0}

        def transport_callable(method, url, body, headers, timeout):
            if url.endswith("/robots.txt"):
                return TransportResponse(200, {}, ROBOTS_ALLOW_ALL, url)

            attempts["n"] += 1
            return TransportResponse(503, {}, b"", url)

        clock = Clock()
        fetcher = Fetcher(
            transport=transport_callable,
            resolver=public_resolver,
            min_interval_seconds=0.0,
            retry=RetryPolicy(
                retries=5, base_delay_ms=1000, max_delay_ms=3000
            ),
            sleep=clock.sleep,
            clock=clock,
            random_source=random.Random(3),
        )

        with self.assertRaises(FetchError):
            fetcher.get("https://example.com/jobs")

        delays = clock.slept
        self.assertEqual(len(delays), 5)
        self.assertTrue(all(0 < delay <= 3.0 for delay in delays))
        # 1s, 2s, then capped at 3s for the remaining attempts.
        self.assertLess(delays[0], delays[1])
        self.assertLessEqual(delays[1], 3.0)
        self.assertLessEqual(delays[2], 3.0)


class RequestPolicyTest(unittest.TestCase):
    def test_sends_honest_user_agent_and_timeout(self) -> None:
        fetcher, transport, _ = make_fetcher(
            {"/robots.txt": (200, {}, ROBOTS_ALLOW_ALL)},
            default=(200, {}, b"ok"),
        )

        fetcher.get("https://example.com/jobs")

        page = transport.requests[-1]
        self.assertEqual(page["headers"]["User-Agent"], fetcher.user_agent)
        self.assertIn("jobpilot-source-adapters", page["headers"]["User-Agent"])
        self.assertIn("http", page["headers"]["User-Agent"])
        self.assertEqual(page["timeout"], fetcher.timeout)

    def test_robots_disallow_aborts_without_reading_the_path(self) -> None:
        fetcher, transport, _ = make_fetcher(
            {
                "/robots.txt": (
                    200,
                    {},
                    b"User-agent: *\nDisallow: /jobs/private\n",
                )
            },
            default=(200, {}, b"ok"),
        )

        with self.assertRaises(RobotsDisallowed):
            fetcher.get("https://example.com/jobs/private")

        urls = [request["url"] for request in transport.requests]
        self.assertEqual(urls, ["https://example.com/robots.txt"])

    def test_robots_verdict_applies_to_the_redirect_target(self) -> None:
        """The path actually read is the final URL, so it is checked too."""

        robots = b"User-agent: *\nAllow: /campus/\nDisallow: /private/\n"

        def transport_callable(method, url, body, headers, timeout):
            if url.endswith("/robots.txt"):
                return TransportResponse(200, {}, robots, url)

            return TransportResponse(
                200,
                {},
                b"<html>secret</html>",
                "https://example.com/private/secret",
            )

        fetcher = Fetcher(
            transport=transport_callable,
            resolver=public_resolver,
            min_interval_seconds=0.0,
            sleep=lambda _seconds: None,
            clock=lambda: 0.0,
        )

        with self.assertRaises(RobotsDisallowed):
            fetcher.get("https://example.com/campus/list")

    def test_robots_verdict_allows_a_permitted_redirect_target(self) -> None:
        robots = b"User-agent: *\nAllow: /campus/\nDisallow: /private/\n"

        def transport_callable(method, url, body, headers, timeout):
            if url.endswith("/robots.txt"):
                return TransportResponse(200, {}, robots, url)

            return TransportResponse(
                200,
                {},
                b"<html>job</html>",
                "https://example.com/campus/real-page",
            )

        fetcher = Fetcher(
            transport=transport_callable,
            resolver=public_resolver,
            min_interval_seconds=0.0,
            sleep=lambda _seconds: None,
            clock=lambda: 0.0,
        )

        response = fetcher.get("https://example.com/campus/list")

        self.assertEqual(response.url, "https://example.com/campus/real-page")

    def test_crawl_delay_is_a_request_floor(self) -> None:
        fetcher, _transport, clock = make_fetcher(
            {
                "/robots.txt": (
                    200,
                    {},
                    b"User-agent: *\nAllow: /\nCrawl-delay: 5\n",
                )
            },
            default=(200, {}, b"ok"),
            min_interval=0.2,
        )

        fetcher.get("https://example.com/a")
        fetcher.get("https://example.com/b")

        # The second request waits out the declared 5s floor.
        self.assertEqual(clock.slept, [5.0])

    def test_response_size_cap_is_enforced(self) -> None:
        fetcher, _transport, _clock = make_fetcher(
            {"/robots.txt": (200, {}, ROBOTS_ALLOW_ALL)},
            default=(200, {}, b"x" * 64),
            budget=FetchBudget(max_response_bytes=32),
        )

        with self.assertRaises(ResponseTooLarge):
            fetcher.get("https://example.com/jobs")

    def test_request_budget_is_enforced_and_recorded(self) -> None:
        fetcher, _transport, _clock = make_fetcher(
            {"/robots.txt": (200, {}, ROBOTS_ALLOW_ALL)},
            default=(200, {}, b"ok"),
            budget=FetchBudget(max_requests=2),
        )

        fetcher.get("https://example.com/a")

        with self.assertRaises(BudgetExceeded):
            fetcher.get("https://example.com/b")

        self.assertTrue(fetcher.budget.truncated)
        self.assertIn("request budget", fetcher.budget.truncation_reason or "")


class PageCeilingTest(unittest.TestCase):
    def test_page_ceiling_truncates_and_never_claims_completeness(self) -> None:
        fetcher, _transport, _clock = make_fetcher(
            {"/robots.txt": (200, {}, ROBOTS_ALLOW_ALL)},
            default=(200, {}, b"ok"),
            budget=FetchBudget(max_pages=2),
        )

        pages = [f"https://example.com/jobs?page={n}" for n in range(1, 6)]
        responses, truncated = collect_pages(fetcher, pages)

        self.assertEqual(len(responses), 2)
        self.assertTrue(truncated)
        self.assertTrue(fetcher.budget.truncated)
        self.assertFalse(
            complete_from_budget(fetcher.budget, claimed_complete=True)
        )

    def test_complete_collection_keeps_completeness(self) -> None:
        fetcher, _transport, _clock = make_fetcher(
            {"/robots.txt": (200, {}, ROBOTS_ALLOW_ALL)},
            default=(200, {}, b"ok"),
            budget=FetchBudget(max_pages=20),
        )

        responses, truncated = collect_pages(
            fetcher, ["https://example.com/jobs?page=1"]
        )

        self.assertEqual(len(responses), 1)
        self.assertFalse(truncated)
        self.assertTrue(
            complete_from_budget(fetcher.budget, claimed_complete=True)
        )

    def test_manual_truncation_marker_blocks_completeness(self) -> None:
        budget = FetchBudget()
        budget.mark_truncated("source reported 30 pages")

        self.assertFalse(complete_from_budget(budget, True))


if __name__ == "__main__":
    unittest.main()
