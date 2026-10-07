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
) -> dict[str, Any]:
    """Build the source-sync job object, omitting every unknown field.

    ``company``, ``title``, ``source`` and ``source_url`` are mandatory for the
    platform contract; the remaining keys are only emitted when known.
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

    return job
