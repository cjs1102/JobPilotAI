"""Parse and validate a capture artifact produced by the bookmarklet.

The artifact is **untrusted input**: it came from a web page via the clipboard
and the user may have edited it by hand. Everything is checked here, before any
normalization and before any network call, with a diagnosable message for every
rejection. Nothing is silently dropped, clamped or truncated.

Shape (``capture_version`` 1, produced by ``capture/bookmarklet.js``)::

    {
      "capture_version": 1,
      "captured_at": "2026-10-09T20:15:00.000Z",
      "page_url": "https://example.com/job/1",
      "page_title": "...",
      "page_host": "example.com",
      "job": {
        "title": "...", "company": "...", "source_url": "...",
        "location": "...", "salary": "15-25K", "description": "...",
        "education": "本科", "job_nature": "校招", "experience": "3-5年",
        "deadline": "2026-10-31", "published_at": "...", "skills": ["..."]
      },
      "warnings": ["..."]
    }

A bare ``job`` object (no wrapper) is also accepted, and unknown keys are
ignored so a newer bookmarklet stays readable. ``skills`` are carried for the
audit trail but are **not** published: the source gives skill names without a
level, and writing a level would fabricate a requirement.
"""

from __future__ import annotations

import urllib.parse
from dataclasses import dataclass, field
from typing import Any, Mapping, Sequence

MAX_CAPTURE_VERSION = 1
#: The manual import endpoint accepts 1..100 jobs per request, and one capture
#: file is published as one request so the outcome is unambiguous.
MAX_CAPTURES_PER_RUN = 100

#: Column byte limits from docs/03 section 3, mirroring the server validation.
MAX_COMPANY_BYTES = 128
MAX_TITLE_BYTES = 128
MAX_LOCATION_BYTES = 64
MAX_SOURCE_URL_BYTES = 255

_OPTIONAL_TEXT_FIELDS = (
    "location",
    "salary",
    "description",
    "education",
    "job_nature",
    "experience",
    "deadline",
    "published_at",
)


class CaptureError(Exception):
    """The capture artifact is unusable; the message says why.

    A rejection that maps onto a stable 0015A quality code carries it here
    (``code``, ``field`` and, for a byte cap, ``limit_bytes``/``actual_bytes``)
    so the CLI can show the operator the same machine-readable finding the C++
    gate would report. The plain message stays human readable.
    """

    def __init__(
        self,
        message: str,
        *,
        code: str | None = None,
        field: str | None = None,
        limit_bytes: int | None = None,
        actual_bytes: int | None = None,
    ) -> None:
        super().__init__(message)
        self.code = code
        self.field = field
        self.limit_bytes = limit_bytes
        self.actual_bytes = actual_bytes


@dataclass(frozen=True)
class CapturedJob:
    """One validated capture: raw source strings, not yet normalized."""

    source_url: str
    title: str
    company: str
    location: str | None = None
    salary: str | None = None
    description: str | None = None
    education: str | None = None
    job_nature: str | None = None
    experience: str | None = None
    deadline: str | None = None
    published_at: str | None = None
    skills: tuple[str, ...] = ()
    page_url: str | None = None
    page_title: str | None = None
    captured_at: str | None = None
    warnings: tuple[str, ...] = field(default_factory=tuple)


def _require_mapping(value: Any, label: str) -> Mapping[str, Any]:
    if not isinstance(value, Mapping):
        raise CaptureError(f"{label} must be a JSON object")

    return value


def _text(
    document: Mapping[str, Any],
    key: str,
    *,
    label: str,
    required: bool = False,
    code: str | None = None,
    field: str | None = None,
) -> str | None:
    value = document.get(key)

    if value is None:
        if required:
            raise CaptureError(
                f"{label} is required but missing",
                code=code,
                field=field,
            )

        return None

    if not isinstance(value, str):
        raise CaptureError(
            f"{label} must be a string, got {type(value).__name__}"
        )

    cleaned = value.strip()

    if not cleaned:
        if required:
            raise CaptureError(
                f"{label} must not be empty",
                code=code,
                field=field,
            )

        return None

    return cleaned


def _bounded(
    value: str,
    max_bytes: int,
    label: str,
    *,
    code: str,
    field: str,
) -> str:
    size = len(value.encode("utf-8"))

    if size > max_bytes:
        raise CaptureError(
            f"{label} is {size} bytes, over the {max_bytes} byte limit; "
            "shorten it before publishing (it is never truncated for you)",
            code=code,
            field=field,
            limit_bytes=max_bytes,
            actual_bytes=size,
        )

    return value


def _absolute_http_url(
    value: str,
    label: str,
    *,
    code: str | None = None,
    field: str | None = None,
) -> str:
    parts = urllib.parse.urlsplit(value)

    if parts.scheme not in {"http", "https"} or not parts.netloc:
        raise CaptureError(
            f"{label} must be an absolute http(s) URL, got {value!r}",
            code=code,
            field=field,
        )

    return value


def _string_list(
    document: Mapping[str, Any],
    key: str,
    *,
    label: str,
) -> tuple[str, ...]:
    value = document.get(key)

    if value is None:
        return ()

    if isinstance(value, str):
        # A single value is accepted for hand-written artifacts.
        return (value,) if value.strip() else ()

    if not isinstance(value, Sequence):
        raise CaptureError(f"{label} must be a list of strings")

    entries: list[str] = []

    for index, entry in enumerate(value):
        if not isinstance(entry, str):
            raise CaptureError(
                f"{label}[{index}] must be a string, got "
                f"{type(entry).__name__}"
            )

        if entry.strip():
            entries.append(entry.strip())

    return tuple(entries)


