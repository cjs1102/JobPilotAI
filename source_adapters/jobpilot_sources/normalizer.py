"""Deterministic normalization helpers for ATS payloads.

MVP rule (ExecPlan 0012 section 6): never fabricate missing salary, location,
or publication time. Every helper returns ``None`` when the value cannot be
derived from the provider payload.
"""

from __future__ import annotations

import html
import re
from datetime import datetime, timezone
from typing import Any, Iterable

from .normalize_cn import MAX_EXPERIENCE_YEARS
from .timestamps import (
    NormalizedTimestamp,
    is_date_only_stored,
    is_valid_utc_timestamp,
)

DB_TIMESTAMP_FORMAT = "%Y-%m-%d %H:%M:%S"

_SCRIPT_RE = re.compile(r"(?is)<(script|style)[^>]*>.*?</\1>")
_TAG_RE = re.compile(r"(?s)<[^>]+>")
_BREAK_RE = re.compile(r"(?i)<br\s*/?>")
_BLOCK_END_RE = re.compile(r"(?i)</(?:p|div|li|ul|ol|h[1-6]|tr|td)>")
_WHITESPACE_RE = re.compile(r"[ \t\r\f\v]+")


def clean_text(value: Any) -> str | None:
    """Trim and collapse interior whitespace; return None for empty input."""

    if not isinstance(value, str):
        return None

    collapsed = _WHITESPACE_RE.sub(" ", value.replace("\n", " ")).strip()
    return collapsed or None


def html_to_text(value: Any) -> str | None:
    """Convert a provider HTML blob into plain text without inventing content."""

    if not isinstance(value, str) or not value.strip():
        return None

    text = _SCRIPT_RE.sub(" ", value)
    text = _BREAK_RE.sub("\n", text)
    text = _BLOCK_END_RE.sub("\n", text)
    text = _TAG_RE.sub(" ", text)
    text = html.unescape(text)

    lines = [
        re.sub(r"\s+", " ", line).strip()
        for line in text.splitlines()
    ]

    collapsed = "\n".join(line for line in lines if line)
    return collapsed or None


def parse_iso8601_utc(value: Any) -> str | None:
    """Parse an ISO-8601 timestamp and render it as UTC 'YYYY-MM-DD HH:MM:SS'.

    A timestamp without an offset is treated as UTC so stored values stay
    comparable (ExecPlan 0012 section 16, time-zone drift).
    """

    if not isinstance(value, str) or not value.strip():
        return None

    raw = value.strip()

    if raw.endswith(("Z", "z")):
        raw = raw[:-1] + "+00:00"

    try:
        parsed = datetime.fromisoformat(raw)
    except ValueError:
        return None

    if parsed.tzinfo is None:
        parsed = parsed.replace(tzinfo=timezone.utc)

    return parsed.astimezone(timezone.utc).strftime(DB_TIMESTAMP_FORMAT)


def parse_epoch_millis_utc(value: Any) -> str | None:
    """Parse an epoch-milliseconds integer and render it as UTC text."""

    if isinstance(value, bool) or not isinstance(value, (int, float)):
        return None

    try:
        moment = datetime.fromtimestamp(value / 1000.0, tz=timezone.utc)
    except (OverflowError, OSError, ValueError):
        return None

    return moment.strftime(DB_TIMESTAMP_FORMAT)


def first_present(*values: Any) -> Any:
    """Return the first value that is neither None nor an empty string."""

    for value in values:
        if value is None:
            continue

        if isinstance(value, str) and not value.strip():
            continue

        return value

    return None


def as_unsigned_int(value: Any) -> int | None:
    """Return a non-negative int for integral provider numbers, else None."""

    if isinstance(value, bool) or not isinstance(value, (int, float)):
        return None

    if isinstance(value, float) and not value.is_integer():
        return None

    integer = int(value)

    if integer < 0:
        return None

    return integer


def clean_identifier(value: Any) -> str | None:
    """Identifiers arrive as strings or integers; keep them as text."""

    if isinstance(value, bool):
        return None

    if isinstance(value, int):
        return str(value)

    return clean_text(value)


def clean_multiline(value: Any) -> str | None:
    """Normalize a plain-text blob while keeping paragraph breaks."""

    if not isinstance(value, str):
        return None

    normalized = value.replace("\r\n", "\n").replace("\r", "\n")
    collapsed = "\n".join(
        line.rstrip() for line in normalized.split("\n")
    ).strip()

    return collapsed or None


