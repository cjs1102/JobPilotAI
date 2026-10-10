"""Tests for the provider fetch dispatch (JSON vs text/HTML/feed sources)."""

from __future__ import annotations

import unittest
from typing import Any

from jobpilot_sources.cli import fetch_board_payload
from jobpilot_sources.fetch import TransportResponse


class FakeClient:
    def __init__(self) -> None:
        self.urls: list[str] = []

    def get_json(self, url: str) -> Any:
        self.urls.append(url)
        return {"jobs": []}


class FakeFetcher:
    def __init__(self) -> None:
        self.urls: list[str] = []

    def get_text(self, url: str, *, check_robots: bool = True):
        self.urls.append(url)
        return "<html>ok</html>", TransportResponse(
            status=200, headers={}, body=b"<html>ok</html>", url=url
        )


class FakeJsonProvider:
    FETCH_MODE = "json"

    @staticmethod
    def board_url(board: str) -> str:
        return f"https://api.example.invalid/{board}"

    @staticmethod
    def parse_board(payload, *, board, company=None):  # pragma: no cover
        return payload


class FakeTextProvider:
    FETCH_MODE = "text"

    @staticmethod
    def board_url(board: str) -> str:
        return board


class FetchDispatchTest(unittest.TestCase):
    def test_json_provider_uses_the_http_client(self) -> None:
        client = FakeClient()
        fetcher = FakeFetcher()

        payload = fetch_board_payload(
            FakeJsonProvider,
            board="acme",
            http=client,
            fetcher=fetcher,
        )

        self.assertEqual(payload, {"jobs": []})
        self.assertEqual(client.urls, ["https://api.example.invalid/acme"])
        self.assertEqual(fetcher.urls, [])

    def test_text_provider_uses_the_robots_aware_fetcher(self) -> None:
        client = FakeClient()
        fetcher = FakeFetcher()

        payload = fetch_board_payload(
            FakeTextProvider,
            board="https://jobs.example.invalid/campus",
            http=client,
            fetcher=fetcher,
        )

        self.assertEqual(payload, "<html>ok</html>")
        self.assertEqual(fetcher.urls, ["https://jobs.example.invalid/campus"])
        self.assertEqual(client.urls, [])

    def test_public_page_declares_text_mode(self) -> None:
        from jobpilot_sources.providers import public_page

        self.assertEqual(public_page.FETCH_MODE, "text")
        self.assertEqual(
            public_page.board_url("https://x/jobs"), "https://x/jobs"
        )


if __name__ == "__main__":
    unittest.main()
