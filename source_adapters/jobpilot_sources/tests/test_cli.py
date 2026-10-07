"""CLI orchestration tests: no live network, token never logged."""

from __future__ import annotations

import contextlib
import io
import os
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from jobpilot_sources import cli
from jobpilot_sources.client import HttpClient, HttpError
from jobpilot_sources.config import JobPilotConfig, SourceConfig

from .support import (
    RecordingTransport,
    api_response,
    base_config,
    fixture_response,
    write_config,
)

TOKEN = "super-secret-token"


def make_config() -> JobPilotConfig:
    return JobPilotConfig(
        base_url="http://127.0.0.1:8080",
        internal_token_env="JOBPILOT_TEST_TOKEN",
        sources=(
            SourceConfig("greenhouse", "gitlab", True, "GitLab"),
            SourceConfig("lever", "leverdemo", True, "Lever Demo"),
            SourceConfig("ashby", "ramp", True, "Ramp"),
        ),
    )


def fixture_transport() -> RecordingTransport:
    return RecordingTransport(
        responses={
            "boards-api.greenhouse.io": fixture_response(
                "greenhouse_board.json"
            ),
            "api.lever.co": fixture_response("lever_postings.json"),
            "api.ashbyhq.com": fixture_response("ashby_board.json"),
            "127.0.0.1": api_response(
                {
                    "created": 2,
                    "updated": 0,
                    "seen": 2,
                    "closed": 0,
                    "complete": True,
                }
            ),
        }
    )


class RunSyncTest(unittest.TestCase):
    def test_dry_run_never_publishes(self) -> None:
        transport = fixture_transport()
        lines: list[str] = []

        code = cli.run_sync(
            make_config(),
            token=TOKEN,
            dry_run=True,
            client=HttpClient(transport=transport),
            clock=lambda: "2026-10-08 12:00:00",
            log=lines.append,
        )

        self.assertEqual(code, 0)
        self.assertEqual(transport.methods(), ["GET", "GET", "GET"])
        self.assertEqual(len(lines), 3)

        for line in lines:
            self.assertIn("[dry-run]", line)

        self.assertIn("2 job(s)", lines[0])
        self.assertIn("complete=True", lines[0])

    def test_publishes_and_reports_counters(self) -> None:
        transport = fixture_transport()
        lines: list[str] = []

        code = cli.run_sync(
            make_config(),
            token=TOKEN,
            client=HttpClient(transport=transport),
            clock=lambda: "2026-10-08 12:00:00",
            log=lines.append,
        )

        self.assertEqual(code, 0)
        self.assertEqual(
            transport.methods(),
            ["GET", "POST", "GET", "POST", "GET", "POST"],
        )

        for request in transport.posts():
            self.assertEqual(
                request["headers"]["X-Internal-Token"],
                TOKEN,
            )

        self.assertIn("created=2", lines[0])
        self.assertIn("updated=0", lines[0])

    def test_provider_filter_limits_fetches(self) -> None:
        transport = fixture_transport()
        lines: list[str] = []

        code = cli.run_sync(
            make_config(),
            token=TOKEN,
            provider="lever",
            dry_run=True,
            client=HttpClient(transport=transport),
            clock=lambda: "2026-10-08 12:00:00",
            log=lines.append,
        )

        self.assertEqual(code, 0)
        self.assertEqual(transport.methods(), ["GET"])
        self.assertIn("api.lever.co", transport.requests[0]["url"])

    def test_fetch_failure_returns_error_code(self) -> None:
        transport = RecordingTransport(
            default=(500, b"{}"),
        )
        lines: list[str] = []

        code = cli.run_sync(
            make_config(),
            token=TOKEN,
            dry_run=True,
            client=HttpClient(transport=transport),
            clock=lambda: "2026-10-08 12:00:00",
            log=lines.append,
        )

        self.assertEqual(code, 1)

        for line in lines:
            self.assertIn("fetch failed", line)

    def test_publish_failure_returns_error_code(self) -> None:
        transport = RecordingTransport(
            responses={
                "greenhouse.io": fixture_response(
                    "greenhouse_board.json"
                ),
                "127.0.0.1": (401, b'{"code":20001}'),
            }
        )
        lines: list[str] = []

        code = cli.run_sync(
            make_config(),
            token=TOKEN,
            provider="greenhouse",
            client=HttpClient(transport=transport),
            clock=lambda: "2026-10-08 12:00:00",
            log=lines.append,
        )

        self.assertEqual(code, 1)
        self.assertIn("publish failed", lines[0])

    def test_rate_limit_is_reported_as_fetch_failure(self) -> None:
        transport = RecordingTransport(default=(429, b"{}"))
        lines: list[str] = []

        code = cli.run_sync(
            make_config(),
            token=TOKEN,
            provider="lever",
            dry_run=True,
            client=HttpClient(transport=transport),
            clock=lambda: "2026-10-08 12:00:00",
            log=lines.append,
        )

        self.assertEqual(code, 1)
        self.assertIn("HTTP 429", lines[0])

    def test_transport_timeout_is_reported_as_fetch_failure(self) -> None:
        class TimingOutTransport:
            def __call__(self, method, url, body, headers, timeout):
                raise HttpError("request failed: timed out")

        lines: list[str] = []

        code = cli.run_sync(
            make_config(),
            token=TOKEN,
            provider="lever",
            dry_run=True,
            client=HttpClient(transport=TimingOutTransport()),
            clock=lambda: "2026-10-08 12:00:00",
            log=lines.append,
        )

        self.assertEqual(code, 1)
        self.assertIn("timed out", lines[0])

    def test_unproven_empty_board_is_skipped(self) -> None:
        transport = RecordingTransport(
            responses={
                "greenhouse.io": api_response(
                    {"jobs": [], "meta": {"total": 5}}
                )
            }
        )
        lines: list[str] = []

        code = cli.run_sync(
            make_config(),
            token=TOKEN,
            provider="greenhouse",
            client=HttpClient(transport=transport),
            clock=lambda: "2026-10-08 12:00:00",
            log=lines.append,
        )

        self.assertEqual(code, 0)
        self.assertEqual(transport.posts(), [])
        self.assertIn("nothing published", lines[0])


    def test_duplicate_source_urls_are_rejected(self) -> None:
        transport = RecordingTransport(
            responses={
                "api.lever.co": api_response(
                    [
                        {
                            "text": "First",
                            "hostedUrl": "https://x.invalid/1",
                        },
                        {
                            "text": "Second",
                            "hostedUrl": "HTTPS://X.INVALID/1",
                        },
                    ]
                )
            }
        )
        lines: list[str] = []

        code = cli.run_sync(
            make_config(),
            token=TOKEN,
            provider="lever",
            dry_run=True,
            client=HttpClient(transport=transport),
            clock=lambda: "2026-10-08 12:00:00",
            log=lines.append,
        )

        self.assertEqual(code, 1)
        self.assertIn("observation rejected", lines[0])
        self.assertIn("duplicate source_url", lines[0])
        self.assertEqual(transport.posts(), [])


