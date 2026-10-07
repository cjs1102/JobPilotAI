"""Provider parsing and normalization tests (fixture driven)."""

from __future__ import annotations

import unittest

from jobpilot_sources.providers import (
    BoardResult,
    ProviderError,
    normalize_board,
    provider_module,
)
from jobpilot_sources.providers import ashby, greenhouse, lever

from .support import load_fixture


class BoardUrlTest(unittest.TestCase):
    def test_board_urls_point_at_public_apis(self) -> None:
        self.assertIn("boards-api.greenhouse.io", greenhouse.board_url("acme"))
        self.assertIn("/boards/acme/", greenhouse.board_url("acme"))
        self.assertIn("api.lever.co", lever.board_url("acme"))
        self.assertIn("/acme", lever.board_url("acme"))
        self.assertIn("api.ashbyhq.com", ashby.board_url("acme"))
        self.assertIn("/job-board/acme", ashby.board_url("acme"))


class GreenhouseTest(unittest.TestCase):
    def test_parses_complete_board(self) -> None:
        board = greenhouse.parse_board(
            load_fixture("greenhouse_board.json"),
            board="gitlab",
            company="Fallback",
        )

        self.assertTrue(board.complete)
        self.assertEqual(board.total, 2)
        self.assertEqual(board.source, "greenhouse")
        self.assertEqual(len(board.jobs), 2)

        first = board.jobs[0]
        self.assertEqual(first["source"], "greenhouse")
        self.assertEqual(first["title"], "Account Executive - France")
        self.assertEqual(first["company"], "GitLab")
        self.assertEqual(first["location"], "Remote, France")
        self.assertEqual(
            first["source_url"],
            "https://job-boards.greenhouse.io/gitlab/jobs/8860302002",
        )
        self.assertEqual(first["external_id"], "8860302002")
        self.assertEqual(first["published_at"], "2026-10-02 15:31:50")
        self.assertEqual(
            first["description"],
            "Build backend services.\nC++ & Linux",
        )
        self.assertEqual(first["requirements"], [])

        self.assertEqual(
            board.jobs[1]["published_at"],
            "2026-07-22 17:38:40",
        )

    def test_total_mismatch_is_incomplete(self) -> None:
        board = greenhouse.parse_board(
            load_fixture("greenhouse_board_incomplete.json"),
            board="gitlab",
        )

        self.assertFalse(board.complete)
        self.assertEqual(board.total, 7)

    def test_company_falls_back_to_configuration_then_board(self) -> None:
        payload = load_fixture("greenhouse_board.json")
        payload["jobs"][0].pop("company_name")
        payload["jobs"][1].pop("company_name")

        configured = greenhouse.parse_board(
            payload,
            board="gitlab",
            company="Configured Co",
        )
        self.assertEqual(configured.jobs[0]["company"], "Configured Co")

        bare = greenhouse.parse_board(payload, board="gitlab")
        self.assertEqual(bare.jobs[0]["company"], "gitlab")

    def test_rows_without_contract_fields_are_skipped(self) -> None:
        payload = {
            "jobs": [
                {"title": "No URL"},
                {"absolute_url": "https://example.invalid/a"},
                {
                    "title": "Good",
                    "absolute_url": "https://example.invalid/b",
                },
            ],
            "meta": {"total": 3},
        }

        board = greenhouse.parse_board(payload, board="acme")

        self.assertEqual(len(board.jobs), 1)
        self.assertEqual(board.jobs[0]["title"], "Good")

    def test_rejects_unexpected_payload(self) -> None:
        with self.assertRaises(ProviderError):
            greenhouse.parse_board([], board="acme")

        with self.assertRaises(ProviderError):
            greenhouse.parse_board({"meta": {}}, board="acme")


