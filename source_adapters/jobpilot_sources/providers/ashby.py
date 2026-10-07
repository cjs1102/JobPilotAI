"""Ashby public job board adapter.

Endpoint: ``GET https://api.ashbyhq.com/posting-api/job-board/{board}?includeCompensation=true``
Response: ``{"apiVersion": "...", "jobs": [...]}``; the response is the
complete public board by contract (ExecPlan 0012 section 6).

Unlisted postings (``isListed`` is explicitly false) are not part of the public
board and are dropped. Salary is only published when the provider exposes a
machine-readable monthly amount; Ashby's compensation summary is a display
string, so amounts are omitted instead of guessed.
"""

from __future__ import annotations

from typing import Any, Mapping

from ..normalizer import (
    build_job,
    clean_multiline,
    clean_text,
    html_to_text,
    parse_iso8601_utc,
)
from .base import BoardResult, ProviderError

PROVIDER = "ashby"
BOARD_URL = (
    "https://api.ashbyhq.com/posting-api/job-board/"
    "{board}?includeCompensation=true"
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
            "ashby payload must be a JSON object"
        )

    raw_jobs = payload.get("jobs")

    if not isinstance(raw_jobs, list):
        raise ProviderError(
            "ashby payload has no 'jobs' array"
        )

    listed = [
        raw
        for raw in raw_jobs
        if isinstance(raw, Mapping)
        and raw.get("isListed") is not False
    ]

    jobs: list[dict[str, Any]] = []

    for raw in listed:
        job = _normalize_job(
            raw,
            board=board,
            fallback_company=company,
        )

        if job is not None:
            jobs.append(job)

    return BoardResult(
        source=PROVIDER,
        board=board,
        jobs=jobs,
        complete=True,
        total=len(listed),
    )


def _normalize_job(
    raw: Any,
    *,
    board: str,
    fallback_company: str | None,
) -> dict[str, Any] | None:
    if not isinstance(raw, Mapping):
        return None

    source_url = clean_text(raw.get("jobUrl"))
    title = clean_text(raw.get("title"))

    if source_url is None or title is None:
        return None

    company = clean_text(fallback_company) or board
    description = clean_multiline(
        raw.get("descriptionPlain")
    )

    if description is None:
        description = html_to_text(
            raw.get("descriptionHtml")
        )

    return build_job(
        source=PROVIDER,
        source_url=source_url,
        title=title,
        company=company,
        external_id=raw.get("id"),
        published_at=parse_iso8601_utc(
            raw.get("publishedAt")
        ),
        location=raw.get("location"),
        description=description,
    )
