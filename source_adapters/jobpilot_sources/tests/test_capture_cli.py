"""CLI tests for the manual-capture commands.

Covers the bookmarklet output, dry-run, a successful publish through an injected
transport, every failure path, the exit-code contract (0 = verified success,
1 = failed publish, 2 = input/config problem before any request) and the rule
that the internal token never reaches stdout, stderr, the capture artifact or a
command-line argument.
"""

from __future__ import annotations

import contextlib
import io
import json
import os
import unittest
from pathlib import Path
from typing import Any, Mapping

from jobpilot_sources import cli
from jobpilot_sources.registry import PlatformNameRegistry
from jobpilot_sources.tests.support import FIXTURES, load_fixture

TOKEN = "unit-test-internal-token-value"
TOKEN_ENV = "JOBPILOT_CAPTURE_TEST_TOKEN"
SUCCESS_BODY = b'{"created":1,"updated":0,"total":1}'


class RecordingTransport:
    def __init__(
        self,
        status: int = 200,
        body: bytes = SUCCESS_BODY,
        error: Exception | None = None,
    ) -> None:
        self.status = status
        self.body = body
        self.error = error
        self.calls: list[dict[str, Any]] = []

    def __call__(
        self,
        method: str,
        url: str,
        body: bytes | None,
        headers: Mapping[str, str],
        timeout: float,
    ) -> tuple[int, bytes]:
        self.calls.append(
            {
                "method": method,
                "url": url,
                "body": body,
                "headers": dict(headers),
                "timeout": timeout,
            }
        )

        if self.error is not None:
            raise self.error

        return self.status, self.body


def capture_file() -> str:
    return str(FIXTURES / "capture_complete.json")


def capture_path(name: str) -> str:
    return str(FIXTURES / name)


class BookmarkletCommandTest(unittest.TestCase):
    def test_prints_the_javascript_url(self) -> None:
        stdout = io.StringIO()

        with contextlib.redirect_stdout(stdout):
            code = cli.main(["capture-bookmarklet"])

        self.assertEqual(code, 0)
        self.assertIn("javascript:", stdout.getvalue())
        self.assertNotIn(TOKEN, stdout.getvalue())

    def test_prints_the_raw_javascript(self) -> None:
        stdout = io.StringIO()

        with contextlib.redirect_stdout(stdout):
            code = cli.main(["capture-bookmarklet", "--js"])

        self.assertEqual(code, 0)
        self.assertIn("jobpilotCapture", stdout.getvalue())

    def test_prints_the_help_page(self) -> None:
        stdout = io.StringIO()

        with contextlib.redirect_stdout(stdout):
            code = cli.main(["capture-bookmarklet", "--html"])

        self.assertEqual(code, 0)
        self.assertIn("<!DOCTYPE html>", stdout.getvalue())
        self.assertIn("no network request", stdout.getvalue())


class PublishDryRunTest(unittest.TestCase):
    def test_dry_run_validates_and_sends_nothing(self) -> None:
        transport = RecordingTransport()
        stdout = io.StringIO()

        code = cli.run_publish_capture(
            path=capture_file(),
            config_path=None,
            base_url="http://127.0.0.1:8080",
            token_env=TOKEN_ENV,
            dry_run=True,
            as_json=True,
            log=stdout.write,
            environ={TOKEN_ENV: TOKEN},
            transport=transport,
        )

        self.assertEqual(code, 0)
        self.assertEqual(transport.calls, [])

        result = json.loads(stdout.getvalue())
        self.assertTrue(result["ok"])
        self.assertTrue(result["dry_run"])
        self.assertEqual(result["submitted"], 1)
        self.assertEqual(result["jobs"][0]["source"], "manual")
        self.assertNotIn(TOKEN, stdout.getvalue())

    def test_dry_run_from_stdin(self) -> None:
        raw = json.dumps(load_fixture("capture_minimal.json"))
        stdout = io.StringIO()

        with unittest.mock.patch("sys.stdin", io.StringIO(raw)):
            code = cli.run_publish_capture(
                path="-",
                config_path=None,
                base_url="http://127.0.0.1:8080",
                token_env=TOKEN_ENV,
                dry_run=True,
                as_json=False,
                log=stdout.write,
                environ={TOKEN_ENV: TOKEN},
            )

        self.assertEqual(code, 0)
        self.assertIn("[dry-run]", stdout.getvalue())


