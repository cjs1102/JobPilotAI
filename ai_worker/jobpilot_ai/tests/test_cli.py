"""CLI behaviour, exit codes and clean failures (no traceback-only crash)."""

from __future__ import annotations

import contextlib
import io
import os
import socket
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

import jobpilot_ai
from jobpilot_ai import cli
from jobpilot_ai.client import ApiError, ClientError

from .support import (
    TOKEN,
    TOKEN_ENV,
    config_document,
    write_config,
)

PROJECT_ROOT = Path(jobpilot_ai.__file__).resolve().parent.parent


class FakeApiClient:
    """Records how the CLI built the client and answers claims."""

    instances: list["FakeApiClient"] = []

    def __init__(self, base_url, token, **kwargs) -> None:
        self.base_url = base_url
        self.token = token
        self.kwargs = kwargs
        self.claims: list[dict] = []
        self.completions: list[dict] = []
        self.failures: list[dict] = []
        self.task: dict | None = None
        self.error: Exception | None = None
        FakeApiClient.instances.append(self)

    def claim(self, worker_id, *, lease_seconds, task_types=None):
        self.claims.append({"worker_id": worker_id})

        if self.error is not None:
            raise self.error

        return self.task

    def fetch_input(self, task_id, *, worker_id):
        return {}

    def complete(self, task_id, *, worker_id, result):
        self.completions.append({"task_id": task_id, "result": result})

    def fail(self, task_id, *, worker_id, error_msg, retryable):
        self.failures.append({"task_id": task_id, "retryable": retryable})


def free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.bind(("127.0.0.1", 0))

        return int(sock.getsockname()[1])


class ParserTest(unittest.TestCase):
    def test_help_exits_zero(self) -> None:
        with self.assertRaises(SystemExit) as caught:
            with contextlib.redirect_stdout(io.StringIO()) as stdout:
                cli.main(["worker", "--help"])

        self.assertEqual(caught.exception.code, 0)
        self.assertIn("usage", stdout.getvalue())

    def test_a_command_is_required(self) -> None:
        with self.assertRaises(SystemExit) as caught:
            with contextlib.redirect_stderr(io.StringIO()):
                cli.main([])

        self.assertEqual(caught.exception.code, 2)

    def test_config_is_required(self) -> None:
        with self.assertRaises(SystemExit) as caught:
            with contextlib.redirect_stderr(io.StringIO()):
                cli.main(["worker"])

        self.assertEqual(caught.exception.code, 2)

    def test_the_provider_key_can_never_be_a_flag(self) -> None:
        with self.assertRaises(SystemExit) as caught:
            with contextlib.redirect_stderr(io.StringIO()):
                cli.main(
                    [
                        "worker",
                        "--config",
                        "unused.yaml",
                        "--key",
                        "sk-should-never-exist",
                    ]
                )

        self.assertEqual(caught.exception.code, 2)


