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
import json
import sys
from datetime import datetime, timezone
from typing import Any, Callable, Mapping, Sequence

from .client import HttpClient, HttpError, Transport
from .config import (
    SUPPORTED_PROVIDERS,
    ConfigError,
    JobPilotConfig,
    load_config,
    resolve_internal_token,
)
from .fetch import FetchError, Fetcher
from .providers import ProviderError, provider_module
from .publisher import BatchError, build_batches, publish_batches


def utc_now_timestamp() -> str:
    return datetime.now(timezone.utc).strftime("%Y-%m-%d %H:%M:%S")


DEFAULT_TOKEN_ENV = "INTERNAL_API_TOKEN"


def fetch_board_payload(
    module: Any,
    *,
    board: str,
    http: HttpClient,
    fetcher: Fetcher | None = None,
) -> Any:
    """Fetch one board using the provider's declared fetch mode.

    JSON providers keep the 0012 ``HttpClient.get_json`` path; text providers
    (HTML, JSON-LD, RSS/sitemap) go through :class:`~jobpilot_sources.fetch.Fetcher`,
    which enforces robots, the rate floor, retries, the SSRF guard and the
    response-size cap. ``get_json`` never carries HTML/feed parsing.
    """

    url = module.board_url(board)
    mode = getattr(module, "FETCH_MODE", "json")

    if mode == "text":
        text, _response = fetcher.get_text(url)
        return text

    return http.get_json(url)


def run_sync(
    config: JobPilotConfig,
    *,
    token: str,
    provider: str | None = None,
    dry_run: bool = False,
    client: HttpClient | None = None,
    fetcher: Fetcher | None = None,
    clock: Callable[[], str] = utc_now_timestamp,
    log: Callable[[str], None] = print,
) -> int:
    """Run one sync pass; returns a process exit code."""

    http = client or HttpClient()
    text_fetcher = fetcher
    observed_at = clock()
    failures = 0

    for source in config.enabled_sources(provider):
        module = provider_module(source.type)
        label = f"[{source.type}] {source.board}"

        try:
            if getattr(module, "FETCH_MODE", "json") == "text" and (
                text_fetcher is None
            ):
                text_fetcher = Fetcher()

            payload = fetch_board_payload(
                module,
                board=source.board,
                http=http,
                fetcher=text_fetcher,
            )
            board = module.parse_board(
                payload,
                board=source.board,
                company=source.company,
            )
        except (HttpError, ProviderError, FetchError) as error:
            failures += 1
            log(f"{label}: fetch failed: {error}")
            continue

        try:
            batches = build_batches(
                source=source.type,
                scope=source.board,
                observed_at=observed_at,
                jobs=board.jobs,
                complete=board.complete,
            )
        except BatchError as error:
            failures += 1
            log(f"{label}: observation rejected: {error}")
            continue

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


def run_capture_bookmarklet(
    *,
    as_html: bool = False,
    raw_js: bool = False,
    log: Callable[[str], None] = print,
) -> int:
    """Print the bookmarklet (or its raw source / help page)."""

    from .capture.bookmarklet import (
        BookmarkletError,
        bookmarklet_href,
        bookmarklet_source,
        instructions_html,
    )

    try:
        if as_html:
            log(instructions_html())
            return 0

        if raw_js:
            log(bookmarklet_source())
            return 0

        log(bookmarklet_href())
        log(
            "Paste that javascript: URL as a browser bookmark. The bookmarklet "
            "holds no credential and makes no network request."
        )
        return 0
    except BookmarkletError as error:
        log(f"bookmarklet error: {error}")
        return 2