class PublishSuccessTest(unittest.TestCase):
    def test_success_reports_the_verified_counters(self) -> None:
        transport = RecordingTransport()
        stdout = io.StringIO()

        code = cli.run_publish_capture(
            path=capture_file(),
            config_path=None,
            base_url="http://127.0.0.1:8080/",
            token_env=TOKEN_ENV,
            dry_run=False,
            as_json=True,
            log=stdout.write,
            environ={TOKEN_ENV: TOKEN},
            transport=transport,
        )

        self.assertEqual(code, 0)
        result = json.loads(stdout.getvalue())
        for key, value in (
            ("ok", True),
            ("submitted", 1),
            ("created", 1),
            ("updated", 0),
            ("total", 1),
        ):
            self.assertEqual(result[key], value)

        # Additive 0015A pre-flight keys (Step 7): counters, per-item findings
        # and the registry that was consulted.
        self.assertIn("counters", result)
        self.assertIn("items", result)
        self.assertIn("registry", result)

        self.assertEqual(len(transport.calls), 1)
        call = transport.calls[0]
        self.assertEqual(
            call["url"], "http://127.0.0.1:8080/internal/jobs/import"
        )
        self.assertEqual(call["headers"]["X-Internal-Token"], TOKEN)
        self.assertNotIn(TOKEN.encode(), call["body"])
        self.assertNotIn(TOKEN, stdout.getvalue())

    def test_human_readable_output_mentions_the_counters(self) -> None:
        transport = RecordingTransport()
        stdout = io.StringIO()

        code = cli.run_publish_capture(
            path=capture_file(),
            config_path=None,
            base_url="http://127.0.0.1:8080",
            token_env=TOKEN_ENV,
            dry_run=False,
            as_json=False,
            log=stdout.write,
            environ={TOKEN_ENV: TOKEN},
            transport=transport,
        )

        self.assertEqual(code, 0)
        self.assertIn("created=1", stdout.getvalue())
        self.assertIn("total=1", stdout.getvalue())


