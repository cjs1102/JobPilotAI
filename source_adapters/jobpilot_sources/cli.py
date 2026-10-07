"""Command line entry point for the source adapters.

Usage (ExecPlan 0012 section 4):

    python -m jobpilot_sources sync --config source_adapters/config.yaml
    python -m jobpilot_sources sync --config source_adapters/config.yaml \
        --provider greenhouse

The internal token is read from the configured environment variable before any
network call and is never logged.
"""

from __future__ import annotations

import argparse
import sys
from datetime import datetime, timezone
from typing import Any, Callable, Sequence

from .client import HttpClient, HttpError
from .config import (
    SUPPORTED_PROVIDERS,
    ConfigError,
    JobPilotConfig,
    load_config,
    resolve_internal_token,
)
from .providers import ProviderError, provider_module
from .publisher import build_batches, publish_batches


def utc_now_timestamp() -> str:
    return datetime.now(timezone.utc).strftime("%Y-%m-%d %H:%M:%S")


def run_sync(
    config: JobPilotConfig,
    *,
    token: str,
    provider: str | None = None,
    dry_run: bool = False,
    client: HttpClient | None = None,
    clock: Callable[[], str] = utc_now_timestamp,
    log: Callable[[str], None] = print,
) -> int:
    """Run one sync pass; returns a process exit code."""

    http = client or HttpClient()
    observed_at = clock()
    failures = 0

    for source in config.enabled_sources(provider):
        module = provider_module(source.type)
        label = f"[{source.type}] {source.board}"

        try:
            payload = http.get_json(module.board_url(source.board))
            board = module.parse_board(
                payload,
                board=source.board,
                company=source.company,
            )
        except (HttpError, ProviderError) as error:
            failures += 1
            log(f"{label}: fetch failed: {error}")
            continue

        batches = build_batches(
            source=source.type,
            scope=source.board,
            observed_at=observed_at,
            jobs=board.jobs,
            complete=board.complete,
        )

        if not batches:
            log(
                f"{label}: no jobs observed and completeness is "
                "unproven; nothing published"
            )
            continue

        if dry_run:
            log(
                f"{label}: {len(board.jobs)} job(s), "
                f"board_complete={board.complete}, "
                f"{len(batches)} batch(es), "
                f"publish_complete={batches[0]['complete']} [dry-run]"
            )
            continue

        try:
            responses = publish_batches(
                http,
                base_url=config.base_url,
                token=token,
                batches=batches,
            )
        except HttpError as error:
            failures += 1
            log(f"{label}: publish failed: {error}")
            continue

        created = _sum_field(responses, "created")
        updated = _sum_field(responses, "updated")
        closed = _sum_field(responses, "closed")

        log(
            f"{label}: {len(board.jobs)} job(s) -> "
            f"created={created} updated={updated} closed={closed} "
            f"complete={batches[0]['complete']}"
        )

    return 1 if failures else 0


def _sum_field(responses: Sequence[Any], field: str) -> int:
    total = 0

    for response in responses:
        if not isinstance(response, dict):
            continue

        value = response.get(field)

        if isinstance(value, int) and not isinstance(value, bool):
            total += value

    return total


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="jobpilot_sources",
        description="Publish public ATS boards to JobPilotAI.",
    )
    subparsers = parser.add_subparsers(
        dest="command",
        required=True,
    )

    sync = subparsers.add_parser(
        "sync",
        help="fetch every enabled source and publish one observation",
    )
    sync.add_argument(
        "--config",
        required=True,
        help="path to the YAML configuration file",
    )
    sync.add_argument(
        "--provider",
        choices=list(SUPPORTED_PROVIDERS),
        default=None,
        help="restrict the run to one provider type",
    )
    sync.add_argument(
        "--dry-run",
        action="store_true",
        help="fetch and report without publishing",
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
        return 2

    return run_sync(
        config,
        token=token,
        provider=arguments.provider,
        dry_run=arguments.dry_run,
    )
