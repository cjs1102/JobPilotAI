"""Local publisher for manual captures (ExecPlan 0014 Step 6).

Responsibilities, in order:

1. **Normalize** each validated capture with the *same* deterministic modules
   the automatic adapters use (``fetch.normalize_url``, ``normalize_cn``,
   ``salary``, ``timestamps``), so the manual path cannot drift from the
   automatic one and the two channels agree on the ``source_url`` idempotency
   key -- which is what makes F14 (manual wins) reachable at all.
2. **Validate** the resulting payload against the documented
   ``POST /internal/jobs/import`` contract (``docs/04`` section 5.4) before any
   network call.
3. **Publish** it through that existing endpoint with ``source="manual"``, then
   verify the response counters. A publish is reported as successful *only*
   after a ``200`` whose ``created + updated == total ==`` the number of jobs
   submitted.

The internal API token is read from this process's environment by variable name
and is sent only in the ``X-Internal-Token`` header. It is never a command-line
argument, never part of the capture artifact, never in the browser, and never
logged: error messages carry the server's ``{code,message}`` and the HTTP
status, not the credential.
"""

from __future__ import annotations

import json
from dataclasses import dataclass
from typing import Any, Mapping, Sequence

from ..client import (
    DEFAULT_TIMEOUT_SECONDS,
    DEFAULT_USER_AGENT,
    HttpError,
    Transport,
    UrllibTransport,
)
from ..config import ConfigError, JobPilotConfig, resolve_internal_token
from ..fetch import normalize_url
from ..normalizer import html_to_text
from ..normalize_cn import MAX_EXPERIENCE_YEARS
from ..salary import parse_monthly_salary_thousand, parse_salary_currency
from .. import normalize_cn
from ..timestamps import BEIJING_TZ, normalize_published_at
from .artifact import CapturedJob, CaptureError

IMPORT_PATH = "/internal/jobs/import"
MANUAL_SOURCE = "manual"
MAX_IMPORT_BATCH_SIZE = 100

MAX_DESCRIPTION_BYTES = 65535
MAX_LOCATION_BYTES = 64

ABSOLUTE_MAX_EXPERIENCE_YEARS = MAX_EXPERIENCE_YEARS


class PublishError(Exception):
    """The publish attempt failed and must not be reported as a success."""


@dataclass(frozen=True)
class BuiltJob:
    """One normalized import payload plus the local normalization notes."""

    payload: dict[str, Any]
    notes: tuple[str, ...] = ()


@dataclass(frozen=True)
class PublishOutcome:
    """The verified result of one publish attempt."""

    ok: bool
    submitted: int
    created: int = 0
    updated: int = 0
    total: int = 0
    status: int = 0
    dry_run: bool = False
    notes: tuple[str, ...] = ()


def _bounded(value: str | None, max_bytes: int, label: str, notes: list[str]):
    """Omit an over-long *optional* value instead of truncating it."""

    if value is None:
        return None

    if len(value.encode("utf-8")) > max_bytes:
        notes.append(
            f"{label} was omitted: {len(value.encode('utf-8'))} bytes is over "
            f"the {max_bytes} byte column limit (values are never truncated)"
        )
        return None

    return value


