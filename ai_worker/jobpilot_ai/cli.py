"""Command line entry point for the AI worker.

Usage (ExecPlan 0013 section 6.5):

    python -m jobpilot_ai worker --config ai_worker/config.yaml
    python -m jobpilot_ai worker --config ai_worker/config.yaml --once
    python -m jobpilot_ai worker --config ai_worker/config.yaml --interval 5 \
        --max-tasks 10

Exit codes: 0 = the run finished (a degraded completion is still a success),
1 = the internal API could not be reached or a task failed permanently,
2 = the configuration or the internal token is unusable.

The provider key is never a CLI flag: it is read from the environment variable
named by ``provider.key_env`` inside the provider client.
"""

from __future__ import annotations

import argparse
import sys
from dataclasses import replace
from typing import Sequence

from .client import ClientError, InternalApiClient, RetryPolicy
from .config import ConfigError, load_config, resolve_internal_token
from .worker import Worker

EXIT_OK = 0
EXIT_RUN_FAILED = 1
EXIT_CONFIG_ERROR = 2


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="jobpilot_ai",
        description=(
            "JobPilotAI AI worker: keep the deterministic match_rule_v1 "
            "baseline and optionally add validated, evidence-bound AI output."
        ),
    )
    subparsers = parser.add_subparsers(dest="command", required=True)

    worker = subparsers.add_parser(
        "worker",
        help="claim AI tasks and merge optional provider output",
    )
    worker.add_argument(
        "--config",
        required=True,
        help="path to the YAML configuration file",
    )
    worker.add_argument(
        "--once",
        action="store_true",
        help="claim and process at most one task, then exit (tests/CI)",
    )
    worker.add_argument(
        "--interval",
        type=float,
        default=None,
        help="seconds to back off when the queue is empty (overrides config)",
    )
    worker.add_argument(
        "--max-tasks",
        type=int,
        default=None,
        help="stop after processing this many tasks",
    )

    return parser


def main(argv: Sequence[str] | None = None) -> int:
    parser = build_parser()
    arguments = parser.parse_args(argv)

    try:
        config = load_config(arguments.config)
        token = resolve_internal_token(config)
    except ConfigError as error:
        print(f"config error: {error}", file=sys.stderr)
        return EXIT_CONFIG_ERROR

    if arguments.interval is not None:
        if arguments.interval < 0:
            print(
                "config error: --interval must be zero or greater",
                file=sys.stderr,
            )
            return EXIT_CONFIG_ERROR

        config = replace(config, interval_seconds=float(arguments.interval))

    if arguments.max_tasks is not None and arguments.max_tasks < 1:
        print(
            "config error: --max-tasks must be one or greater",
            file=sys.stderr,
        )
        return EXIT_CONFIG_ERROR

    client = InternalApiClient(
        config.base_url,
        token,
        timeout=config.request_timeout_seconds,
        policy=RetryPolicy(max_retries=config.max_retries),
    )
    worker = Worker(config, client=client)

    try:
        return worker.run(
            once=arguments.once,
            max_tasks=arguments.max_tasks,
        )
    except ClientError as error:
        print(f"error: {error}", file=sys.stderr)
        return EXIT_RUN_FAILED


if __name__ == "__main__":  # pragma: no cover - exercised via __main__
    raise SystemExit(main())