class ConfigurationFailureTest(unittest.TestCase):
    def setUp(self) -> None:
        self._temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self._temporary.cleanup)
        self.directory = Path(self._temporary.name)
        self.config_path = write_config(self.directory, config_document())

    def test_missing_config_file_exits_two(self) -> None:
        stderr = io.StringIO()

        with contextlib.redirect_stderr(stderr):
            code = cli.main(
                ["worker", "--config", str(self.directory / "absent.yaml")]
            )

        self.assertEqual(code, 2)
        self.assertIn("config error", stderr.getvalue())

    def test_invalid_config_exits_two(self) -> None:
        path = self.directory / "bad.yaml"
        path.write_text("jobpilot:\n  base_url: ftp://x\n", encoding="utf-8")
        stderr = io.StringIO()

        with contextlib.redirect_stderr(stderr):
            code = cli.main(["worker", "--config", str(path)])

        self.assertEqual(code, 2)
        self.assertIn("config error", stderr.getvalue())

    def test_missing_token_exits_two_before_any_client_is_built(self) -> None:
        FakeApiClient.instances.clear()
        stderr = io.StringIO()

        with mock.patch.dict(os.environ, {}, clear=True):
            with mock.patch.object(cli, "InternalApiClient", FakeApiClient):
                with contextlib.redirect_stderr(stderr):
                    code = cli.main(["worker", "--config", str(self.config_path)])

        self.assertEqual(code, 2)
        self.assertEqual(FakeApiClient.instances, [])
        self.assertIn(TOKEN_ENV, stderr.getvalue())

    def test_negative_interval_exits_two(self) -> None:
        with mock.patch.dict(os.environ, {TOKEN_ENV: TOKEN}, clear=True):
            with contextlib.redirect_stderr(io.StringIO()):
                code = cli.main(
                    ["worker", "--config", str(self.config_path), "--interval", "-1"]
                )

        self.assertEqual(code, 2)

    def test_zero_max_tasks_exits_two(self) -> None:
        with mock.patch.dict(os.environ, {TOKEN_ENV: TOKEN}, clear=True):
            with contextlib.redirect_stderr(io.StringIO()):
                code = cli.main(
                    ["worker", "--config", str(self.config_path), "--max-tasks", "0"]
                )

        self.assertEqual(code, 2)


class FakeWorker:
    """Captures the config the CLI derived and answers without any I/O."""

    last_config = None
    last_kwargs: dict = {}

    def __init__(self, config, *, client) -> None:
        self.config = config
        self.client = client
        FakeWorker.last_config = config

    def run(self, *, once=False, max_tasks=None) -> int:
        FakeWorker.last_kwargs = {"once": once, "max_tasks": max_tasks}

        return 0


class RunTest(unittest.TestCase):
    def setUp(self) -> None:
        self._temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self._temporary.cleanup)
        self.directory = Path(self._temporary.name)
        self.config_path = write_config(self.directory, config_document())
        FakeApiClient.instances.clear()
        FakeWorker.last_config = None
        FakeWorker.last_kwargs = {}

    def test_once_on_an_empty_queue_exits_zero(self) -> None:
        stdout = io.StringIO()

        with mock.patch.dict(os.environ, {TOKEN_ENV: TOKEN}, clear=True):
            with mock.patch.object(cli, "InternalApiClient", FakeApiClient):
                with contextlib.redirect_stdout(stdout):
                    code = cli.main(
                        ["worker", "--config", str(self.config_path), "--once"]
                    )

        self.assertEqual(code, 0)
        self.assertIn("queue empty", stdout.getvalue())
        self.assertEqual(FakeApiClient.instances[0].token, TOKEN)
        self.assertNotIn(TOKEN, stdout.getvalue())

    def test_interval_flag_overrides_the_config(self) -> None:
        with mock.patch.dict(os.environ, {TOKEN_ENV: TOKEN}, clear=True):
            with mock.patch.object(cli, "InternalApiClient", FakeApiClient):
                with mock.patch.object(cli, "Worker", FakeWorker):
                    code = cli.main(
                        [
                            "worker",
                            "--config",
                            str(self.config_path),
                            "--interval",
                            "7",
                        ]
                    )

        self.assertEqual(code, 0)
        self.assertEqual(FakeWorker.last_config.interval_seconds, 7.0)
        self.assertEqual(FakeWorker.last_kwargs, {"once": False, "max_tasks": None})

    def test_max_tasks_and_once_are_forwarded(self) -> None:
        with mock.patch.dict(os.environ, {TOKEN_ENV: TOKEN}, clear=True):
            with mock.patch.object(cli, "InternalApiClient", FakeApiClient):
                with mock.patch.object(cli, "Worker", FakeWorker):
                    code = cli.main(
                        [
                            "worker",
                            "--config",
                            str(self.config_path),
                            "--once",
                            "--max-tasks",
                            "3",
                        ]
                    )

        self.assertEqual(code, 0)
        self.assertEqual(FakeWorker.last_kwargs, {"once": True, "max_tasks": 3})

    def test_api_failure_exits_one_with_a_clear_message(self) -> None:
        stderr = io.StringIO()

        def factory(*args, **kwargs):
            client = FakeApiClient(*args, **kwargs)
            client.error = ApiError(
                "request to http://127.0.0.1:8080/internal/ai/tasks/claim "
                "failed with HTTP 500 code 50000"
            )

            return client

        with mock.patch.dict(os.environ, {TOKEN_ENV: TOKEN}, clear=True):
            with mock.patch.object(cli, "InternalApiClient", factory):
                with contextlib.redirect_stderr(stderr):
                    code = cli.main(
                        ["worker", "--config", str(self.config_path), "--once"]
                    )

        self.assertEqual(code, 1)
        self.assertIn("error:", stderr.getvalue())
        self.assertIn("HTTP 500", stderr.getvalue())
        self.assertNotIn("Traceback", stderr.getvalue())
        self.assertNotIn(TOKEN, stderr.getvalue())

    def test_transport_failure_exits_one(self) -> None:
        stderr = io.StringIO()

        def factory(*args, **kwargs):
            client = FakeApiClient(*args, **kwargs)
            client.error = ClientError(
                "POST http://127.0.0.1:9/internal/ai/tasks/claim failed: "
                "request failed: [Errno 111] Connection refused"
            )

            return client

        with mock.patch.dict(os.environ, {TOKEN_ENV: TOKEN}, clear=True):
            with mock.patch.object(cli, "InternalApiClient", factory):
                with contextlib.redirect_stderr(stderr):
                    code = cli.main(
                        ["worker", "--config", str(self.config_path), "--once"]
                    )

        self.assertEqual(code, 1)
        self.assertIn("Connection refused", stderr.getvalue())


