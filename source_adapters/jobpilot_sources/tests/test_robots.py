"""Offline tests for the robots.txt per-path verdict (ExecPlan 0014 section 5.2)."""

from __future__ import annotations

import unittest

from jobpilot_sources.robots import (
    ALLOWED,
    DISALLOWED,
    RobotsCache,
    RobotsUnavailable,
    parse_robots,
    robots_url,
)
from jobpilot_sources.tests.support import text_fixture


class RobotsParsingTest(unittest.TestCase):
    def test_named_group_replaces_the_wildcard_group(self) -> None:
        rules = parse_robots(text_fixture("robots_sample.txt"))

        self.assertTrue(rules.explicit_group)
        self.assertEqual(rules.crawl_delay, 2.0)
        self.assertEqual(rules.verdict("/jobs/1"), ALLOWED)
        self.assertEqual(rules.verdict("/jobs/1/apply"), DISALLOWED)
        self.assertEqual(rules.verdict("/campus-recruitment/x"), ALLOWED)
        # RFC 9309: matching groups are not merged, so the wildcard group's
        # /search rule does not apply once our own group matched.
        self.assertEqual(rules.verdict("/search"), ALLOWED)

        other = parse_robots(
            text_fixture("robots_sample.txt"),
            user_agent="unknown-crawler/1.0",
        )
        self.assertEqual(other.verdict("/search"), DISALLOWED)
        self.assertEqual(other.crawl_delay, 1.0)

    def test_longest_match_wins_and_allow_breaks_ties(self) -> None:
        rules = parse_robots(
            "User-agent: *\n"
            "Disallow: /jobs/\n"
            "Allow: /jobs/public/\n"
            "Disallow: /jobs/public/secret\n"
            "Allow: /jobs/public/secret\n"
        )

        self.assertEqual(rules.verdict("/jobs/1"), DISALLOWED)
        self.assertEqual(rules.verdict("/jobs/public/1"), ALLOWED)
        # Equivalent match (same specificity): Allow wins, RFC 9309 2.2.2.
        self.assertEqual(rules.verdict("/jobs/public/secret"), ALLOWED)

    def test_equal_length_tie_allows_regardless_of_rule_order(self) -> None:
        allow_first = parse_robots(
            "User-agent: *\n"
            "Allow: /jobs/list\n"
            "Disallow: /jobs/list\n"
        )
        disallow_first = parse_robots(
            "User-agent: *\n"
            "Disallow: /jobs/list\n"
            "Allow: /jobs/list\n"
        )

        self.assertEqual(allow_first.verdict("/jobs/list"), ALLOWED)
        self.assertEqual(disallow_first.verdict("/jobs/list"), ALLOWED)

    def test_equivalent_tie_never_weakens_specificity(self) -> None:
        # A longer Disallow still beats a shorter Allow, and a longer Allow still
        # beats a shorter Disallow: only *equal* specificity is permissive.
        rules = parse_robots(
            "User-agent: *\n"
            "Allow: /a\n"
            "Disallow: /a/b/c\n"
            "Disallow: /x\n"
            "Allow: /x/keep\n"
        )

        self.assertEqual(rules.verdict("/a/b/c"), DISALLOWED)
        self.assertEqual(rules.verdict("/x/keep/1"), ALLOWED)
        self.assertEqual(rules.verdict("/x/other"), DISALLOWED)

    def test_two_disallow_rules_still_disallow(self) -> None:
        rules = parse_robots(
            "User-agent: *\n"
            "Disallow: /jobs\n"
            "Disallow: /jobs\n"
        )

        self.assertEqual(rules.verdict("/jobs"), DISALLOWED)

    def test_wildcards_and_end_anchor(self) -> None:
        rules = parse_robots(
            "User-agent: *\n"
            "Disallow: /*.pdf$\n"
            "Disallow: /tmp/*/private\n"
            "Allow: /campus/\n"
        )

        self.assertEqual(rules.verdict("/a/b.pdf"), DISALLOWED)
        # The query string is part of the matched target, so appending one
        # breaks the end anchor.
        self.assertEqual(rules.verdict("/a/b.pdf?x=1"), ALLOWED)
        self.assertEqual(rules.verdict("/tmp/a/private"), DISALLOWED)
        self.assertEqual(rules.verdict("/tmp/a/public"), ALLOWED)
        self.assertEqual(rules.verdict("/campus/1"), ALLOWED)

    def test_blanket_disallow_with_a_narrow_allow(self) -> None:
        rules = parse_robots(
            "User-agent: *\nDisallow: /\nAllow: /campus/\n"
        )

        self.assertEqual(rules.verdict("/jobs/1"), DISALLOWED)
        self.assertEqual(rules.verdict("/campus/1"), ALLOWED)

    def test_empty_disallow_allows_and_empty_rules_allow(self) -> None:
        rules = parse_robots("User-agent: *\nDisallow:\n")

        self.assertEqual(rules.verdict("/anything"), ALLOWED)
        self.assertEqual(parse_robots("").verdict("/anything"), ALLOWED)

    def test_unmatched_user_agent_uses_wildcard(self) -> None:
        rules = parse_robots(
            "User-agent: other-bot\n"
            "Disallow: /\n"
            "\n"
            "User-agent: *\n"
            "Allow: /\n"
        )

        self.assertEqual(rules.verdict("/x"), ALLOWED)

    def test_query_string_participates_in_matching(self) -> None:
        rules = parse_robots(
            "User-agent: *\nDisallow: /jobs?page=\n"
        )

        self.assertEqual(rules.verdict("/jobs?page=2"), DISALLOWED)
        self.assertEqual(rules.verdict("/jobs"), ALLOWED)

    def test_crawl_delay_ignores_garbage(self) -> None:
        rules = parse_robots(
            "User-agent: *\nCrawl-delay: soon\nDisallow: /x\n"
        )

        self.assertIsNone(rules.crawl_delay)

    def test_robots_url_keeps_scheme_and_port(self) -> None:
        self.assertEqual(
            robots_url("https://example.com:8443/jobs?page=1"),
            "https://example.com:8443/robots.txt",
        )