class LeverTest(unittest.TestCase):
    def test_parses_complete_board(self) -> None:
        board = lever.parse_board(
            load_fixture("lever_postings.json"),
            board="leverdemo",
            company="Lever Demo",
        )

        self.assertTrue(board.complete)
        self.assertEqual(board.total, 2)
        self.assertEqual(len(board.jobs), 2)

        first = board.jobs[0]
        self.assertEqual(first["source"], "lever")
        self.assertEqual(first["title"], "Customer Success Manager")
        self.assertEqual(first["company"], "Lever Demo")
        self.assertEqual(first["location"], "Atlanta, Georgia")
        self.assertEqual(
            first["source_url"],
            "https://jobs.lever.co/leverdemo/"
            "ad208490-4052-4f91-a57e-433f2d1e484b",
        )
        self.assertEqual(
            first["external_id"],
            "ad208490-4052-4f91-a57e-433f2d1e484b",
        )
        self.assertEqual(first["published_at"], "2026-08-31 18:12:19")
        self.assertEqual(
            first["description"],
            "Own the customer relationship.\n\nWork with engineering.",
        )
        self.assertEqual(first["requirements"], [])

    def test_salary_is_not_converted_and_not_fabricated(self) -> None:
        board = lever.parse_board(
            load_fixture("lever_postings.json"),
            board="leverdemo",
        )

        for job in board.jobs:
            self.assertNotIn("salary_min", job)
            self.assertNotIn("salary_max", job)

    def test_company_defaults_to_board(self) -> None:
        board = lever.parse_board(
            load_fixture("lever_postings.json"),
            board="leverdemo",
        )

        self.assertEqual(board.jobs[0]["company"], "leverdemo")

    def test_rows_without_contract_fields_are_skipped(self) -> None:
        board = lever.parse_board(
            [{"text": "No URL"}, {"hostedUrl": "https://example.invalid/x"}],
            board="acme",
        )

        self.assertEqual(board.jobs, [])
        self.assertTrue(board.complete)

    def test_rejects_unexpected_payload(self) -> None:
        with self.assertRaises(ProviderError):
            lever.parse_board({"jobs": []}, board="acme")


class AshbyTest(unittest.TestCase):
    def test_parses_listed_board_and_drops_unlisted(self) -> None:
        board = ashby.parse_board(
            load_fixture("ashby_board.json"),
            board="ramp",
            company="Ramp",
        )

        self.assertTrue(board.complete)
        self.assertEqual(board.total, 2)
        self.assertEqual(len(board.jobs), 2)

        first = board.jobs[0]
        self.assertEqual(first["source"], "ashby")
        self.assertEqual(first["title"], "Security Engineer, Cloud")
        self.assertEqual(first["company"], "Ramp")
        self.assertEqual(first["location"], "New York, NY (HQ)")
        self.assertEqual(
            first["source_url"],
            "https://jobs.ashbyhq.com/ramp/"
            "34413f8d-26bf-4bbc-8ade-eb309a0e2245",
        )
        self.assertEqual(
            first["external_id"],
            "34413f8d-26bf-4bbc-8ade-eb309a0e2245",
        )
        self.assertEqual(first["published_at"], "2026-04-07 17:12:35")
        self.assertEqual(
            first["description"],
            "Design and build distributed systems.",
        )
        self.assertEqual(first["requirements"], [])

        for job in board.jobs:
            self.assertNotIn("unlisted-0001", job["source_url"])

    def test_rows_without_contract_fields_are_skipped(self) -> None:
        board = ashby.parse_board(
            {"jobs": [{"title": "No URL"}, {"jobUrl": "https://x.invalid"}]},
            board="acme",
        )

        self.assertEqual(board.jobs, [])

    def test_rejects_unexpected_payload(self) -> None:
        with self.assertRaises(ProviderError):
            ashby.parse_board([], board="acme")

        with self.assertRaises(ProviderError):
            ashby.parse_board({"apiVersion": "1"}, board="acme")


class DispatchTest(unittest.TestCase):
    def test_normalize_board_dispatches(self) -> None:
        board = normalize_board(
            "lever",
            load_fixture("lever_postings.json"),
            board="leverdemo",
        )

        self.assertIsInstance(board, BoardResult)
        self.assertEqual(board.source, "lever")

    def test_unknown_provider_raises(self) -> None:
        with self.assertRaises(ProviderError):
            provider_module("workday")


if __name__ == "__main__":
    unittest.main()