class QualityFindingsTest(unittest.TestCase):
    """0015A Step 7: the --json pre-flight shows why an item would be refused."""

    def run_publish(
        self,
        path: str | None = None,
        *,
        registry: PlatformNameRegistry | None = None,
        dry_run: bool = False,
        as_json: bool = True,
        transport: RecordingTransport | None = None,
        environ: dict[str, str] | None = None,
    ) -> tuple[int, str]:
        stdout = io.StringIO()

        code = cli.run_publish_capture(
            path=path or capture_file(),
            config_path=None,
            base_url="http://127.0.0.1:8080",
            token_env=TOKEN_ENV,
            dry_run=dry_run,
            as_json=as_json,
            log=stdout.write,
            environ=environ if environ is not None else {TOKEN_ENV: TOKEN},
            transport=transport or RecordingTransport(),
            registry=(
                registry
                if registry is not None
                else PlatformNameRegistry.from_names(["牛客"])
            ),
        )

        return code, stdout.getvalue()

    def test_dry_run_reports_counters_and_a_clean_item(self) -> None:
        code, output = self.run_publish(dry_run=True)
        result = json.loads(output)

        self.assertEqual(code, 0)
        self.assertEqual(
            result["counters"],
            {"clean": 1, "needs_review": 0, "rejected": 0, "advisory": 0},
        )

        item = result["items"][0]

        self.assertEqual(item["outcome"], "clean")
        self.assertEqual(item["review_status"], 0)
        self.assertEqual(item["findings"], [])
        self.assertEqual(result["registry"]["size"], 1)
        self.assertNotIn(TOKEN, output)

    def test_dry_run_surfaces_a_needs_review_platform_match(self) -> None:
        document = {
            "title": "T",
            "company": "牛客网",
            "source_url": "https://x.invalid/campus/1001",
        }

        with tempfile_json(json.dumps(document)) as path:
            code, output = self.run_publish(path=path, dry_run=True)

        result = json.loads(output)

        self.assertEqual(code, 0)  # needs_review is storable, not a refusal
        self.assertEqual(result["counters"]["needs_review"], 1)

        finding = result["items"][0]["findings"][0]

        self.assertEqual(finding["code"], "employer_is_platform")
        self.assertEqual(finding["outcome"], "needs_review")
        self.assertEqual(finding["review_status"], 1)

    def test_human_readable_dry_run_explains_the_flag(self) -> None:
        document = {
            "title": "T",
            "company": "牛客网",
            "source_url": "https://x.invalid/campus/1001",
        }

        with tempfile_json(json.dumps(document)) as path:
            code, output = self.run_publish(
                path=path, dry_run=True, as_json=False
            )

        self.assertEqual(code, 0)
        self.assertIn("employer_is_platform", output)
        self.assertIn("needs_review", output)

    def test_a_needs_review_item_is_still_published(self) -> None:
        document = {
            "title": "T",
            "company": "某公司",
            "source_url": "https://www.example.invalid/",
        }
        transport = RecordingTransport()

        with tempfile_json(json.dumps(document)) as path:
            code, output = self.run_publish(path=path, transport=transport)

        result = json.loads(output)

        self.assertEqual(code, 0)
        self.assertEqual(len(transport.calls), 1)
        self.assertTrue(result["ok"])
        self.assertEqual(result["counters"]["needs_review"], 1)
        self.assertEqual(
            result["items"][0]["findings"][0]["code"], "source_url_not_detail"
        )

    def test_over_cap_description_is_rejected_before_any_request(self) -> None:
        document = {
            "title": "T",
            "company": "C",
            "source_url": "https://x.invalid/campus/1001",
            "description": "a" * 49153,
        }
        transport = RecordingTransport()

        with tempfile_json(json.dumps(document)) as path:
            code, output = self.run_publish(path=path, transport=transport)

        result = json.loads(output)

        self.assertEqual(code, 2)
        self.assertEqual(transport.calls, [])
        self.assertFalse(result["ok"])
        self.assertEqual(result["stage"], "quality")
        self.assertEqual(result["counters"]["rejected"], 1)
        self.assertIn("description_too_large", result["error"])

        finding = result["items"][0]["findings"][0]

        self.assertEqual(finding["code"], "description_too_large")
        self.assertEqual(finding["field"], "description")
        self.assertEqual(finding["limit_bytes"], 49152)
        self.assertEqual(finding["actual_bytes"], 49153)
        self.assertEqual(finding["outcome"], "rejected")

    def test_exactly_49152_bytes_is_accepted_by_the_pre_flight(self) -> None:
        document = {
            "title": "T",
            "company": "C",
            "source_url": "https://x.invalid/campus/1001",
            "description": "a" * 49152,
        }

        with tempfile_json(json.dumps(document)) as path:
            code, output = self.run_publish(path=path, dry_run=True)

        result = json.loads(output)

        self.assertEqual(code, 0)
        self.assertEqual(result["counters"]["clean"], 1)
        self.assertEqual(
            len(result["jobs"][0]["description"].encode("utf-8")), 49152
        )

    def test_multibyte_description_boundary_is_bytes_not_characters(self) -> None:
        document = {
            "title": "T",
            "company": "C",
            "source_url": "https://x.invalid/campus/1001",
            "description": "汉" * 16385,
        }

        with tempfile_json(json.dumps(document)) as path:
            code, output = self.run_publish(path=path, dry_run=True)

        result = json.loads(output)

        self.assertEqual(code, 2)
        self.assertEqual(result["stage"], "quality")
        self.assertEqual(
            result["items"][0]["findings"][0]["actual_bytes"], 49155
        )

    def test_a_parse_level_reject_carries_a_finding_too(self) -> None:
        code, output = self.run_publish(
            path=capture_path("capture_missing_company.json")
        )
        result = json.loads(output)

        self.assertEqual(code, 2)
        self.assertEqual(result["stage"], "capture")

        finding = result["findings"][0]

        self.assertEqual(finding["code"], "company_missing")
        self.assertEqual(finding["field"], "company")
        self.assertEqual(finding["outcome"], "rejected")

    def test_missing_registry_file_fails_open_with_a_warning(self) -> None:
        registry = PlatformNameRegistry.load("/nonexistent/platform_names.yaml")

        code, output = self.run_publish(dry_run=True, registry=registry)
        result = json.loads(output)

        self.assertEqual(code, 0)
        self.assertEqual(result["registry"]["size"], 0)
        self.assertIn("unavailable", result["registry"]["warning"])
        self.assertEqual(result["counters"]["clean"], 1)


