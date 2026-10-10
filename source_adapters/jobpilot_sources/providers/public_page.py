"""Generic ``public_page`` source: one public, login-free list page per board.

This is the framework-level adapter that Step 3 of ExecPlan 0014 needs in order
to prove the stack from a recorded public response through to a source-sync
payload. Per-source China adapters (Step 5) reuse the same extractors with their
own registered source id.

**Registration status.** ``PROVIDER = "public_page"`` is intentionally *not*
listed in ``config.SUPPORTED_PROVIDERS`` or ``providers.PROVIDERS`` yet. The
allowlist is a four-place contract (``docs/04`` section 5.5): adding the
identifier here without the same identifier in the C++ runtime allowlist
(``IsSupportedObservationSource``) would make every publish fail with ``400``.
Until that coordinated change lands, this module is exercised by fixture tests
only. Nothing is half-enabled at runtime.

Completeness: only a JSON payload that states a total equal to the returned row
count proves the board was fully read (the Greenhouse rule). An HTML/JSON-LD/feed
page can never prove completeness, so it is always reported with
``complete=false``; a partial observation can therefore never close jobs.

Structured skill lists are dropped: the source gives skill names but no level,
and the platform's level column means something specific (1 = 了解), so writing
one would fabricate a requirement.

Two different byte limits are easy to confuse here; they are not the same thing:

- :data:`MAX_DESCRIPTION_BYTES` (48 KiB = 49,152 bytes) caps the **final job
  description** that would be stored, judged in UTF-8 bytes. An over-cap
  description is a hard 0015A ``description_too_large`` rejection carrying
  ``limit_bytes``/``actual_bytes``: the observation fails with a diagnostic and
  nothing is published. It is never truncated, and the row is never published
  with the description silently dropped.
- ``fetch.DEFAULT_MAX_RESPONSE_BYTES`` (2 MiB) is the **raw response-body**
  safety budget for the 0012 fetch path. It is an independent, much larger
  limit and is deliberately left alone.
"""

from __future__ import annotations

import json
import urllib.parse
from typing import Any, Mapping, Sequence

from .. import normalize_cn
from ..feeds import looks_like_feed, looks_like_sitemap, parse_feed
from ..htmljob import find_job_postings, json_ld_to_job_fields
from ..normalizer import (
    build_job,
    clean_identifier,
    clean_text,
    first_present,
    html_to_text,
)
from ..salary import parse_monthly_salary_thousand, parse_salary_currency
from ..timestamps import BEIJING_TZ, normalize_published_at
from .base import BoardResult, ProviderError

PROVIDER = "public_page"
FETCH_MODE = "text"

# Column byte limits from docs/03 section 3. An over-long optional value is
# omitted (it cannot be stored and truncating it would invent a different
# value); a row whose mandatory values exceed their limit is skipped, because
# publishing it would fail the whole observation with a 400.
MAX_COMPANY_BYTES = 128
MAX_TITLE_BYTES = 128
MAX_LOCATION_BYTES = 64
#: 0015A decision 5 / docs/04 section 5.1: the FINAL description (after
#: ``html_to_text``) is capped at 48 KiB of UTF-8 **bytes**, judged with
#: ``len(value.encode("utf-8"))``. This is NOT the raw-page safety budget:
#: that one is ``fetch.DEFAULT_MAX_RESPONSE_BYTES`` (2 MiB, an unrelated
#: response-body limit) and must never be lowered to match this number.
#: Over-cap content is rejected with ``description_too_large`` plus
#: ``limit_bytes``/``actual_bytes`` and nothing is published - it is never
#: truncated and never silently dropped field-by-field.
MAX_DESCRIPTION_BYTES = 48 * 1024
DESCRIPTION_TOO_LARGE_CODE = "description_too_large"

_TITLE_KEYS = ("title", "jobTitle", "jobName", "positionName", "name")
_URL_KEYS = (
    "source_url",
    "sourceUrl",
    "url",
    "link",
    "detailUrl",
    "detail_url",
    "applyUrl",
    "apply_url",
    "jobUrl",
    "positionUrl",
)
_COMPANY_KEYS = ("company", "companyName", "company_name", "employer", "firm")
_CITY_KEYS = ("city", "cityName", "workCity", "locationCity")
_DISTRICT_KEYS = ("district", "area", "county")
_LOCATION_KEYS = ("location", "workLocation", "address", "jobLocation")
_DESCRIPTION_KEYS = ("description", "jobDescription", "content", "detail", "jd")
_PUBLISHED_KEYS = (
    "published_at",
    "publishedAt",
    "postedAt",
    "postTime",
    "publishTime",
    "datePosted",
    "createTime",
    "createdAt",
)
_DEADLINE_KEYS = (
    "deadline",
    "deadlineDate",
    "validThrough",
    "closeTime",
    "applyDeadline",
    "endTime",
)
_SALARY_TEXT_KEYS = ("salary", "salaryText", "salaryRange", "compensation", "pay")
_EDUCATION_KEYS = ("education", "educationRequired", "educationLevel", "degree")
_NATURE_KEYS = ("jobNature", "jobType", "nature", "recruitType", "positionType")
_EXPERIENCE_KEYS = ("experience", "experienceRequired", "workYears", "yearsOfExperience")
_TOTAL_KEYS = ("total", "totalCount", "count", "totalNum")
_ROW_CONTAINER_KEYS = ("jobs", "data", "list", "items", "results", "records", "rows")


