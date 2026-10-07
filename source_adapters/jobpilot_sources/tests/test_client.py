"""HTTP client tests: status handling, invalid payloads, transport failures."""

from __future__ import annotations

import json
import unittest
import urllib.error
from unittest import mock

from jobpilot_sources.client import (
    HttpClient,
    HttpError,
    UrllibTransport,
)

from .support import RecordingTransport, api_response


class HttpClientTest(unittest.TestCase):
    def test_get_returns_parsed_json(self) -> None:
        transport = RecordingTransport(
            default=api_response({"jobs": []})
        )
        client = HttpClient(transport=transport)

        self.assertEqual(
            client.get_json("https://example.invalid/board"),
            {"jobs": []},
        )

    def test_non_2xx_raises_http_error(self) -> None:
        for status in (401, 429, 500, 503):
            transport = RecordingTransport(
                default=(status, b"{}")
            )
            client = HttpClient(transport=transport)

            with self.assertRaises(HttpError) as context:
                client.get_json("https://example.invalid/board")

            self.assertIn(str(status), str(context.exception))

    def test_invalid_json_raises_http_error(self) -> None:
        transport = RecordingTransport(
            default=(200, b"<html>not json</html>")
        )
        client = HttpClient(transport=transport)

        with self.assertRaises(HttpError):
            client.get_json("https://example.invalid/board")

    def test_post_sends_body_headers_and_no_token_in_url(self) -> None:
        transport = RecordingTransport(
            default=api_response({"created": 1})
        )
        client = HttpClient(transport=transport)

        client.post_json(
            "https://example.invalid/internal/jobs/source-sync",
            {"source": "lever"},
            headers={"X-Internal-Token": "secret-token"},
        )

        request = transport.requests[0]
        self.assertEqual(request["method"], "POST")
        self.assertEqual(
            json.loads(request["body"].decode("utf-8")),
            {"source": "lever"},
        )
        self.assertEqual(
            request["headers"]["X-Internal-Token"],
            "secret-token",
        )
        self.assertNotIn("secret-token", request["url"])

    def test_urllib_transport_maps_url_error_to_http_error(self) -> None:
        with mock.patch(
            "urllib.request.urlopen",
            side_effect=urllib.error.URLError("timed out"),
        ):
            with self.assertRaises(HttpError) as context:
                UrllibTransport()(
                    "GET",
                    "https://example.invalid",
                    None,
                    {},
                    1.0,
                )

        self.assertIn("timed out", str(context.exception))

    def test_urllib_transport_returns_http_error_status(self) -> None:
        error = urllib.error.HTTPError(
            "https://example.invalid",
            429,
            "Too Many Requests",
            {},
            None,
        )

        with mock.patch(
            "urllib.request.urlopen",
            side_effect=error,
        ):
            status, _ = UrllibTransport()(
                "GET",
                "https://example.invalid",
                None,
                {},
                1.0,
            )

        self.assertEqual(status, 429)


if __name__ == "__main__":
    unittest.main()
