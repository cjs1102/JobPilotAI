"""Beijing-time timestamp normalization (ExecPlan 0014 F13, docs/03 section 9.1).

**The only authority for deadline semantics in Python.** The C++ side implements
the identical convention; see ``docs/03-database-design.md`` section 9.1.

Rules:

* Recruitment deadlines are interpreted as **Asia/Shanghai (UTC+8)** time. The
  offset is modelled as a fixed ``timedelta(hours=8)`` with the timezone *name*
  ``Asia/Shanghai``: China has had no DST since 1991, and a fixed offset is
  independent of the host's ``TZ``/tzdata. No function here ever calls
  ``datetime.now()``/``time.localtime``/``time.mktime`` on local time.
* A value that carries **only a date** is stored as that Beijing calendar day's
  local **noon** converted to UTC, i.e. ``YYYY-MM-DD 04:00:00``, and is flagged
  ``date_only=True``. Noon is a storage convention that keeps the calendar date
  stable across the UTC round-trip; it is **not** an expiry instant.
* A value that carries an **explicit time** keeps the real instant, converted
  from its own offset (or from ``source_tz`` when no offset is given), and is
  flagged ``date_only=False``. It is never rewritten to noon.
* Anything ambiguous (for example a bare ``10月8日`` with no year) is omitted:
  ``None``. Nothing is ever fabricated and no server-local timezone is used.

Canonical stored form (identical to ``published_at``): ``"%Y-%m-%d %H:%M:%S"``
in UTC.
"""

from __future__ import annotations

import re
from dataclasses import dataclass
from datetime import date, datetime, time, timedelta, timezone

BEIJING_TZ_NAME = "Asia/Shanghai"
#: Fixed +08:00 with the authoritative timezone name for documentation/debug.
BEIJING_TZ = timezone(timedelta(hours=8), BEIJING_TZ_NAME)
BEIJING_UTC_OFFSET_SECONDS = 8 * 60 * 60

DB_TIMESTAMP_FORMAT = "%Y-%m-%d %H:%M:%S"
DATE_FORMAT = "%Y-%m-%d"
DATE_ONLY_STORED_TIME = "04:00:00"
DATE_ONLY_BEIJING_HOUR = 12

_DATE_ONLY_PATTERNS = (
    re.compile(r"^(?P<year>\d{4})-(?P<month>\d{1,2})-(?P<day>\d{1,2})$"),
    re.compile(r"^(?P<year>\d{4})/(?P<month>\d{1,2})/(?P<day>\d{1,2})$"),
    re.compile(r"^(?P<year>\d{4})\.(?P<month>\d{1,2})\.(?P<day>\d{1,2})$"),
    re.compile(
        r"^(?P<year>\d{4})\s*年\s*(?P<month>\d{1,2})\s*月\s*"
        r"(?P<day>\d{1,2})\s*日?$"
    ),
)

_EXPLICIT_PATTERNS = (
    re.compile(
        r"^(?P<year>\d{4})[-/.](?P<month>\d{1,2})[-/.](?P<day>\d{1,2})"
        r"[ T]+(?P<hour>\d{1,2}):(?P<minute>\d{1,2})"
        r"(?::(?P<second>\d{1,2}))?$"
    ),
    re.compile(
        r"^(?P<year>\d{4})\s*年\s*(?P<month>\d{1,2})\s*月\s*"
        r"(?P<day>\d{1,2})\s*日\s*(?P<hour>\d{1,2}):(?P<minute>\d{1,2})"
        r"(?::(?P<second>\d{1,2}))?$"
    ),
)

_RELATIVE_PATTERNS = (
    re.compile(r"^刚刚$|^刚发布$|^最新$"),
    re.compile(r"^(?P<amount>\d+)\s*秒前$"),
    re.compile(r"^(?P<amount>\d+)\s*分钟前$"),
    re.compile(r"^(?P<amount>\d+)\s*小时前$"),
    re.compile(r"^(?P<amount>\d+)\s*天前$"),
)


@dataclass(frozen=True)
class NormalizedTimestamp:
    """One normalized value plus whether it came from a date-only source."""

    utc: str
    date_only: bool


def beijing_date_to_stored_utc(day: date) -> str:
    """Store a Beijing calendar day as its local noon converted to UTC."""

    if isinstance(day, datetime):
        raise TypeError("expected a date, not a datetime")

    local_noon = datetime.combine(
        day, time(hour=DATE_ONLY_BEIJING_HOUR), tzinfo=BEIJING_TZ
    )

    return local_noon.astimezone(timezone.utc).strftime(DB_TIMESTAMP_FORMAT)


def stored_utc_to_beijing(text: str) -> datetime | None:
    """Read a stored UTC timestamp back as a Beijing-local datetime."""

    parsed = _parse_stored_utc(text)

    if parsed is None:
        return None

    return parsed.astimezone(BEIJING_TZ)


def stored_utc_to_beijing_date(text: str) -> str | None:
    """Read a stored UTC timestamp back as its Beijing calendar date."""

    local = stored_utc_to_beijing(text)

    if local is None:
        return None

    return local.strftime(DATE_FORMAT)


def is_date_only_stored(text: str) -> bool:
    """True when the stored value carries the date-only noon convention."""

    parsed = _parse_stored_utc(text)

    if parsed is None:
        return False

    return parsed.astimezone(timezone.utc).strftime("%H:%M:%S") == (
        DATE_ONLY_STORED_TIME
    )