def board_url(board: str) -> str:
    """A ``public_page`` board is the absolute list URL itself."""

    if not isinstance(board, str) or not board.strip():
        raise ProviderError("public_page board must be an absolute URL")

    candidate = board.strip()
    parts = urllib.parse.urlsplit(candidate)

    if parts.scheme not in {"http", "https"} or not parts.netloc:
        raise ProviderError(
            f"public_page board must be an absolute http(s) URL: {board!r}"
        )

    return candidate


def parse_board(
    payload: Any,
    *,
    board: str,
    company: str | None = None,
) -> BoardResult:
    """Normalize one public page (HTML, JSON, feed or sitemap) into jobs."""

    if not isinstance(payload, str):
        # A JSON document that the caller already parsed is accepted too.
        if isinstance(payload, Mapping):
            jobs, total = _jobs_from_mapping_document(
                payload, board=board, company=company
            )

            return _result(board, jobs, total)

        raise ProviderError(
            "public_page payload must be decoded text or a JSON object"
        )

    text = payload.strip()

    if not text:
        return BoardResult(source=PROVIDER, board=board, jobs=[], complete=False)

    if looks_like_sitemap(text):
        # A sitemap lists candidate pages; it does not itself contain jobs.
        return BoardResult(source=PROVIDER, board=board, jobs=[], complete=False)

    if looks_like_feed(text):
        jobs = _jobs_from_feed(text, board=board, company=company)
        return _result(board, jobs, None)

    if text.startswith(("{", "[")):
        try:
            document = json.loads(text)
        except json.JSONDecodeError:
            document = None

        if document is not None:
            if isinstance(document, Mapping):
                jobs, total = _jobs_from_mapping_document(
                    document, board=board, company=company
                )

                return _result(board, jobs, total)

            if isinstance(document, list):
                jobs = [
                    job
                    for job in (
                        _job_from_mapping(
                            row,
                            board=board,
                            company=company,
                        )
                        for row in document
                        if isinstance(row, Mapping)
                    )
                    if job is not None
                ]

                return _result(board, jobs, None)

    postings, _skipped = find_job_postings(text)
    jobs = [
        job
        for job in (
            _job_from_fields(
                json_ld_to_job_fields(posting),
                board=board,
                company=company,
            )
            for posting in postings
        )
        if job is not None
    ]

    return _result(board, jobs, None)


def _result(
    board: str,
    jobs: list[dict[str, Any]],
    total: int | None,
) -> BoardResult:
    complete = total is not None and total == len(jobs)

    return BoardResult(
        source=PROVIDER,
        board=board,
        jobs=jobs,
        complete=complete,
        total=total if total is not None else len(jobs),
    )


def _jobs_from_mapping_document(
    document: Mapping[str, Any],
    *,
    board: str,
    company: str | None,
) -> tuple[list[dict[str, Any]], int | None]:
    rows: Sequence[Any] = ()

    for key in _ROW_CONTAINER_KEYS:
        candidate = document.get(key)

        if isinstance(candidate, list):
            rows = candidate
            break

    if not rows and any(key in document for key in _TITLE_KEYS):
        rows = [document]

    total = None

    for key in _TOTAL_KEYS:
        candidate = document.get(key)

        if isinstance(candidate, int) and not isinstance(candidate, bool):
            total = candidate
            break

    jobs = [
        job
        for job in (
            _job_from_mapping(row, board=board, company=company)
            for row in rows
            if isinstance(row, Mapping)
        )
        if job is not None
    ]

    return jobs, total


def _jobs_from_feed(
    text: str,
    *,
    board: str,
    company: str | None,
) -> list[dict[str, Any]]:
    feed = parse_feed(text)
    jobs: list[dict[str, Any]] = []
    fallback_company = _bounded_text(
        clean_text(company), MAX_COMPANY_BYTES
    )

    for entry in feed.entries:
        title = _bounded_text(clean_text(entry.title), MAX_TITLE_BYTES)

        if title is None or fallback_company is None:
            # Without a title or a company the row cannot satisfy the platform
            # contract, so it is skipped instead of being invented.
            continue

        source_url = clean_text(entry.link)

        if source_url is None:
            continue

        jobs.append(
            build_job(
                source=PROVIDER,
                source_url=source_url,
                title=title,
                company=fallback_company,
                external_id=entry.identifier,
                published_at=entry.published,
                description=_bounded_description(entry.summary),
            )
        )

    return jobs