class MainTest(unittest.TestCase):
    def setUp(self) -> None:
        self._temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self._temporary.cleanup)
        self.directory = Path(self._temporary.name)
        self.config_path = write_config(self.directory, base_config())

    def test_missing_token_fails_before_any_network_call(self) -> None:
        transport = fixture_transport()

        with mock.patch.object(
            cli,
            "HttpClient",
            lambda: HttpClient(transport=transport),
        ):
            with mock.patch.dict(os.environ, {}, clear=True):
                with contextlib.redirect_stderr(io.StringIO()):
                    code = cli.main(
                        ["sync", "--config", str(self.config_path)]
                    )

        self.assertEqual(code, 2)
        self.assertEqual(transport.requests, [])

    def test_missing_config_returns_config_error(self) -> None:
        with contextlib.redirect_stderr(io.StringIO()):
            code = cli.main(
                ["sync", "--config", str(self.directory / "absent.yaml")]
            )

        self.assertEqual(code, 2)

    def test_dry_run_keeps_the_token_out_of_output(self) -> None:
        transport = fixture_transport()
        stdout = io.StringIO()
        stderr = io.StringIO()

        with mock.patch.object(
            cli,
            "HttpClient",
            lambda: HttpClient(transport=transport),
        ):
            with mock.patch.dict(
                os.environ,
                {"JOBPILOT_TEST_TOKEN": TOKEN},
                clear=True,
            ):
                with contextlib.redirect_stdout(stdout):
                    with contextlib.redirect_stderr(stderr):
                        code = cli.main(
                            [
                                "sync",
                                "--config",
                                str(self.config_path),
                                "--dry-run",
                            ]
                        )

        self.assertEqual(code, 0)
        self.assertNotIn(TOKEN, stdout.getvalue())
        self.assertNotIn(TOKEN, stderr.getvalue())
        self.assertIn("dry-run", stdout.getvalue())

    def test_publish_failure_keeps_the_token_out_of_output(self) -> None:
        transport = RecordingTransport(
            responses={
                "greenhouse.io": fixture_response(
                    "greenhouse_board.json"
                ),
                "127.0.0.1": (401, b'{"code":20001}'),
            }
        )
        stdout = io.StringIO()
        stderr = io.StringIO()

        config = base_config()
        config["sources"] = config["sources"][:1]
        path = write_config(self.directory, config)

        with mock.patch.object(
            cli,
            "HttpClient",
            lambda: HttpClient(transport=transport),
        ):
            with mock.patch.dict(
                os.environ,
                {"JOBPILOT_TEST_TOKEN": TOKEN},
                clear=True,
            ):
                with contextlib.redirect_stdout(stdout):
                    with contextlib.redirect_stderr(stderr):
                        code = cli.main(
                            ["sync", "--config", str(path)]
                        )

        self.assertEqual(code, 1)
        self.assertNotIn(TOKEN, stdout.getvalue())
        self.assertNotIn(TOKEN, stderr.getvalue())


if __name__ == "__main__":
    unittest.main()