def build_import_payload(
    capture: CapturedJob,
) -> BuiltJob:
    """Normalize one capture into the documented import request shape."""

    notes: list[str] = list(capture.warnings)

    try:
        source_url = normalize_url(capture.source_url)
    except ValueError as error:
        raise CaptureError(
            f"source_url could not be normalized: {error}"
        ) from error

    payload: dict[str, Any] = {
        "company": capture.company,
        "title": capture.title,
        "source": MANUAL_SOURCE,
        "source_url": source_url,
    }

    location = _bounded(
        normalize_cn.normalize_location_text(capture.location),
        MAX_LOCATION_BYTES,
        "location",
        notes,
    )

    if location:
        payload["location"] = location

    salary = parse_monthly_salary_thousand(capture.salary)

    if salary is not None:
        minimum, maximum = salary

        if minimum is not None:
            payload["salary_min"] = minimum

        if maximum is not None:
            payload["salary_max"] = maximum

        currency = parse_salary_currency(capture.salary)

        if currency:
            payload["salary_currency"] = currency
    elif capture.salary:
        notes.append(
            f"salary {capture.salary!r} was not published: it is not "
            "unambiguously a monthly amount (the columns are monthly "
            "thousands and nothing is guessed)"
        )

    description = _bounded(
        html_to_text(capture.description),
        MAX_DESCRIPTION_BYTES,
        "description",
        notes,
    )

    if description:
        payload["description"] = description

    education = normalize_cn.normalize_education(capture.education)

    if education is not None:
        payload["education_required"] = education
    elif capture.education:
        notes.append(
            f"education {capture.education!r} was not published: it is not one "
            "of 1不限/2大专/3本科/4硕士/5博士"
        )

    nature = normalize_cn.normalize_job_nature(capture.job_nature)

    if nature is not None:
        payload["job_nature"] = nature
    elif capture.job_nature:
        notes.append(
            f"job_nature {capture.job_nature!r} was not published: it is not "
            "one of 1校招/2社招/3实习"
        )

    experience = normalize_cn.normalize_experience_years(capture.experience)

    if experience is not None:
        for key, value in (
            ("experience_years_min", experience.minimum),
            ("experience_years_max", experience.maximum),
        ):
            if value is None:
                continue

            if value > ABSOLUTE_MAX_EXPERIENCE_YEARS:
                notes.append(
                    f"{key} {value} was not published: over the SMALLINT limit"
                )
                continue

            payload[key] = value
    elif capture.experience:
        notes.append(
            f"experience {capture.experience!r} was not published: no "
            "deterministic year range could be read from it"
        )

    deadline = normalize_cn.normalize_deadline_fields(
        capture.deadline, source_tz=BEIJING_TZ
    )

    if deadline is not None:
        payload["deadline"] = deadline.utc
        payload["deadline_date_only"] = deadline.date_only
    elif capture.deadline:
        notes.append(
            f"deadline {capture.deadline!r} was not published: it is not an "
            "unambiguous date or instant"
        )

    if capture.published_at:
        published = normalize_published_at(capture.published_at)

        if published is not None:
            payload["published_at"] = published
        else:
            notes.append(
                f"published_at {capture.published_at!r} was not published: "
                "it is not an unambiguous date or instant"
            )

    if capture.skills:
        notes.append(
            "skills were captured for the audit trail but are not published: "
            "the page gives names without a level, and job_requirements.level "
            "means something specific"
        )

    return BuiltJob(payload=payload, notes=tuple(notes))


def build_import_payloads(
    captures: Sequence[CapturedJob],
) -> tuple[list[dict[str, Any]], list[str]]:
    """Normalize a validated batch; returns ``(payloads, notes)``."""

    if not captures:
        raise CaptureError("no captures to publish")

    if len(captures) > MAX_IMPORT_BATCH_SIZE:
        raise CaptureError(
            f"{len(captures)} captures exceed the {MAX_IMPORT_BATCH_SIZE} job "
            "manual import batch limit"
        )

    payloads: list[dict[str, Any]] = []
    notes: list[str] = []
    seen: dict[str, int] = {}

    for index, capture in enumerate(captures):
        try:
            built = build_import_payload(capture)
        except CaptureError as error:
            raise CaptureError(f"capture[{index}]: {error}") from error

        # The normalized URL is what the API actually sees, so the duplicate
        # check has to run on it: two captures that differ only by a fragment
        # or a tracking parameter collapse to one idempotency key and the
        # server would reject the batch.
        key = built.payload["source_url"].casefold()

        if key in seen:
            raise CaptureError(
                f"capture[{index}] normalizes to the same source_url as "
                f"capture[{seen[key]}] ({built.payload['source_url']}); "
                "source_url is the idempotency key and duplicates inside one "
                "batch are rejected"
            )

        seen[key] = index
        payloads.append(built.payload)

        for note in built.notes:
            notes.append(f"capture[{index}] {capture.source_url}: {note}")

    return payloads, notes


def resolve_publish_token(
    env_name: str,
    environ: Mapping[str, str] | None = None,
) -> str:
    """Read the internal token by variable name, before any network call."""

    config = JobPilotConfig(
        base_url="http://127.0.0.1",
        internal_token_env=env_name,
    )

    return resolve_internal_token(config, environ)