def run_publish_capture(
    *,
    path: str,
    config_path: str | None,
    base_url: str | None,
    token_env: str | None,
    dry_run: bool,
    as_json: bool,
    log: Callable[[str], None] = print,
    environ: Mapping[str, str] | None = None,
    transport: Transport | None = None,
) -> int:
    """Validate and publish a local capture file; returns an exit code.

    Exit codes: ``0`` only after a verified publish (or a dry run), ``1`` when a
    publish attempt failed, ``2`` for input/config problems detected before any
    request. A failure is never reported as a success.
    """

    from .capture import (
        CaptureError,
        ManualPublisher,
        PublishError,
        build_import_payloads,
        parse_captures,
        resolve_publish_token,
    )

    def emit(payload: dict[str, Any], text: str) -> None:
        log(json.dumps(payload, ensure_ascii=False) if as_json else text)

    def emit_notes(notes: Sequence[str]) -> None:
        # In --json mode the notes are already inside the JSON document, so
        # stdout stays a single parseable object.
        if as_json:
            return

        for note in notes:
            log(f"note: {note}")

    if path == "-":
        raw = sys.stdin.read()
    else:
        try:
            with open(path, "r", encoding="utf-8") as handle:
                raw = handle.read()
        except OSError as error:
            emit(
                {"ok": False, "stage": "read", "error": str(error)},
                f"cannot read capture file: {error}",
            )
            return 2

    try:
        document = json.loads(raw)
    except json.JSONDecodeError as error:
        emit(
            {
                "ok": False,
                "stage": "parse",
                "error": f"capture file is not valid JSON: {error}",
            },
            f"capture file is not valid JSON: {error}",
        )
        return 2

    resolved_base_url = base_url
    resolved_token_env = token_env

    if config_path is not None:
        try:
            config = load_config(config_path)
        except ConfigError as error:
            emit(
                {"ok": False, "stage": "config", "error": str(error)},
                f"config error: {error}",
            )
            return 2

        resolved_base_url = base_url or config.base_url
        resolved_token_env = resolved_token_env or config.internal_token_env

    resolved_token_env = resolved_token_env or DEFAULT_TOKEN_ENV

    if not resolved_base_url:
        emit(
            {
                "ok": False,
                "stage": "config",
                "error": "base_url is required (use --config or --base-url)",
            },
            "base_url is required: pass --config or --base-url",
        )
        return 2

    try:
        token = resolve_publish_token(resolved_token_env, environ)
    except ConfigError as error:
        emit(
            {
                "ok": False,
                "stage": "auth",
                "error": str(error),
            },
            f"cannot publish: {error}",
        )
        return 2

    try:
        captures = parse_captures(document)
        payloads, notes = build_import_payloads(captures)
    except CaptureError as error:
        emit(
            {"ok": False, "stage": "capture", "error": str(error)},
            f"capture rejected: {error}",
        )
        return 2

    if dry_run:
        emit(
            {
                "ok": True,
                "dry_run": True,
                "submitted": len(payloads),
                "jobs": payloads,
                "notes": notes,
            },
            f"[dry-run] {len(payloads)} capture(s) validated; nothing sent",
        )

        emit_notes(notes)

        return 0

    try:
        outcome = ManualPublisher(
            base_url=resolved_base_url,
            token=token,
            transport=transport,
        ).publish(payloads, notes=notes)
    except PublishError as error:
        emit(
            {"ok": False, "stage": "publish", "error": str(error)},
            f"publish failed: {error}",
        )
        return 1

    emit(
        {
            "ok": outcome.ok,
            "submitted": outcome.submitted,
            "created": outcome.created,
            "updated": outcome.updated,
            "total": outcome.total,
        },
        f"published {outcome.submitted} capture(s): "
        f"created={outcome.created} updated={outcome.updated} "
        f"total={outcome.total}",
    )

    emit_notes(outcome.notes)

    return 0


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

    bookmarklet = subparsers.add_parser(
        "capture-bookmarklet",
        help="print the credential-free manual-capture bookmarklet",
    )
    bookmarklet.add_argument(
        "--html",
        action="store_true",
        help="print a small help page containing the bookmarklet link",
    )
    bookmarklet.add_argument(
        "--js",
        action="store_true",
        help="print the raw JavaScript instead of the javascript: URL",
    )

    publish = subparsers.add_parser(
        "publish-capture",
        help=(
            "validate and publish a local capture file through the existing "
            "manual import API (source=\"manual\")"
        ),
    )
    publish.add_argument(
        "--file",
        required=True,
        help="capture JSON file, or '-' to read it from standard input",
    )
    publish.add_argument(
        "--config",
        default=None,
        help=(
            "optional YAML config; supplies jobpilot.base_url and "
            "jobpilot.internal_token_env"
        ),
    )
    publish.add_argument(
        "--base-url",
        default=None,
        help="internal API base URL when no --config is given",
    )
    publish.add_argument(
        "--token-env",
        default=None,
        help=(
            "name of the environment variable holding the internal token "
            "(the token itself is never a command-line argument); defaults to "
            "the --config value or INTERNAL_API_TOKEN"
        ),
    )
    publish.add_argument(
        "--dry-run",
        action="store_true",
        help="validate and print the payload without calling the API",
    )
    publish.add_argument(
        "--json",
        action="store_true",
        help="print a machine-readable result object",
    )

    return parser


def main(argv: Sequence[str] | None = None) -> int:
    parser = build_parser()
    arguments = parser.parse_args(argv)

    if arguments.command == "capture-bookmarklet":
        return run_capture_bookmarklet(
            as_html=arguments.html,
            raw_js=arguments.js,
        )

    if arguments.command == "publish-capture":
        return run_publish_capture(
            path=arguments.file,
            config_path=arguments.config,
            base_url=arguments.base_url,
            token_env=arguments.token_env,
            dry_run=arguments.dry_run,
            as_json=arguments.json,
        )

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