class SubprocessTest(unittest.TestCase):
    """Real process runs: exit codes and clean failure output."""

    def setUp(self) -> None:
        self._temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self._temporary.cleanup)
        self.directory = Path(self._temporary.name)

    def run_cli(self, arguments: list[str], env_overrides=None):
        environment = dict(os.environ)

        if env_overrides:
            environment.update(env_overrides)

        return subprocess.run(
            [sys.executable, "-m", "jobpilot_ai", *arguments],
            cwd=str(PROJECT_ROOT),
            capture_output=True,
            text=True,
            timeout=60,
            env=environment,
        )

    def test_worker_help_runs_in_a_real_process(self) -> None:
        result = self.run_cli(["worker", "--help"])

        self.assertEqual(result.returncode, 0)
        self.assertIn("usage", result.stdout)
        self.assertIn("--once", result.stdout)

    def test_once_against_an_absent_service_fails_cleanly(self) -> None:
        port = free_port()
        document = config_document(
            jobpilot={
                "base_url": f"http://127.0.0.1:{port}",
                "request_timeout_seconds": 1.0,
                "max_retries": 0,
                "interval_seconds": 0,
            }
        )
        path = write_config(self.directory, document)

        result = self.run_cli(
            ["worker", "--config", str(path), "--once"],
            {TOKEN_ENV: TOKEN},
        )

        self.assertEqual(result.returncode, 1)
        self.assertIn("error:", result.stderr)
        self.assertIn("/internal/ai/tasks/claim", result.stderr)
        self.assertNotIn("Traceback", result.stderr)
        self.assertNotIn(TOKEN, result.stderr)
        self.assertNotIn(TOKEN, result.stdout)

    def test_missing_config_fails_cleanly_in_a_real_process(self) -> None:
        result = self.run_cli(
            ["worker", "--config", str(self.directory / "absent.yaml")],
            {TOKEN_ENV: TOKEN},
        )

        self.assertEqual(result.returncode, 2)
        self.assertIn("config error", result.stderr)
        self.assertNotIn("Traceback", result.stderr)


if __name__ == "__main__":
    unittest.main()
