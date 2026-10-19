"""Lever public postings adapter.

Endpoint: ``GET https://api.lever.co/v0/postings/{board}?mode=json``
Response: a JSON array of postings; the array is the complete board by contract
(ExecPlan 0012 section 6).

Salary note: Lever reports absolute yearly amounts in the provider currency,
while the platform column contract is monthly thousands of the platform
currency. Converting would fabricate data, so amounts are omitted until a unit
mapping is agreed.
"""

from __future__ import annotations

from typing import Any, Mapping

from ..normalizer import (
    build_job,
    clean_multiline,
    clean_text,
    parse_epoch_millis_utc,
)
from .base import BoardResult, ProviderError

PROVIDER = "lever"
BOARD_URL = (
    "https://api.lever.co/v0/postings/{board}?mode=json"
)


def board_url(board: str) -> str:
    return BOARD_URL.format(board=board)


def parse_board(
    payload: Any,
    *,
    board: str,
    company: str | None = None,
) -> BoardResult:
    if not isinstance(payload, list):
        raise ProviderError(
            "lever payload must be a JSON array"
        )

    jobs: list[dict[str, Any]] = []

    for raw in payload:
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
        total=len(payload),
    )


def _normalize_job(
    raw: Any,
    *,
    board: str,
    fallback_company: str | None,
) -> dict[str, Any] | None:
    if not isinstance(raw, Mapping):
        return None

    source_url = clean_text(raw.get("hostedUrl"))
    title = clean_text(raw.get("text"))

    if source_url is None or title is None:
        return None

    location = None
    categories = raw.get("categories")

    if isinstance(categories, Mapping):
        location = categories.get("location")

    company = clean_text(fallback_company) or board
    description = clean_multiline(
        raw.get("descriptionPlain")
    )

    if description is None:
        description = clean_multiline(
            raw.get("openingPlain")
        )

    return build_job(
        source=PROVIDER,
        source_url=source_url,
        title=title,
        company=company,
        external_id=raw.get("id"),
        published_at=parse_epoch_millis_utc(
            raw.get("createdAt")
        ),
        location=location,
        description=description,
        apply_url=raw.get("applyUrl"),
    )