class PublishFailureTest(unittest.TestCase):
    def run_publish(self, transport: RecordingTransport, **overrides: Any) -> tuple[int, str]:
        stdout = io.StringIO()
        arguments: dict[str, Any] = {
            "path": capture_file(),
            "config_path": None,
            "base_url": "http://127.0.0.1:8080",
            "token_env": TOKEN_ENV,
            "dry_run": False,
            "as_json": True,
            "log": stdout.write,
            "environ": {TOKEN_ENV: TOKEN},
            "transport": transport,
        }
        arguments.update(overrides)

        return cli.run_publish_capture(**arguments), stdout.getvalue()

    def test_missing_token_fails_before_any_request(self) -> None:
        transport = RecordingTransport()
        code, output = self.run_publish(transport, environ={})

        self.assertEqual(code, 2)
        self.assertEqual(transport.calls, [])
        self.assertFalse(json.loads(output)["ok"])
        self.assertEqual(json.loads(output)["stage"], "auth")

    def test_http_error_is_reported_with_the_server_message(self) -> None:
        transport = RecordingTransport(
            status=400, body=b'{"code":10001,"message":"title invalid"}'
        )
        code, output = self.run_publish(transport)

        self.assertEqual(code, 1)
        result = json.loads(output)
        self.assertFalse(result["ok"])
        self.assertEqual(result["stage"], "publish")
        self.assertIn("title invalid", result["error"])
        self.assertNotIn(TOKEN, output)

    def test_unauthorized_is_a_failure(self) -> None:
        code, output = self.run_publish(
            RecordingTransport(status=401, body=b'{"code":20001,"message":"unauthorized"}')
        )

        self.assertEqual(code, 1)
        self.assertIn("401", json.loads(output)["error"])

    def test_server_error_and_invalid_body_are_failures(self) -> None:
        for transport in (
            RecordingTransport(status=500, body=b"{}"),
            RecordingTransport(status=200, body=b"not json"),
            RecordingTransport(status=200, body=b'{"created":1,"updated":0,"total":7}'),
        ):
            with self.subTest(status=transport.status, body=transport.body):
                code, output = self.run_publish(transport)

                self.assertEqual(code, 1)
                self.assertFalse(json.loads(output)["ok"])

    def test_network_and_timeout_errors_are_failures(self) -> None:
        for error in (TimeoutError("timed out"), OSError("connection refused")):
            with self.subTest(error=type(error).__name__):
                code, output = self.run_publish(RecordingTransport(error=error))

                self.assertEqual(code, 1)
                self.assertIn("network/timeout", json.loads(output)["error"])

    def test_missing_file_is_an_input_error(self) -> None:
        code, output = self.run_publish(
            RecordingTransport(), path="/nonexistent/capture.json"
        )

        self.assertEqual(code, 2)
        self.assertEqual(json.loads(output)["stage"], "read")

    def test_invalid_json_is_an_input_error(self) -> None:
        with tempfile_json("{not json") as path:
            code, output = self.run_publish(RecordingTransport(), path=path)

        self.assertEqual(code, 2)
        self.assertEqual(json.loads(output)["stage"], "parse")

    def test_invalid_capture_names_the_problem(self) -> None:
        code, output = self.run_publish(
            RecordingTransport(), path=capture_path("capture_missing_company.json")
        )

        self.assertEqual(code, 2)
        result = json.loads(output)
        self.assertEqual(result["stage"], "capture")
        self.assertIn("job.company", result["error"])

    def test_duplicate_source_url_is_rejected_before_any_request(self) -> None:
        transport = RecordingTransport()
        code, output = self.run_publish(
            transport, path=capture_path("capture_duplicate_urls.json")
        )

        self.assertEqual(code, 2)
        self.assertEqual(transport.calls, [])
        self.assertIn("same source_url", json.loads(output)["error"])

    def test_missing_base_url_is_an_input_error(self) -> None:
        code, output = self.run_publish(RecordingTransport(), base_url=None)

        self.assertEqual(code, 2)
        self.assertEqual(json.loads(output)["stage"], "config")

    def test_failures_never_report_ok_true(self) -> None:
        for transport in (
            RecordingTransport(status=500),
            RecordingTransport(status=200, body=b"{}"),
            RecordingTransport(error=TimeoutError("x")),
        ):
            with self.subTest(transport=transport):
                code, output = self.run_publish(transport)

                self.assertNotEqual(code, 0)
                self.assertFalse(json.loads(output)["ok"])