def is_valid_utc_timestamp(text: object) -> bool:
    """True for a stored ``YYYY-MM-DD HH:MM:SS`` UTC timestamp string."""

    return _parse_stored_utc(text) is not None


def normalize_timestamp(
    value: object,
    *,
    source_tz: timezone | None = None,
    now: datetime | None = None,
) -> NormalizedTimestamp | None:
    """Normalize a date or datetime value into the stored UTC convention."""

    tz = source_tz or BEIJING_TZ

    if isinstance(value, datetime):
        return NormalizedTimestamp(
            utc=_to_utc_text(value, tz),
            date_only=False,
        )

    if isinstance(value, date):
        return NormalizedTimestamp(
            utc=beijing_date_to_stored_utc(value),
            date_only=True,
        )

    if isinstance(value, bool) or not isinstance(value, str):
        return None

    text = value.strip()

    if not text:
        return None

    date_only = _parse_date_only(text)

    if date_only is not None:
        return NormalizedTimestamp(
            utc=beijing_date_to_stored_utc(date_only),
            date_only=True,
        )

    explicit = _parse_explicit(text, tz)

    if explicit is not None:
        return NormalizedTimestamp(
            utc=explicit.astimezone(timezone.utc).strftime(
                DB_TIMESTAMP_FORMAT
            ),
            date_only=False,
        )

    relative = _parse_relative(text, now)

    if relative is not None:
        return NormalizedTimestamp(
            utc=relative.astimezone(timezone.utc).strftime(
                DB_TIMESTAMP_FORMAT
            ),
            date_only=False,
        )

    return None


def normalize_deadline(
    value: object,
    *,
    source_tz: timezone | None = None,
    now: datetime | None = None,
) -> NormalizedTimestamp | None:
    """F13 entry point for ``deadline`` (alias of :func:`normalize_timestamp`)."""

    return normalize_timestamp(value, source_tz=source_tz, now=now)


def normalize_published_at(
    value: object,
    *,
    source_tz: timezone | None = None,
    now: datetime | None = None,
) -> str | None:
    """Normalize ``published_at`` to stored UTC text (no kind flag exists).

    A date-only publication time uses the same Beijing-noon convention so the
    Beijing calendar day is preserved under the day-granularity
    ``posted_within_days`` filter.
    """

    normalized = normalize_timestamp(value, source_tz=source_tz, now=now)

    return None if normalized is None else normalized.utc


def parse_epoch_millis_utc(value: object) -> str | None:
    """Absolute epoch-millisecond value -> UTC text (explicit instant)."""

    if isinstance(value, bool) or not isinstance(value, (int, float)):
        return None

    try:
        moment = datetime.fromtimestamp(value / 1000.0, tz=timezone.utc)
    except (OverflowError, OSError, ValueError):
        return None

    return moment.strftime(DB_TIMESTAMP_FORMAT)


def _to_utc_text(value: datetime, tz: timezone) -> str:
    if value.tzinfo is None:
        value = value.replace(tzinfo=tz)

    return value.astimezone(timezone.utc).strftime(DB_TIMESTAMP_FORMAT)


def _parse_stored_utc(text: object) -> datetime | None:
    if not isinstance(text, str):
        return None

    try:
        return datetime.strptime(text.strip(), DB_TIMESTAMP_FORMAT).replace(
            tzinfo=timezone.utc
        )
    except ValueError:
        return None


def _safe_date(year: int, month: int, day: int) -> date | None:
    try:
        return date(year, month, day)
    except ValueError:
        return None


def _parse_date_only(text: str) -> date | None:
    for pattern in _DATE_ONLY_PATTERNS:
        match = pattern.match(text)

        if match is not None:
            return _safe_date(
                int(match.group("year")),
                int(match.group("month")),
                int(match.group("day")),
            )

    return None


def _parse_explicit(text: str, tz: timezone) -> datetime | None:
    for pattern in _EXPLICIT_PATTERNS:
        match = pattern.match(text)

        if match is None:
            continue

        day = _safe_date(
            int(match.group("year")),
            int(match.group("month")),
            int(match.group("day")),
        )

        if day is None:
            return None

        hour = int(match.group("hour"))
        minute = int(match.group("minute"))
        second = int(match.group("second") or 0)

        if hour > 23 or minute > 59 or second > 59:
            return None

        return datetime(
            day.year,
            day.month,
            day.day,
            hour,
            minute,
            second,
            tzinfo=tz,
        )

    # ISO-8601 with an explicit offset, e.g. 2026-10-08T18:30:00+08:00 / Z.
    raw = text

    if raw.endswith(("Z", "z")):
        raw = raw[:-1] + "+00:00"

    if "T" in raw or "+" in raw[10:] or raw.count("-") > 3:
        try:
            parsed = datetime.fromisoformat(raw)
        except ValueError:
            return None

        if parsed.tzinfo is None:
            parsed = parsed.replace(tzinfo=tz)

        return parsed

    return None


def _parse_relative(text: str, now: datetime | None) -> datetime | None:
    if now is None:
        return None

    if now.tzinfo is None:
        return None

    if _RELATIVE_PATTERNS[0].match(text):
        return now

    units = (
        (_RELATIVE_PATTERNS[1], 1.0),
        (_RELATIVE_PATTERNS[2], 60.0),
        (_RELATIVE_PATTERNS[3], 3600.0),
        (_RELATIVE_PATTERNS[4], 86400.0),
    )

    for pattern, seconds in units:
        match = pattern.match(text)

        if match is None:
            continue

        amount = int(match.group("amount"))
        return now - timedelta(seconds=amount * seconds)

    return None
