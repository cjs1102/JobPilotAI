"""Greenhouse public board adapter.

Endpoint: ``GET https://boards-api.greenhouse.io/v1/boards/{board}/jobs?content=true``
Response: ``{"jobs": [...], "meta": {"total": N}}``.

Completeness depends on the provider total: the observation counts as complete
only when ``meta.total`` is present and equals the number of returned rows
(ExecPlan 0012 section 6).
"""

from __future__ import annotations

from typing import Any, Mapping

from ..normalizer import (
    build_job,
    clean_text,
    html_to_text,
    parse_iso8601_utc,
)
from .base import BoardResult, ProviderError

PROVIDER = "greenhouse"
BOARD_URL = (
    "https://boards-api.greenhouse.io/v1/boards/"
    "{board}/jobs?content=true"
)


def board_url(board: str) -> str:
    return BOARD_URL.format(board=board)


def parse_board(
    payload: Any,
    *,
    board: str,
    company: str | None = None,
) -> BoardResult:
    if not isinstance(payload, Mapping):
        raise ProviderError(
            "greenhouse payload must be a JSON object"
        )

    raw_jobs = payload.get("jobs")

    if not isinstance(raw_jobs, list):
        raise ProviderError(
            "greenhouse payload has no 'jobs' array"
        )

    meta = payload.get("meta")
    raw_total = None

    if isinstance(meta, Mapping):
        candidate = meta.get("total")

        if isinstance(candidate, int) and not isinstance(
            candidate,
            bool,
        ):
            raw_total = candidate

    jobs: list[dict[str, Any]] = []

    for raw in raw_jobs:
        job = _normalize_job(
            raw,
            board=board,
            fallback_company=company,
        )

        if job is not None:
            jobs.append(job)

    complete = (
        raw_total is not None and raw_total == len(raw_jobs)
    )

    return BoardResult(
        source=PROVIDER,
        board=board,
        jobs=jobs,
        complete=complete,
        total=raw_total if raw_total is not None else len(raw_jobs),
    )


def _normalize_job(
    raw: Any,
    *,
    board: str,
    fallback_company: str | None,
) -> dict[str, Any] | None:
    if not isinstance(raw, Mapping):
        return None

    source_url = clean_text(raw.get("absolute_url"))
    title = clean_text(raw.get("title"))

    if source_url is None or title is None:
        # Without the contract fields the row cannot be published; skip it
        # instead of inventing a URL or title.
        return None

    location = None
    raw_location = raw.get("location")

    if isinstance(raw_location, Mapping):
        location = raw_location.get("name")

    company_name = clean_text(raw.get("company_name"))
    company = (
        company_name
        or clean_text(fallback_company)
        or board
    )

    return build_job(
        source=PROVIDER,
        source_url=source_url,
        title=title,
        company=company,
        external_id=raw.get("id"),
        published_at=parse_iso8601_utc(
            raw.get("first_published")
        ),
        location=location,
        description=html_to_text(raw.get("content")),
    )