class TokenBoundaryTest(unittest.TestCase):
    def test_cli_has_no_token_value_option(self) -> None:
        parser = cli.build_parser()

        option_strings: set[str] = set()

        for action in parser._actions:
            option_strings.update(action.option_strings)

            if hasattr(action, "choices") and isinstance(action.choices, dict):
                for subparser in action.choices.values():
                    for sub_action in subparser._actions:
                        option_strings.update(sub_action.option_strings)

        self.assertIn("--token-env", option_strings)

        for forbidden in ("--token", "--token-value", "--api-token", "--secret"):
            self.assertNotIn(forbidden, option_strings)

    def test_token_is_only_taken_from_the_environment(self) -> None:
        source = (
            Path(__file__).resolve().parents[1] / "cli.py"
        ).read_text(encoding="utf-8")

        self.assertIn("resolve_publish_token", source)
        self.assertNotIn("--token ", source)
        self.assertNotIn("add_argument(\"--token\"", source)

    def test_example_config_contains_only_an_env_var_name(self) -> None:
        import yaml

        with open(
            Path(__file__).resolve().parents[2] / "config.example.yaml",
            encoding="utf-8",
        ) as handle:
            document = yaml.safe_load(handle)

        env_name = document["jobpilot"]["internal_token_env"]

        self.assertRegex(env_name, r"^[A-Z][A-Z0-9_]*$")
        self.assertNotIn("token", document["jobpilot"], "no token value key")

    def test_publish_output_never_contains_the_token_on_any_path(self) -> None:
        cases = (
            RecordingTransport(),
            RecordingTransport(status=401, body=b'{"code":20001,"message":"unauthorized"}'),
            RecordingTransport(error=TimeoutError("timed out")),
        )

        for transport in cases:
            with self.subTest(status=transport.status):
                stdout = io.StringIO()

                with contextlib.redirect_stderr(stdout):
                    cli.run_publish_capture(
                        path=capture_file(),
                        config_path=None,
                        base_url="http://127.0.0.1:8080",
                        token_env=TOKEN_ENV,
                        dry_run=False,
                        as_json=True,
                        log=stdout.write,
                        environ={TOKEN_ENV: TOKEN},
                        transport=transport,
                    )

                self.assertNotIn(TOKEN, stdout.getvalue())


class TempJson:
    def __init__(self, content: str) -> None:
        self.content = content
        self.path: str | None = None

    def __enter__(self) -> str:
        import tempfile

        handle = tempfile.NamedTemporaryFile(
            "w", suffix=".json", delete=False, encoding="utf-8"
        )
        handle.write(self.content)
        handle.close()
        self.path = handle.name
        return handle.name

    def __exit__(self, *_: Any) -> None:
        if self.path:
            os.unlink(self.path)


def tempfile_json(content: str) -> TempJson:
    return TempJson(content)


import unittest.mock  # noqa: E402  (used by the stdin test patch)

if __name__ == "__main__":
    unittest.main()
