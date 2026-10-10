"""Chinese salary normalisation to the platform's "monthly thousands" columns.

The ``jobs.salary_min``/``salary_max`` columns are **INT UNSIGNED thousands of
the monthly salary**, so only forms that convert exactly to a monthly thousand
integer are published (ExecPlan 0014 section 5.1, "宁可省略也不换算"):

===========================  ===================================
Source form                  Result (thousand, monthly)
===========================  ===================================
``15-25K``                   ``(15, 25)``
``8k-12k``                   ``(8, 12)``
``1.5-2.5万/月``             ``(15, 25)``
``1.5万/月``                 ``(15, 15)``
``8000-12000元/月``          ``(8, 12)``
``20-35K·14薪``              ``(20, 35)`` (months do not change the monthly value)
``15K以上``                  ``(15, None)``
``面议`` / ``300/天`` /      omitted (``None``)
``30-50万/年``
``8.5K`` (not integral)      omitted (``None``)
===========================  ===================================

``万`` without a monthly basis is ambiguous and is omitted; ``K`` alone is
monthly by the convention used on every Chinese job board.
"""

from __future__ import annotations

import re
from typing import Final

MAX_MONTHLY_THOUSAND: Final[int] = 1_000_000

_AMBIGUOUS_MARKERS = ("面议", "面谈", "议", "保密", "详谈")
_FOREIGN_CURRENCY_MARKERS = ("$", "usd", "美元", "€", "£", "hkd", "港币")
_PER_YEAR_MARKERS = ("/年", "年薪", "每年", "年包")
_PER_DAY_MARKERS = ("/天", "元/天", "每天", "日薪", "天/")
_PER_HOUR_MARKERS = ("/小时", "时薪")
_MONTHLY_MARKERS = ("/月", "月薪", "每月", "月/")

_NUMBER_RE = re.compile(r"\d+(?:\.\d+)?")
_RANGE_SPLIT_RE = re.compile(r"\s*(?:-|~|—|–|至|到)\s*")
#: "·14薪" / "，13薪" / "×15薪": the number of months never changes the monthly
#: value, so the suffix is removed before the numbers are read.
_EXTRA_MONTHS_RE = re.compile(r"[·*×,，]?\s*\d+\s*薪")

_MIN_ONLY_MARKERS = ("以上", "起", "及以上", "或以上")
_MAX_ONLY_MARKERS = ("以下", "以内", "及以下", "或以下")


def parse_monthly_salary_thousand(text: object) -> tuple[int, int | None] | None:
    """Return ``(min, max)`` monthly thousands, or ``None`` when unsure."""

    cleaned = _clean(text)

    if cleaned is None:
        return None

    lowered = cleaned.lower()

    if any(marker in cleaned for marker in _AMBIGUOUS_MARKERS):
        return None

    if any(marker in lowered for marker in _FOREIGN_CURRENCY_MARKERS):
        return None

    if any(marker in cleaned for marker in _PER_YEAR_MARKERS):
        # Annual packages need a month count the source never states.
        return None

    if any(marker in lowered for marker in _PER_DAY_MARKERS) or any(
        marker in cleaned for marker in _PER_DAY_MARKERS
    ):
        return None

    if any(marker in cleaned for marker in _PER_HOUR_MARKERS):
        return None

    has_monthly_basis = any(
        marker in lowered for marker in _MONTHLY_MARKERS
    ) or any(marker in cleaned for marker in _MONTHLY_MARKERS)

    factor = _unit_factor(cleaned)

    if factor is None:
        return None

    if factor == 10000 and not has_monthly_basis:
        return None

    numbers = _extract_numbers(cleaned)

    if not numbers:
        return None

    scaled = [_to_thousand(value, factor) for value in numbers]

    if any(value is None for value in scaled):
        return None

    values = [value for value in scaled if value is not None]

    if any(value <= 0 or value > MAX_MONTHLY_THOUSAND for value in values):
        return None

    if any(marker in cleaned for marker in _MIN_ONLY_MARKERS):
        return values[0], None

    if any(marker in cleaned for marker in _MAX_ONLY_MARKERS):
        return None, values[0]

    if len(values) == 1:
        return values[0], values[0]

    minimum, maximum = values[0], values[1]

    if maximum < minimum:
        return None

    return minimum, maximum


def parse_salary_currency(text: object) -> str | None:
    """``"CNY"`` for an accepted domestic salary string, else ``None``."""

    if parse_monthly_salary_thousand(text) is None:
        return None

    return "CNY"


def _clean(text: object) -> str | None:
    if not isinstance(text, str):
        return None

    cleaned = text.replace("\u3000", " ").strip()

    if not cleaned:
        return None

    return re.sub(r"\s+", " ", cleaned)


def _unit_factor(text: str) -> int | None:
    """Thousands per unit: ``K``/``千`` -> 1, ``万`` -> 10, ``元`` -> 1/1000."""

    lowered = text.lower()

    if "万" in text:
        return 10000

    if re.search(r"\d\s*k", lowered) or "千" in text:
        return 1000

    if "元" in text or "¥" in text or "￥" in text:
        return 1

    return None


def _extract_numbers(text: str) -> list[float]:
    head = _EXTRA_MONTHS_RE.sub("", text)

    for separator in ("，", ",", "("):
        if separator in head:
            head = head.split(separator, 1)[0]

    parts = _RANGE_SPLIT_RE.split(head)
    numbers: list[float] = []

    for part in parts:
        match = _NUMBER_RE.search(part)

        if match is None:
            continue

        try:
            numbers.append(float(match.group(0)))
        except ValueError:  # pragma: no cover - regex guarantees digits
            return []

        if len(numbers) == 2:
            break

    return numbers


def _to_thousand(value: float, factor: int) -> int | None:
    thousand = value * factor / 1000.0

    if abs(thousand - round(thousand)) > 1e-9:
        # A value that is not exactly representable in the column would have to
        # be rounded, which is fabricated data.
        return None

    return int(round(thousand))
