"""Mirrored pre-ingest quality checks for the manual-capture path.

ExecPlan 0015A sections 5.5 and 6.1 define three deterministic outcomes
(``rejected`` / ``needs_review`` / ``accepted_advisory``) and one stable reason
code per trigger. This module is the **Python mirror** of
``service/src/job/job_quality.cpp`` (``EvaluateJobQuality``,
``MarkDuplicateContentInBatch``, ``SummarizeJobQuality``): it runs *before* the
publish call so an operator sees why an item would be rejected or flagged, and
it reports exactly the same vocabulary as the C++ engine.

It is UX only and **never authoritative**: the C++ gate decides. A finding that
cannot be decided locally (for example an employer contradicted by JSON-LD) is
left to the server.

Mirrored rules, in the C++ order:

1. ``company_missing`` - employer empty after the C++ trim -> reject.
2. ``description_too_large`` - more than 49,152 **UTF-8 bytes** -> reject,
   carrying ``limit_bytes`` / ``actual_bytes``. Never truncated.
3. ``description_polluted`` - at least 3 distinct navigation markers **and**
   marker bytes >= 20 % of the description bytes -> needs_review; otherwise any
   marker is ``description_marker_minor`` -> advisory.
4. ``source_url_unusable`` - not an absolute ``http(s)`` URL -> reject.
5. ``source_url_not_detail`` - empty/``/`` path, a known list path, or a
   query-only URL -> needs_review.
6. ``source_url_shape_unrecognised`` - no detail marker and no >= 4 digit run ->
   advisory.
7. ``employer_is_platform`` - registry match -> needs_review (fail-open when the
   name is unknown).
8. ``employer_implausible`` - normalised employer empty/1 ASCII byte long, or
   all ASCII digits -> advisory.
9. ``title_equals_company`` -> needs_review.
10. ``duplicate_content_in_batch`` - equal normalised employer+title+location
    under a different normalised ``source_url`` -> needs_review on every
    involved item.

``review_status`` is derived the same way: any reject refuses the item (nothing
is written, no status), otherwise any needs_review finding sets ``1``, otherwise
``0`` while advisory findings may still exist.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Any, Iterable, Mapping, Sequence

from ..registry import (
    PlatformNameRegistry,
    ascii_trim,
    normalize_platform_name,
)
from .artifact import CaptureError

#: 0015A decision 5 / docs/04 section 5.1: the application-layer description cap
#: is 48 KiB of UTF-8 **bytes** (well below the TEXT column's 65,535 bytes). The
#: check is byte based (`len(value.encode("utf-8"))`), never character based, and
#: an over-cap description is rejected - never truncated.
MAX_DESCRIPTION_BYTES = 48 * 1024

OUTCOME_REJECTED = "rejected"
OUTCOME_NEEDS_REVIEW = "needs_review"
OUTCOME_ADVISORY = "accepted_advisory"

CODE_COMPANY_MISSING = "company_missing"
CODE_DESCRIPTION_TOO_LARGE = "description_too_large"
CODE_DESCRIPTION_POLLUTED = "description_polluted"
CODE_DESCRIPTION_MARKER_MINOR = "description_marker_minor"
CODE_SOURCE_URL_UNUSABLE = "source_url_unusable"
CODE_SOURCE_URL_NOT_DETAIL = "source_url_not_detail"
CODE_SOURCE_URL_UNRECOGNISED = "source_url_shape_unrecognised"
CODE_EMPLOYER_IS_PLATFORM = "employer_is_platform"
CODE_EMPLOYER_IMPLAUSIBLE = "employer_implausible"
CODE_TITLE_EQUALS_COMPANY = "title_equals_company"
CODE_DUPLICATE_CONTENT_IN_BATCH = "duplicate_content_in_batch"

#: Versioned marker table (docs/04 §5.1). Same list and same order as
#: ``PollutionMarkers()`` in job_quality.cpp.
POLLUTION_MARKERS = (
    "推荐岗位",
    "相关职位",
    "热门职位",
    "猜你喜欢",
    "浏览更多",
    "登录注册",
)

POLLUTION_MIN_DISTINCT = 3
POLLUTION_MIN_SHARE_PERCENT = 20

#: Same markers as ``HasDetailMarker`` in job_quality.cpp.
DETAIL_MARKERS = ("/job/", "/jobs/", "/position", "/detail", "/jobinfo",
                  "/job-detail")

#: Same list as ``IsClearlyNotDetail`` in job_quality.cpp.
LIST_PATHS = ("/jobs", "/job", "/search", "/list", "/positions")

_ASCII_DIGITS = "0123456789"

REVIEW_STATUS_CLEAN = 0
REVIEW_STATUS_NEEDS_REVIEW = 1


@dataclass(frozen=True)
class Finding:
    """One mirrored quality finding (the C++ ``JobQualityFinding`` shape)."""

    code: str
    reason: str
    outcome: str
    field: str | None = None
    limit_bytes: int | None = None
    actual_bytes: int | None = None


@dataclass(frozen=True)
class ItemQuality:
    """The verdict for one payload item."""

    index: int
    source_url: str
    outcome: str
    review_status: int
    findings: tuple[Finding, ...] = ()
    diagnostic_code: str | None = None
    diagnostic_field: str | None = None
    diagnostic_reason: str | None = None

    @property
    def rejected(self) -> bool:
        return self.outcome == OUTCOME_REJECTED

    def to_json(self) -> dict[str, Any]:
        return {
            "index": self.index,
            "source_url": self.source_url,
            "outcome": self.outcome,
            "review_status": self.review_status,
            "diagnostic_code": self.diagnostic_code,
            "diagnostic_field": self.diagnostic_field,
            "diagnostic_reason": self.diagnostic_reason,
            "findings": [
                finding_to_json(finding, self.review_status)
                for finding in self.findings
            ],
        }


@dataclass(frozen=True)
class QualityCounters:
    """Item counters; ``clean``/``needs_review``/``rejected`` mirror docs/04 §5.2."""

    clean: int = 0
    needs_review: int = 0
    rejected: int = 0
    advisory: int = 0

    def to_json(self) -> dict[str, int]:
        return {
            "clean": self.clean,
            "needs_review": self.needs_review,
            "rejected": self.rejected,
            "advisory": self.advisory,
        }


@dataclass(frozen=True)
class CaptureQualityReport:
    """The pre-flight report shown to an operator before publishing."""

    items: tuple[ItemQuality, ...] = ()
    counters: QualityCounters = QualityCounters()

    @property
    def rejected_items(self) -> tuple[ItemQuality, ...]:
        return tuple(item for item in self.items if item.rejected)

    @property
    def needs_review_items(self) -> tuple[ItemQuality, ...]:
        return tuple(
            item
            for item in self.items
            if item.review_status == REVIEW_STATUS_NEEDS_REVIEW
        )

    def to_json(self) -> dict[str, Any]:
        return {
            "counters": self.counters.to_json(),
            "items": [item.to_json() for item in self.items],
        }


def finding_to_json(
    finding: Finding,
    review_status: int | None = None,
) -> dict[str, Any]:
    """The docs/04 §5.2 finding shape plus the byte diagnostic when present."""

    document: dict[str, Any] = {
        "code": finding.code,
        "reason": finding.reason,
        "outcome": finding.outcome,
        "review_status": review_status,
        "field": finding.field,
        "limit_bytes": finding.limit_bytes,
        "actual_bytes": finding.actual_bytes,
    }

    return document


def capture_error_finding(
    error: CaptureError,
    review_status: int = REVIEW_STATUS_CLEAN,
) -> dict[str, Any] | None:
    """Project a structured :class:`CaptureError` onto the finding shape.

    A parse-level rejection (``job.company`` missing, an over-cap field) already
    knows its stable code, so an operator sees the same vocabulary whether the
    item is refused while parsing or while evaluating quality.
    """

    if error.code is None:
        return None

    return {
        "code": error.code,
        "reason": str(error),
        "outcome": OUTCOME_REJECTED,
        "review_status": review_status,
        "field": error.field,
        "limit_bytes": error.limit_bytes,
        "actual_bytes": error.actual_bytes,
    }


def _field(job: Any, name: str) -> str | None:
    """Read a string field from a payload mapping or a ``CapturedJob``."""

    if isinstance(job, Mapping):
        value = job.get(name)
    else:
        value = getattr(job, name, None)

    return value if isinstance(value, str) else None


def _add(
    findings: list[Finding],
    code: str,
    reason: str,
    outcome: str,
    *,
    field: str | None = None,
    limit_bytes: int | None = None,
    actual_bytes: int | None = None,
) -> None:
    findings.append(
        Finding(
            code=code,
            reason=reason,
            outcome=outcome,
            field=field,
            limit_bytes=limit_bytes,
            actual_bytes=actual_bytes,
        )
    )


def utf8_size(value: str) -> int:
    """UTF-8 byte length; the only unit the 48 KiB cap is measured in."""

    return len(value.encode("utf-8"))


def marker_stats(description: str) -> tuple[int, int]:
    """``(distinct markers, marker byte share percent)`` - byte based."""

    marker_bytes = 0
    distinct = 0

    for marker in POLLUTION_MARKERS:
        hits = description.count(marker)

        if hits > 0:
            distinct += 1
            marker_bytes += hits * utf8_size(marker)

    total = utf8_size(description)

    if total == 0:
        return distinct, 0

    # C++ integer division: (markerBytes * 100) / description.size().
    return distinct, (marker_bytes * 100) // total


def is_all_ascii_digits(value: str) -> bool:
    """``std::isdigit`` over every byte/code point; empty is not all digits."""

    return bool(value) and all(
        character in _ASCII_DIGITS for character in value
    )


def source_url_path(url: str) -> tuple[str, bool]:
    """Split the URL the way the C++ engine does (absolute URL assumed)."""

    authority_start = url.find("//")
    authority_end = url.find("/", authority_start + 2) if authority_start >= 0 else -1
    query_start = url.find("?")

    if authority_end < 0:
        path = ""
    else:
        end = len(url) if query_start < 0 else query_start
        path = url[authority_end:end]

    return path, query_start >= 0


def is_clearly_not_detail(path: str, has_query: bool) -> bool:
    if not path or path == "/":
        return True

    trimmed = path[:-1] if path.endswith("/") else path

    if not trimmed:
        return True

    if trimmed in LIST_PATHS:
        return True

    # A query-only URL (`?keyword=...`) is never a job detail page.
    if has_query and "/" not in trimmed:
        return True

    return False


def has_detail_marker(path: str) -> bool:
    return any(marker in path for marker in DETAIL_MARKERS)


def has_id_like_segment(path: str) -> bool:
    run = 0

    for character in path:
        if character in _ASCII_DIGITS:
            run += 1

            if run >= 4:
                return True
        else:
            run = 0

    return False


def evaluate_findings(
    job: Any,
    *,
    registry: PlatformNameRegistry | None = None,
) -> list[Finding]:
    """Run the mirrored per-item checks; returns the findings in C++ order.

    ``registry=None`` keeps this pure: no registry is consulted (no I/O) and no
    ``employer_is_platform`` finding is produced. The CLI loads the single-source
    file and passes it in.
    """

    active = registry if registry is not None else PlatformNameRegistry()
    findings: list[Finding] = []

    company = ascii_trim(_field(job, "company") or "")

    if not company:
        _add(
            findings,
            CODE_COMPANY_MISSING,
            "employer is missing after normalisation",
            OUTCOME_REJECTED,
            field="company",
        )

    description = _field(job, "description")

    if description is not None:
        actual = utf8_size(description)

        if actual > MAX_DESCRIPTION_BYTES:
            _add(
                findings,
                CODE_DESCRIPTION_TOO_LARGE,
                f"description is {actual} bytes, over the "
                f"{MAX_DESCRIPTION_BYTES} byte cap (not truncated)",
                OUTCOME_REJECTED,
                field="description",
                limit_bytes=MAX_DESCRIPTION_BYTES,
                actual_bytes=actual,
            )
        elif description:
            distinct, share = marker_stats(description)

            if distinct >= POLLUTION_MIN_DISTINCT and (
                share >= POLLUTION_MIN_SHARE_PERCENT
            ):
                _add(
                    findings,
                    CODE_DESCRIPTION_POLLUTED,
                    f"description contains {distinct} navigation/recommendation "
                    f"markers ({share}% of its bytes)",
                    OUTCOME_NEEDS_REVIEW,
                    field="description",
                )
            elif distinct > 0:
                _add(
                    findings,
                    CODE_DESCRIPTION_MARKER_MINOR,
                    f"{distinct} minor navigation marker(s); kept verbatim",
                    OUTCOME_ADVISORY,
                    field="description",
                )

    url = ascii_trim(_field(job, "source_url") or "")

    if not (url.startswith("http://") or url.startswith("https://")):
        _add(
            findings,
            CODE_SOURCE_URL_UNUSABLE,
            "source_url is not an absolute http(s) URL",
            OUTCOME_REJECTED,
            field="source_url",
        )
    else:
        path, has_query = source_url_path(url)

        if is_clearly_not_detail(path, has_query):
            _add(
                findings,
                CODE_SOURCE_URL_NOT_DETAIL,
                "source_url looks like a site root/list/search page, not a "
                "job detail page",
                OUTCOME_NEEDS_REVIEW,
                field="source_url",
            )
        elif not has_detail_marker(path) and not has_id_like_segment(path):
            _add(
                findings,
                CODE_SOURCE_URL_UNRECOGNISED,
                "source_url shape is unrecognised; accepted as advisory",
                OUTCOME_ADVISORY,
                field="source_url",
            )

    if company:
        if active.size > 0 and active.contains(company):
            _add(
                findings,
                CODE_EMPLOYER_IS_PLATFORM,
                'employer equals a registered platform name ("'
                f'{company}"); the platform is not the employer',
                OUTCOME_NEEDS_REVIEW,
                field="company",
            )

        normalized = normalize_platform_name(company)

        # C++ compares `normalized.size() < 2` in BYTES, so a one-character CJK
        # name (3 bytes) is plausible while a single ASCII character is not.
        if utf8_size(normalized) < 2 or is_all_ascii_digits(normalized):
            _add(
                findings,
                CODE_EMPLOYER_IMPLAUSIBLE,
                "employer name looks implausible for a company",
                OUTCOME_ADVISORY,
                field="company",
            )

        if normalized and (
            normalize_platform_name(_field(job, "title") or "") == normalized
        ):
            _add(
                findings,
                CODE_TITLE_EQUALS_COMPANY,
                "title equals employer; the real employer is unknown",
                OUTCOME_NEEDS_REVIEW,
                field="title",
            )

    return findings


def _content_key(job: Any) -> str:
    return "\x1f".join(
        (
            normalize_platform_name(_field(job, "company") or ""),
            normalize_platform_name(_field(job, "title") or ""),
            normalize_platform_name(_field(job, "location") or ""),
        )
    )


def mark_duplicate_content_in_batch(
    jobs: Sequence[Any],
    findings_per_item: Sequence[list[Finding]],
) -> None:
    """Flag equal employer+title+location under a different URL (all items)."""

    if len(jobs) != len(findings_per_item):
        return

    keys = [_content_key(job) for job in jobs]

    for index, key in enumerate(keys):
        for other, other_key in enumerate(keys):
            if other == index or other_key != key:
                continue

            if normalize_platform_name(
                _field(jobs[other], "source_url") or ""
            ) != normalize_platform_name(_field(jobs[index], "source_url") or ""):
                _add(
                    findings_per_item[index],
                    CODE_DUPLICATE_CONTENT_IN_BATCH,
                    "another job in this batch has the same employer, title and "
                    "location under a different URL",
                    OUTCOME_NEEDS_REVIEW,
                    field="source_url",
                )
                break


def _item_result(
    index: int,
    source_url: str,
    findings: Iterable[Finding],
) -> ItemQuality:
    collected = tuple(findings)
    rejected = any(
        finding.outcome == OUTCOME_REJECTED for finding in collected
    )
    needs_review = any(
        finding.outcome == OUTCOME_NEEDS_REVIEW for finding in collected
    )

    diagnostic_code = None
    diagnostic_field = None
    diagnostic_reason = None

    if rejected:
        for finding in collected:
            if finding.outcome == OUTCOME_REJECTED:
                diagnostic_code = finding.code
                diagnostic_field = finding.field
                diagnostic_reason = finding.reason
                break

    if rejected:
        outcome = OUTCOME_REJECTED
        review_status = REVIEW_STATUS_CLEAN
    elif needs_review:
        outcome = OUTCOME_NEEDS_REVIEW
        review_status = REVIEW_STATUS_NEEDS_REVIEW
    else:
        outcome = "clean"
        review_status = REVIEW_STATUS_CLEAN

    return ItemQuality(
        index=index,
        source_url=source_url,
        outcome=outcome,
        review_status=review_status,
        findings=collected,
        diagnostic_code=diagnostic_code,
        diagnostic_field=diagnostic_field,
        diagnostic_reason=diagnostic_reason,
    )


def evaluate_item_quality(
    job: Any,
    *,
    index: int = 0,
    registry: PlatformNameRegistry | None = None,
) -> ItemQuality:
    """Evaluate one item on its own (no batch-level duplicate check)."""

    return _item_result(
        index,
        _field(job, "source_url") or "",
        evaluate_findings(job, registry=registry),
    )


def evaluate_batch_quality(
    jobs: Sequence[Any],
    *,
    registry: PlatformNameRegistry | None = None,
) -> CaptureQualityReport:
    """Evaluate a batch: per-item checks plus the batch duplicate check."""

    items = list(jobs)
    findings_per_item = [
        evaluate_findings(job, registry=registry) for job in items
    ]
    mark_duplicate_content_in_batch(items, findings_per_item)

    results = tuple(
        _item_result(
            index,
            _field(job, "source_url") or "",
            findings_per_item[index],
        )
        for index, job in enumerate(items)
    )

    counters = QualityCounters(
        clean=sum(1 for item in results if item.outcome == "clean"),
        needs_review=sum(1 for item in results if item.review_status == 1),
        rejected=sum(1 for item in results if item.rejected),
        advisory=sum(
            1
            for item in results
            if any(
                finding.outcome == OUTCOME_ADVISORY
                for finding in item.findings
            )
        ),
    )

    return CaptureQualityReport(items=results, counters=counters)


__all__ = [
    "CODE_COMPANY_MISSING",
    "CODE_DESCRIPTION_MARKER_MINOR",
    "CODE_DESCRIPTION_POLLUTED",
    "CODE_DESCRIPTION_TOO_LARGE",
    "CODE_DUPLICATE_CONTENT_IN_BATCH",
    "CODE_EMPLOYER_IMPLAUSIBLE",
    "CODE_EMPLOYER_IS_PLATFORM",
    "CODE_SOURCE_URL_NOT_DETAIL",
    "CODE_SOURCE_URL_UNRECOGNISED",
    "CODE_SOURCE_URL_UNUSABLE",
    "CODE_TITLE_EQUALS_COMPANY",
    "DETAIL_MARKERS",
    "LIST_PATHS",
    "MAX_DESCRIPTION_BYTES",
    "OUTCOME_ADVISORY",
    "OUTCOME_NEEDS_REVIEW",
    "OUTCOME_REJECTED",
    "POLLUTION_MARKERS",
    "POLLUTION_MIN_DISTINCT",
    "POLLUTION_MIN_SHARE_PERCENT",
    "CaptureQualityReport",
    "Finding",
    "ItemQuality",
    "QualityCounters",
    "capture_error_finding",
    "evaluate_batch_quality",
    "evaluate_findings",
    "evaluate_item_quality",
    "finding_to_json",
    "has_detail_marker",
    "has_id_like_segment",
    "is_all_ascii_digits",
    "is_clearly_not_detail",
    "mark_duplicate_content_in_batch",
    "marker_stats",
    "source_url_path",
    "utf8_size",
]