class RobotsCacheTest(unittest.TestCase):
    def test_404_allows_every_path(self) -> None:
        cache = RobotsCache(lambda url: (404, b"not found"))

        self.assertTrue(cache.is_allowed("https://example.com/jobs/1"))

    def test_404_error_page_is_never_parsed_or_decoded(self) -> None:
        # A 404 body is an error page: its encoding is irrelevant and it must
        # not turn into a fail-closed verdict.
        cache = RobotsCache(lambda url: (404, b"<html>\xff\xfe not utf-8"))

        self.assertTrue(cache.is_allowed("https://example.com/jobs/1"))

    def test_server_error_is_fail_closed(self) -> None:
        cache = RobotsCache(lambda url: (503, b"boom"))

        self.assertEqual(
            cache.verdict_for("https://example.com/jobs/1"),
            DISALLOWED,
        )
        self.assertIn("https://example.com", cache.denied_origins)

    def test_transport_error_is_fail_closed(self) -> None:
        def failing(url: str) -> bytes:
            raise OSError("network down")

        cache = RobotsCache(failing)

        self.assertEqual(
            cache.verdict_for("https://example.com/jobs/1"),
            DISALLOWED,
        )

    def test_undecodable_robots_is_fail_closed(self) -> None:
        cache = RobotsCache(lambda url: b"\xff\xfe\x00\x01")

        self.assertEqual(
            cache.verdict_for("https://example.com/jobs/1"),
            DISALLOWED,
        )

    def test_rules_are_cached_per_origin(self) -> None:
        calls: list[str] = []

        def fetcher(url: str) -> bytes:
            calls.append(url)
            return b"User-agent: *\nDisallow: /private\n"

        cache = RobotsCache(fetcher)

        self.assertTrue(cache.is_allowed("https://example.com/jobs/1"))
        self.assertFalse(cache.is_allowed("https://example.com/private/1"))
        self.assertEqual(len(calls), 1)

    def test_direct_loader_raises_for_undecodable_payload(self) -> None:
        cache = RobotsCache(lambda url: b"\xff\xfe")

        with self.assertRaises(RobotsUnavailable):
            cache.rules_for("https://example.com/jobs/1")


if __name__ == "__main__":
    unittest.main()