def _job_from_fields(
    fields: Mapping[str, Any],
    *,
    board: str,
    company: str | None,
) -> dict[str, Any] | None:
    title = _bounded_text(
        clean_text(first_present(*(fields.get(key) for key in _TITLE_KEYS))),
        MAX_TITLE_BYTES,
    )
    source_url = clean_text(first_present(*(fields.get(key) for key in _URL_KEYS)))

    if title is None or source_url is None:
        return None

    company_name = _bounded_text(
        first_present(
            first_present(*(fields.get(key) for key in _COMPANY_KEYS)),
            clean_text(company),
        ),
        MAX_COMPANY_BYTES,
    )

    if company_name is None or not isinstance(company_name, str):
        return None

    return _build(
        title=title,
        source_url=source_url,
        company=company_name,
        board=board,
        fields=fields,
    )


def _job_from_mapping(
    raw: Mapping[str, Any],
    *,
    board: str,
    company: str | None,
) -> dict[str, Any] | None:
    return _job_from_fields(raw, board=board, company=company)


def _build(
    *,
    title: str,
    source_url: str,
    company: str,
    board: str,
    fields: Mapping[str, Any],
) -> dict[str, Any]:
    location = normalize_cn.normalize_location(
        first_present(*(fields.get(key) for key in _CITY_KEYS)),
        first_present(*(fields.get(key) for key in _DISTRICT_KEYS)),
    )

    if location is None:
        location = normalize_cn.normalize_location_text(
            first_present(*(fields.get(key) for key in _LOCATION_KEYS))
        )

    location = _bounded_text(location, MAX_LOCATION_BYTES)

    salary_text = first_present(
        *(fields.get(key) for key in _SALARY_TEXT_KEYS)
    )
    salary = parse_monthly_salary_thousand(salary_text)
    salary_min = None
    salary_max = None

    if salary is not None:
        salary_min, salary_max = salary

    deadline = normalize_cn.normalize_deadline_fields(
        first_present(*(fields.get(key) for key in _DEADLINE_KEYS)),
        source_tz=BEIJING_TZ,
    )

    published = normalize_published_at(
        first_present(*(fields.get(key) for key in _PUBLISHED_KEYS))
    )

    experience = normalize_cn.normalize_experience_years(
        first_present(*(fields.get(key) for key in _EXPERIENCE_KEYS))
    )

    description = _bounded_description(
        html_to_text(
            first_present(*(fields.get(key) for key in _DESCRIPTION_KEYS))
        )
    )

    return build_job(
        source=PROVIDER,
        source_url=source_url,
        title=title,
        company=company,
        external_id=clean_identifier(
            first_present(
                fields.get("id"),
                fields.get("jobId"),
                fields.get("positionId"),
                fields.get("identifier"),
            )
        ),
        published_at=published,
        location=location,
        salary_min=salary_min,
        salary_max=salary_max,
        salary_currency=parse_salary_currency(salary_text),
        description=description,
        education_required=normalize_cn.normalize_education(
            first_present(*(fields.get(key) for key in _EDUCATION_KEYS))
        ),
        experience_years_min=(
            experience.minimum if experience is not None else None
        ),
        experience_years_max=(
            experience.maximum if experience is not None else None
        ),
        job_nature=normalize_cn.normalize_job_nature(
            first_present(*(fields.get(key) for key in _NATURE_KEYS))
        ),
        deadline=deadline,
    )


def _bounded_description(value: Any) -> str | None:
    """Bound the final description in UTF-8 bytes, or reject the observation.

    Missing/blank stays absent (the description is optional). Over the 48 KiB
    cap is a hard 0015A ``description_too_large`` rejection: the caller gets the
    stable code plus ``limit_bytes``/``actual_bytes`` and no jobs are published,
    because publishing the row without its description would silently drop the
    posting's body text - exactly what the contract forbids.
    """

    if not isinstance(value, str):
        return None

    cleaned = value.strip()

    if not cleaned:
        return None

    size = len(cleaned.encode("utf-8"))

    if size > MAX_DESCRIPTION_BYTES:
        raise ProviderError(
            "description_too_large: description is "
            f"{size} bytes, over the {MAX_DESCRIPTION_BYTES} byte cap "
            f"(limit_bytes={MAX_DESCRIPTION_BYTES}, actual_bytes={size}); "
            "the posting is rejected, not truncated",
            code=DESCRIPTION_TOO_LARGE_CODE,
            field="description",
            limit_bytes=MAX_DESCRIPTION_BYTES,
            actual_bytes=size,
        )

    return cleaned


def _bounded_text(value: Any, max_bytes: int) -> str | None:
    """Keep a text value only when it fits the column byte limit."""

    if not isinstance(value, str):
        return None

    cleaned = value.strip()

    if not cleaned:
        return None

    if len(cleaned.encode("utf-8")) > max_bytes:
        return None

    return cleaned


__all__ = [
    "DESCRIPTION_TOO_LARGE_CODE",
    "FETCH_MODE",
    "MAX_DESCRIPTION_BYTES",
    "PROVIDER",
    "board_url",
    "parse_board",
]
