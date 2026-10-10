"""Deterministic Chinese field mapping for the China source framework.

Every function here maps a source string onto a platform enum or number, or
returns ``None``. **No inference happens**: a value the mapping does not know is
omitted so the C++ validator never sees a guess (ExecPlan 0014 section 5.1).

Enum ranges are the confirmed ``docs/03`` section 9.2 domains:

* ``education_required`` -- ``1不限 / 2大专 / 3本科 / 4硕士 / 5博士``
* ``job_nature`` -- ``1校招 / 2社招 / 3实习``
* ``experience_years_*`` -- non-negative integers, ``min <= max``, ``<= 32767``

``deadline`` follows the F13 convention implemented in
:mod:`jobpilot_sources.timestamps`: a date-only value becomes Beijing local noon
stored as UTC (``04:00:00``) with ``date_only=True``; an explicit time keeps the
real instant with ``date_only=False``.
"""

from __future__ import annotations

import re
from dataclasses import dataclass
from datetime import datetime, timezone

from .timestamps import (
    BEIJING_TZ,
    NormalizedTimestamp,
    normalize_deadline,
)

MAX_EXPERIENCE_YEARS = 32767

EDUCATION_UNLIMITED = 1
EDUCATION_COLLEGE = 2
EDUCATION_BACHELOR = 3
EDUCATION_MASTER = 4
EDUCATION_DOCTOR = 5

JOB_NATURE_CAMPUS = 1
JOB_NATURE_EXPERIENCED = 2
JOB_NATURE_INTERNSHIP = 3

_EDUCATION_RULES: tuple[tuple[tuple[str, ...], int], ...] = (
    (("博士",), EDUCATION_DOCTOR),
    (("硕士", "研究生", "硕研"), EDUCATION_MASTER),
    (("本科", "学士", "大学本科"), EDUCATION_BACHELOR),
    (("大专", "专科", "高职"), EDUCATION_COLLEGE),
    (("不限", "无要求", "学历不限"), EDUCATION_UNLIMITED),
)

# Chinese wording only: short English markers would match unrelated words
# ("intern" inside "internal", "graduate" inside "undergraduate"), which would
# be fabrication rather than a deterministic mapping.
_NATURE_INTERNSHIP_MARKERS = ("实习",)
_NATURE_CAMPUS_MARKERS = ("校招", "校园招聘", "应届")
_NATURE_EXPERIENCED_MARKERS = ("社招", "社会招聘")

_CITY_SUFFIX = "市"
#: Separators accepted between the city and the district parts. The hyphen is
#: our own canonical form, so an already-normalized "城市-区县" value goes
#: through the same city normalization as "城市·区县".
_LOCATION_SEPARATORS = re.compile(r"\s*[·・|/、,，\-—–]\s*")

_EXPERIENCE_RANGE_RE = re.compile(
    r"(?P<low>\d{1,3})\s*[-~—–至到]\s*(?P<high>\d{1,3})\s*年"
)
_EXPERIENCE_MIN_RE = re.compile(r"(?P<low>\d{1,3})\s*年(?:以上|及以上|起|\+)")
_EXPERIENCE_MAX_RE = re.compile(r"(?P<high>\d{1,3})\s*年(?:以下|以内|及以下)")
_EXPERIENCE_EXACT_RE = re.compile(r"^(?P<low>\d{1,3})\s*年$")

_FRESH_GRADUATE_MARKERS = ("应届", "无经验", "无需经验", "经验不限", "不限经验")
_NO_REQUIREMENT_MARKERS = ("不限", "无要求", "经验不限")

_DEADLINE_LABELS = (
    "截止时间",
    "报名截止",
    "投递截止",
    "申请截止",
    "截止日期",
    "截止",
    "deadline",
)


@dataclass(frozen=True)
class ExperienceYears:
    minimum: int | None = None
    maximum: int | None = None


def normalize_education(value: object) -> int | None:
    """Map a source education phrase onto ``1..5`` or ``None``."""

    cleaned = _clean(value)

    if cleaned is None:
        return None

    for markers, mapped in _EDUCATION_RULES:
        if any(marker in cleaned for marker in markers):
            return mapped

    return None


def normalize_job_nature(value: object) -> int | None:
    """Map a source job-nature phrase onto ``1..3`` or ``None``.

    Internship wording wins over campus wording when both appear ("校招实习"
    is an internship), and experienced-hire wording is only used when nothing
    else matches.
    """

    cleaned = _clean(value)

    if cleaned is None:
        return None

    if any(marker in cleaned for marker in _NATURE_INTERNSHIP_MARKERS):
        return JOB_NATURE_INTERNSHIP

    if any(marker in cleaned for marker in _NATURE_CAMPUS_MARKERS):
        return JOB_NATURE_CAMPUS

    if any(marker in cleaned for marker in _NATURE_EXPERIENCED_MARKERS):
        return JOB_NATURE_EXPERIENCED

    return None