@dataclass
class ManualPublisher:
    """Publish validated captures through ``POST /internal/jobs/import``."""

    base_url: str
    token: str
    transport: Transport | None = None
    timeout: float = DEFAULT_TIMEOUT_SECONDS
    user_agent: str = DEFAULT_USER_AGENT

    def __post_init__(self) -> None:
        if self.transport is None:
            self.transport = UrllibTransport()

        if not isinstance(self.base_url, str) or not self.base_url.strip():
            raise PublishError("base_url must be a non-empty string")

        normalized = self.base_url.strip().rstrip("/")

        if not normalized.startswith(("http://", "https://")):
            raise PublishError(
                "base_url must start with http:// or https://"
            )

        self.base_url = normalized

        if not isinstance(self.token, str) or not self.token.strip():
            raise PublishError(
                "internal token is missing or blank; refusing to publish"
            )

    @property
    def import_url(self) -> str:
        return self.base_url + IMPORT_PATH

    def publish(
        self,
        payloads: Sequence[Mapping[str, Any]],
        *,
        dry_run: bool = False,
        notes: Sequence[str] = (),
    ) -> PublishOutcome:
        """Publish one batch; never reports success without a verified 200."""

        submitted = len(payloads)

        if submitted == 0:
            raise PublishError("refusing to publish an empty batch")

        if submitted > MAX_IMPORT_BATCH_SIZE:
            raise PublishError(
                f"{submitted} jobs exceed the {MAX_IMPORT_BATCH_SIZE} job "
                "manual import batch limit"
            )

        note_tuple = tuple(notes)

        if dry_run:
            return PublishOutcome(
                ok=True,
                submitted=submitted,
                dry_run=True,
                notes=note_tuple,
            )

        document = {"jobs": list(payloads)}
        body = json.dumps(document).encode("utf-8")
        headers = {
            "Accept": "application/json",
            "Content-Type": "application/json",
            "User-Agent": self.user_agent,
            "X-Internal-Token": self.token,
        }

        try:
            status, response = self.transport(
                "POST",
                self.import_url,
                body,
                headers,
                self.timeout,
            )
        except HttpError as error:
            raise PublishError(
                f"import request failed before a response: {error}"
            ) from error
        except OSError as error:
            raise PublishError(
                f"import request failed (network/timeout): {error}"
            ) from error

        if status != 200:
            raise PublishError(
                f"import API returned HTTP {status}: "
                f"{self._server_message(response)}"
            )

        counters = self._parse_counters(response)

        created = counters["created"]
        updated = counters["updated"]
        total = counters["total"]

        if total != submitted:
            raise PublishError(
                "import API reported an inconsistent total: "
                f"submitted {submitted}, server total {total}"
            )

        if created + updated != total:
            raise PublishError(
                "import API reported created+updated != total "
                f"({created}+{updated} != {total}); treating the publish as "
                "failed"
            )

        return PublishOutcome(
            ok=True,
            submitted=submitted,
            created=created,
            updated=updated,
            total=total,
            status=status,
            notes=note_tuple,
        )

    @staticmethod
    def _parse_counters(response: bytes) -> dict[str, int]:
        try:
            document = json.loads(response.decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError) as error:
            raise PublishError(
                "import API returned a body that is not valid JSON"
            ) from error

        if not isinstance(document, Mapping):
            raise PublishError(
                "import API returned a body that is not a JSON object"
            )

        counters: dict[str, int] = {}

        for key in ("created", "updated", "total"):
            value = document.get(key)

            if isinstance(value, bool) or not isinstance(value, int):
                raise PublishError(
                    f"import API response is missing an integer {key!r}"
                )

            if value < 0:
                raise PublishError(
                    f"import API response has a negative {key!r}"
                )

            counters[key] = value

        return counters

    @staticmethod
    def _server_message(response: bytes) -> str:
        try:
            document = json.loads(response.decode("utf-8", "replace"))
        except (ValueError, TypeError):
            document = None

        if isinstance(document, Mapping):
            code = document.get("code")
            message = document.get("message")

            if message is not None:
                return f"code={code} message={message}"

        text = response.decode("utf-8", "replace")

        return text[:200] if text else "<empty body>"


def publish_captures(
    captures: Sequence[CapturedJob],
    *,
    base_url: str,
    token: str,
    transport: Transport | None = None,
    timeout: float = DEFAULT_TIMEOUT_SECONDS,
    dry_run: bool = False,
) -> PublishOutcome:
    """Convenience wrapper: normalize then publish one batch of captures."""

    payloads, notes = build_import_payloads(captures)

    publisher = ManualPublisher(
        base_url=base_url,
        token=token,
        transport=transport,
        timeout=timeout,
    )

    return publisher.publish(payloads, dry_run=dry_run, notes=notes)


__all__ = [
    "ABSOLUTE_MAX_EXPERIENCE_YEARS",
    "IMPORT_PATH",
    "MANUAL_SOURCE",
    "MAX_IMPORT_BATCH_SIZE",
    "BuiltJob",
    "ManualPublisher",
    "PublishError",
    "PublishOutcome",
    "build_import_payload",
    "build_import_payloads",
    "publish_captures",
    "resolve_publish_token",
]