def build_job(
    *,
    source: str,
    source_url: str,
    title: str,
    company: str,
    external_id: Any = None,
    published_at: Any = None,
    location: Any = None,
    salary_min: Any = None,
    salary_max: Any = None,
    salary_currency: Any = None,
    description: Any = None,
    requirements: Iterable[dict[str, Any]] | None = None,
    education_required: Any = None,
    experience_years_min: Any = None,
    experience_years_max: Any = None,
    job_nature: Any = None,
    deadline: Any = None,
    deadline_date_only: Any = None,
) -> dict[str, Any]:
    """Build the source-sync job object, omitting every unknown field.

    ``company``, ``title``, ``source`` and ``source_url`` are mandatory for the
    platform contract; the remaining keys are only emitted when known.

    The 0014 keys mirror the C++ validation domain (``docs/03`` section 9.2), so
    a payload produced here can never be rejected for a value this function
    emitted: enums stay inside ``1..5``/``1..3``, experience years are
    non-negative integers ``<= 32767`` with ``min <= max``, and ``deadline`` is a
    stored UTC timestamp whose ``deadline_date_only`` kind is always definite
    (F13). A contradictory ``deadline``/``deadline_date_only`` pair is dropped
    entirely instead of being sent as a fabricated instant.
    """

    job: dict[str, Any] = {
        "company": company,
        "title": title,
        "source": source,
        "source_url": source_url,
        "requirements": list(requirements or ()),
    }

    optional = {
        "external_id": clean_identifier(external_id),
        "published_at": published_at,
        "location": clean_text(location),
        "description": description,
        "salary_currency": clean_text(salary_currency),
    }

    for key, value in optional.items():
        if value is not None:
            job[key] = value

    minimum = as_unsigned_int(salary_min)
    maximum = as_unsigned_int(salary_max)

    if minimum is not None:
        job["salary_min"] = minimum

    if maximum is not None:
        job["salary_max"] = maximum

    education = as_unsigned_int(education_required)

    if education is not None and 1 <= education <= 5:
        job["education_required"] = education

    nature = as_unsigned_int(job_nature)

    if nature is not None and 1 <= nature <= 3:
        job["job_nature"] = nature

    years_min = as_unsigned_int(experience_years_min)
    years_max = as_unsigned_int(experience_years_max)

    if years_min is not None and years_min > MAX_EXPERIENCE_YEARS:
        years_min = None

    if years_max is not None and years_max > MAX_EXPERIENCE_YEARS:
        years_max = None

    if (
        years_min is not None
        and years_max is not None
        and years_max < years_min
    ):
        # An inverted pair is unusable; sending it would only be rejected.
        years_min = None
        years_max = None

    if years_min is not None:
        job["experience_years_min"] = years_min

    if years_max is not None:
        job["experience_years_max"] = years_max

    stored_deadline, stored_kind = deadline_pair(
        deadline,
        deadline_date_only,
    )

    if stored_deadline is not None and stored_kind is not None:
        job["deadline"] = stored_deadline
        job["deadline_date_only"] = stored_kind

    return job


def deadline_pair(
    deadline: Any,
    deadline_date_only: Any = None,
) -> tuple[str | None, bool | None]:
    """Normalize a ``(utc, date_only)`` pair, or ``(None, None)`` when unusable.

    Accepts a :class:`~jobpilot_sources.timestamps.NormalizedTimestamp`, a
    ``(utc, date_only)`` pair, or a stored UTC string plus a flag. The wire
    protocol always carries a definite kind next to a present deadline, so an
    omitted flag means "explicit instant"; a date-only flag whose value does not
    carry the Beijing-noon convention is contradictory and drops the pair.
    """

    value: Any = deadline
    kind: Any = deadline_date_only

    if isinstance(deadline, NormalizedTimestamp):
        value = deadline.utc
        kind = deadline.date_only
    elif isinstance(deadline, (tuple, list)) and len(deadline) == 2:
        value, kind = deadline

    if not isinstance(value, str) or not is_valid_utc_timestamp(value):
        return None, None

    if kind is None:
        kind = False

    if not isinstance(kind, bool):
        return None, None

    if kind and not is_date_only_stored(value):
        return None, None

    return value, kind