def normalize_experience_years(value: object) -> ExperienceYears | None:
    """Map a Chinese experience requirement onto ``(min, max)`` years."""

    cleaned = _clean(value)

    if cleaned is None:
        return None

    if any(marker in cleaned for marker in ("应届",)):
        # A fresh-graduate posting requires no prior years, and the source
        # states no upper bound.
        return ExperienceYears(minimum=0, maximum=None)

    if any(marker in cleaned for marker in _NO_REQUIREMENT_MARKERS):
        return ExperienceYears(minimum=None, maximum=None)

    if any(marker in cleaned for marker in _FRESH_GRADUATE_MARKERS):
        return ExperienceYears(minimum=0, maximum=None)

    match = _EXPERIENCE_RANGE_RE.search(cleaned)

    if match is not None:
        return _bounded(
            int(match.group("low")),
            int(match.group("high")),
        )

    match = _EXPERIENCE_MIN_RE.search(cleaned)

    if match is not None:
        return _bounded(int(match.group("low")), None)

    match = _EXPERIENCE_MAX_RE.search(cleaned)

    if match is not None:
        return _bounded(None, int(match.group("high")))

    match = _EXPERIENCE_EXACT_RE.match(cleaned)

    if match is not None:
        years = int(match.group("low"))
        return _bounded(years, years)

    return None


def normalize_location(
    city: object = None,
    district: object = None,
) -> str | None:
    """Return ``城市`` or ``城市-区县`` from separate source fields."""

    clean_city = _city_text(city)
    clean_district = _clean(district)

    if clean_city is None:
        return clean_district

    if clean_district is None:
        return clean_city

    if clean_district == clean_city:
        return clean_city

    return f"{clean_city}-{clean_district}"


def normalize_location_text(value: object) -> str | None:
    """Normalize a combined location string without a city gazetteer.

    Only deterministic edits happen: whitespace collapsing, ``·``/``/``/``、``
    separators unified to ``-``, and a trailing ``市`` removed from the first
    (city) segment so the combined form matches
    :func:`normalize_location`'s ``城市-区县`` output. No alias or fuzzy
    matching is performed.
    """

    cleaned = _clean(value)

    if cleaned is None:
        return None

    segments = [
        segment
        for segment in _LOCATION_SEPARATORS.split(cleaned)
        if segment
    ]

    if not segments:
        return None

    if len(segments) == 1:
        return _city_text(segments[0])

    city = _city_text(segments[0])
    district = _clean(segments[1]) if len(segments) > 1 else None

    if city is None:
        return district

    if district is None or district == city:
        return city

    return f"{city}-{district}"


def normalize_deadline_fields(
    value: object,
    *,
    source_tz: timezone | None = None,
    now: datetime | None = None,
) -> NormalizedTimestamp | None:
    """F13 deadline normalization for Chinese source strings.

    Known deadline labels (``截止``/``报名截止``/...) are stripped first; the
    remaining text goes through the shared Beijing-time normalizer.
    """

    cleaned = strip_deadline_labels(value)

    return normalize_deadline(cleaned, source_tz=source_tz, now=now)


def strip_deadline_labels(value: object) -> object:
    """Remove a leading deadline label so the value can be parsed."""

    if not isinstance(value, str):
        return value

    text = value.replace("\u3000", " ").strip()

    lowered = text.lower()

    for label in _DEADLINE_LABELS:
        if lowered.endswith(label.lower()):
            text = text[: -len(label)].strip(" :：")
            lowered = text.lower()

    for label in _DEADLINE_LABELS:
        if lowered.startswith(label.lower()):
            text = text[len(label) :].strip(" :：")
            lowered = text.lower()

    for suffix in ("（含）", "(含)", "之前", "前"):
        if text.endswith(suffix):
            text = text[: -len(suffix)].strip()

    return text


def _clean(value: object) -> str | None:
    if not isinstance(value, str):
        return None

    cleaned = re.sub(r"\s+", " ", value.replace("\u3000", " ")).strip()

    return cleaned or None


def _city_text(value: object) -> str | None:
    cleaned = _clean(value)

    if cleaned is None:
        return None

    if (
        cleaned.endswith(_CITY_SUFFIX)
        and len(cleaned) > len(_CITY_SUFFIX) + 1
    ):
        cleaned = cleaned[: -len(_CITY_SUFFIX)]

    return cleaned or None


def _bounded(
    minimum: int | None,
    maximum: int | None,
) -> ExperienceYears | None:
    if minimum is None and maximum is None:
        return ExperienceYears(None, None)

    for value in (minimum, maximum):
        if value is not None and (value < 0 or value > MAX_EXPERIENCE_YEARS):
            return None

    if minimum is not None and maximum is not None and maximum < minimum:
        return None

    return ExperienceYears(minimum, maximum)


__all__ = [
    "EDUCATION_BACHELOR",
    "EDUCATION_COLLEGE",
    "EDUCATION_DOCTOR",
    "EDUCATION_MASTER",
    "EDUCATION_UNLIMITED",
    "ExperienceYears",
    "JOB_NATURE_CAMPUS",
    "JOB_NATURE_EXPERIENCED",
    "JOB_NATURE_INTERNSHIP",
    "MAX_EXPERIENCE_YEARS",
    "normalize_deadline_fields",
    "normalize_education",
    "normalize_experience_years",
    "normalize_job_nature",
    "normalize_location",
    "normalize_location_text",
    "strip_deadline_labels",
]