def parse_artifact(document: Any) -> CapturedJob:
    """Validate one capture artifact and return a :class:`CapturedJob`."""

    root = _require_mapping(document, "capture artifact")

    version = root.get("capture_version")

    if version is not None:
        if isinstance(version, bool) or not isinstance(version, int):
            raise CaptureError(
                "capture_version must be an integer, got "
                f"{type(version).__name__}"
            )

        if version != MAX_CAPTURE_VERSION:
            raise CaptureError(
                f"unsupported capture_version {version}; this publisher "
                f"understands {MAX_CAPTURE_VERSION}"
            )

    job_document = root.get("job")

    if job_document is None:
        # A bare job object (the wrapper omitted) is accepted.
        job_document = root
    else:
        job_document = _require_mapping(job_document, "capture artifact.job")

    page_url = _text(root, "page_url", label="page_url")

    if page_url is not None:
        page_url = _absolute_http_url(page_url, "page_url")

    source_url = _text(
        job_document, "source_url", label="job.source_url"
    )

    if source_url is None:
        source_url = page_url

    if source_url is None:
        raise CaptureError(
            "job.source_url (or page_url) is required: it is the platform's "
            "idempotency key",
            code="source_url_unusable",
            field="source_url",
        )

    source_url = _absolute_http_url(
        source_url,
        "job.source_url",
        code="source_url_unusable",
        field="source_url",
    )
    source_url = _bounded(
        source_url,
        MAX_SOURCE_URL_BYTES,
        "job.source_url",
        code="source_url_too_large",
        field="source_url",
    )

    title = _text(
        job_document,
        "title",
        label="job.title",
        required=True,
        code="title_missing",
        field="title",
    )
    company = _text(
        job_document,
        "company",
        label="job.company",
        required=True,
        code="company_missing",
        field="company",
    )

    assert title is not None and company is not None  # required=True above

    title = _bounded(
        title,
        MAX_TITLE_BYTES,
        "job.title",
        code="title_too_large",
        field="title",
    )
    company = _bounded(
        company,
        MAX_COMPANY_BYTES,
        "job.company",
        code="company_too_large",
        field="company",
    )

    optional: dict[str, str | None] = {}

    for key in _OPTIONAL_TEXT_FIELDS:
        value = _text(job_document, key, label=f"job.{key}")

        if key == "location" and value is not None:
            value = _bounded(
                value,
                MAX_LOCATION_BYTES,
                "job.location",
                code="location_too_large",
                field="location",
            )

        # NOTE: the description is deliberately NOT bounded here. The 48 KiB
        # UTF-8 byte cap is a 0015A *quality* check (capture/quality.py), so an
        # operator sees the stable `description_too_large` finding with
        # limit/actual bytes, and the pre-flight refuses the batch. The value is
        # never truncated; ManualPublisher.publish additionally refuses to send
        # an over-cap description.
        optional[key] = value

    return CapturedJob(
        source_url=source_url,
        title=title,
        company=company,
        location=optional["location"],
        salary=optional["salary"],
        description=optional["description"],
        education=optional["education"],
        job_nature=optional["job_nature"],
        experience=optional["experience"],
        deadline=optional["deadline"],
        published_at=optional["published_at"],
        skills=_string_list(job_document, "skills", label="job.skills"),
        page_url=page_url,
        page_title=_text(root, "page_title", label="page_title"),
        captured_at=_text(root, "captured_at", label="captured_at"),
        warnings=_string_list(root, "warnings", label="warnings"),
    )


def parse_captures(document: Any) -> list[CapturedJob]:
    """Validate one artifact, a list of artifacts, or ``{"captures": [...]}``."""

    entries: Any = document

    if isinstance(document, Mapping):
        if "captures" in document:
            entries = document["captures"]
        elif "jobs" in document and "job" not in document:
            entries = document["jobs"]

    if isinstance(entries, Mapping):
        entries = [entries]

    if not isinstance(entries, Sequence) or isinstance(entries, (str, bytes)):
        raise CaptureError(
            "capture file must be a JSON object, a list of objects, "
            "or {\"captures\": [...]}"
        )

    if not entries:
        raise CaptureError("capture file contains no captures to publish")

    if len(entries) > MAX_CAPTURES_PER_RUN:
        raise CaptureError(
            f"capture file contains {len(entries)} captures, over the "
            f"{MAX_CAPTURES_PER_RUN} per run limit (the manual import endpoint "
            "accepts at most 100 jobs per request); split the file"
        )

    captures: list[CapturedJob] = []
    seen: dict[str, int] = {}

    for index, entry in enumerate(entries):
        try:
            capture = parse_artifact(entry)
        except CaptureError as error:
            # Preserve the stable 0015A code/field/byte diagnostic so the CLI
            # can report it as a machine-readable finding.
            raise CaptureError(
                f"capture[{index}]: {error}",
                code=error.code,
                field=error.field,
                limit_bytes=error.limit_bytes,
                actual_bytes=error.actual_bytes,
            ) from error

        folded = capture.source_url.casefold()

        if folded in seen:
            raise CaptureError(
                f"capture[{index}] repeats source_url {capture.source_url!r} "
                f"(already used by capture[{seen[folded]}]); source_url is the "
                "idempotency key, so duplicates are rejected instead of being "
                "silently merged"
            )

        seen[folded] = index
        captures.append(capture)

    return captures


__all__ = [
    "MAX_CAPTURE_VERSION",
    "MAX_CAPTURES_PER_RUN",
    "MAX_COMPANY_BYTES",
    "MAX_LOCATION_BYTES",
    "MAX_SOURCE_URL_BYTES",
    "MAX_TITLE_BYTES",
    "CaptureError",
    "CapturedJob",
    "parse_artifact",
    "parse_captures",
]
