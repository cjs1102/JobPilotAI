"""Nowcoder campus job list (China, ExecPlan 0018).

The public list at ``/jobs/school/jobs`` is a login-free server-rendered page.
Its data arrives as ``window.__INITIAL_STATE__`` with the postings under a
``jobListData`` key. The module number that owns that key changes between
builds (``app.132`` at the 2026-10-19 probe), so this provider searches the
decoded state recursively and never hardcodes a build number.

Coverage is deliberately marked incomplete. One query returns at most 20 rows,
there is no usable pagination, and the order changes slightly between requests.
A 20-row window therefore cannot prove a complete board, and publishing it as
``complete=true`` would let the server close jobs that merely fell outside the
window. Every result is ``complete=False``.

Two shapes share the list:

* native postings (5-6 digit ids): ``jobName`` / ``companyNameText`` /
  ``jobCity`` / ``salaryText``, with a JSON ``ext`` carrying the JD;
* aggregated referral postings (12+ digit ids): ``jobTitle`` / ``companyName``
  / ``salary`` / ``description`` (HTML), with ``platform`` (for example
  ``moka``) and no ``ext``.

Unknown values are omitted, never guessed: the native ``eduLevel`` encoding is
not documented (``5000`` was observed), so only an explicit education string is
mapped. A description over 48 KiB is a hard per-row rejection (never truncated).
"""

from __future__ import annotations

import json
import re
import urllib.parse
from typing import Any, Iterable, Mapping, Sequence

from .. import normalize_cn
from ..normalizer import (
    build_job,
    clean_identifier,
    clean_multiline,
    clean_text,
    first_present,
    html_to_text,
)
from ..salary import parse_monthly_salary_thousand, parse_salary_currency
from ..timestamps import (
    BEIJING_TZ,
    NormalizedTimestamp,
    parse_epoch_millis_utc,
)
from .base import BoardResult, ProviderError

PROVIDER = "nowcoder"  # registry-guard: source-id
FETCH_MODE = "text"

#: Compliance budget: one list request per board, sequential, at least 5 s
#: apart. ``run_sync`` builds a provider-specific Fetcher from this value.
MIN_INTERVAL_SECONDS = 5.0

BASE_URL = "https://www.nowcoder.com/jobs/school/jobs"  # registry-guard: source-id
DETAIL_URL = "https://www.nowcoder.com/jobs/detail/{external_id}"  # registry-guard: source-id
STATE_MARKER = "window.__INITIAL_STATE__="

MAX_TITLE_BYTES = 128
MAX_COMPANY_BYTES = 128
MAX_LOCATION_BYTES = 64
MAX_DESCRIPTION_BYTES = 48 * 1024
DESCRIPTION_TOO_LARGE_CODE = "description_too_large"

_ID_RE = re.compile(r"^\d{1,20}$")
_NUMBER_RE = re.compile(r"^-?\d+(?:\.\d+)?$")


class _RowSkip(Exception):
    """A single unusable row: keep the rest of the board, record a diagnostic."""

    def __init__(
        self,
        reason: str,
        *,
        field: str | None = None,
        external_id: str | None = None,
        limit_bytes: int | None = None,
        actual_bytes: int | None = None,
    ) -> None:
        super().__init__(reason)
        self.diagnostic: dict[str, Any] = {"reason": reason}

        if field is not None:
            self.diagnostic["field"] = field

        if external_id is not None:
            self.diagnostic["external_id"] = external_id

        if limit_bytes is not None:
            self.diagnostic["limit_bytes"] = limit_bytes

        if actual_bytes is not None:
            self.diagnostic["actual_bytes"] = actual_bytes


def board_url(board: str) -> str:
    """Build the public list URL from ``<keyword>`` or ``<keyword>|<city>``."""

    keyword, city = _split_board(board)
    query = {"search": keyword}

    if city is not None:
        query["city"] = city

    return f"{BASE_URL}?{urllib.parse.urlencode(query)}"


def parse_board(
    payload: Any,
    *,
    board: str,
    company: str | None = None,
) -> BoardResult:
    """Parse one decoded SSR page into source-sync jobs.

    A missing/undecodable ``__INITIAL_STATE__`` or a missing ``jobListData`` is
    a board-level failure (``ProviderError``), never an empty result. An
    explicit empty ``jobListData`` list is a valid no-match result and is
    returned with ``complete=False``; the publisher then writes nothing.
    """

    if not isinstance(payload, str):
        raise ProviderError("source payload must be decoded HTML text")

    if not payload.strip():
        raise ProviderError("source payload is empty")

    state = _decode_state(payload)
    lists = _find_job_lists(state)

    if not lists:
        raise ProviderError(
            "source payload has no jobListData list; "
            "refusing to treat it as an empty result"
        )

    # More than one direct jobListData key is not expected, but a future page
    # could add a recommendation block. The largest list is the search result;
    # ties keep the first one encountered.
    rows = max(lists, key=len)

    jobs: list[dict[str, Any]] = []
    diagnostics: list[dict[str, Any]] = []
    seen: set[tuple[str, str, str]] = set()

    for index, row in enumerate(rows):
        if not isinstance(row, Mapping):
            diagnostics.append(
                {"index": index, "reason": "unrecognised_record_shape"}
            )
            continue

        try:
            job = _parse_row(
                row,
                board=board,
                company=company,
                seen=seen,
            )
        except _RowSkip as skip:
            diagnostics.append({"index": index, **skip.diagnostic})
            continue

        jobs.append(job)

    return BoardResult(
        source=PROVIDER,
        board=board,
        jobs=jobs,
        complete=False,
        total=len(jobs),
        diagnostics=diagnostics,
    )


def _split_board(board: str) -> tuple[str, str | None]:
    if not isinstance(board, str) or not board.strip():
        raise ProviderError("source board must be a non-empty string")

    if any(ord(character) < 32 for character in board):
        raise ProviderError("source board must not contain control characters")

    parts = board.split("|")

    if len(parts) > 2:
        raise ProviderError(
            "source board must be '<keyword>' or '<keyword>|<city>'"
        )

    keyword = parts[0].strip()

    if not keyword:
        raise ProviderError("source board requires a non-empty keyword")

    if len(parts) == 2:
        city = parts[1].strip()

        if not city:
            raise ProviderError(
                "source board has an empty city after '|'; "
                "omit the '|' instead"
            )

        return keyword, city

    return keyword, None


def _decode_state(payload: str) -> Any:
    marker = payload.find(STATE_MARKER)

    if marker < 0:
        raise ProviderError(
            "source payload has no window.__INITIAL_STATE__; "
            "the page shape changed"
        )

    start = marker + len(STATE_MARKER)

    try:
        state, _end = json.JSONDecoder().raw_decode(payload[start:])
    except json.JSONDecodeError as error:
        raise ProviderError(
            f"source __INITIAL_STATE__ is not valid JSON: {error}"
        ) from error

    if not isinstance(state, Mapping):
        raise ProviderError("source __INITIAL_STATE__ is not an object")

    return state


def _find_job_lists(state: Any) -> list[list[Any]]:
    """Return every direct ``jobListData`` list, at any depth.

    A breadth-first walk keeps the shallowest list first, so a page that adds a
    nested recommendation list still prefers the search-result list.
    """

    found: list[list[Any]] = []
    queue: list[Any] = [state]
    seen_nodes: set[int] = set()

    while queue:
        node = queue.pop(0)

        if isinstance(node, Mapping):
            if id(node) in seen_nodes:
                continue

            seen_nodes.add(id(node))
            candidate = node.get("jobListData")

            if isinstance(candidate, list):
                found.append(candidate)

            for value in node.values():
                if isinstance(value, (Mapping, list)):
                    queue.append(value)
        elif isinstance(node, list):
            if id(node) in seen_nodes:
                continue

            seen_nodes.add(id(node))

            for value in node:
                if isinstance(value, (Mapping, list)):
                    queue.append(value)

    return found


def _parse_row(
    row: Mapping[str, Any],
    *,
    board: str,
    company: str | None,
    seen: set[tuple[str, str, str]],
) -> dict[str, Any]:
    external_id = _external_id(row.get("id"))

    if external_id is None:
        raise _RowSkip("missing_external_id", field="id")

    shape, ext = _record_shape(row, external_id)
    title = _required_text(
        first_present(row.get("jobName"), row.get("jobTitle"), row.get("title")),
        field="title",
        max_bytes=MAX_TITLE_BYTES,
        external_id=external_id,
    )
    company_name = _required_text(
        first_present(
            row.get("companyNameText"),
            row.get("companyName"),
            clean_text(company),
        ),
        field="company",
        max_bytes=MAX_COMPANY_BYTES,
        external_id=external_id,
    )
    location = _optional_text(
        _location_value(row),
        max_bytes=MAX_LOCATION_BYTES,
    )
    salary_text = first_present(row.get("salaryText"), row.get("salary"))
    salary = parse_monthly_salary_thousand(salary_text)
    salary_min = salary[0] if salary is not None else None
    salary_max = salary[1] if salary is not None else None
    description = _description(row, shape, ext, external_id)
    published_at = _published_at(row)
    deadline, deadline_date_only = _deadline(row)
    job_nature = _job_nature(row.get("recruitType"))
    education = _education(row)
    source_url = DETAIL_URL.format(external_id=external_id)

    if len(source_url.encode("utf-8")) > 255:
        raise _RowSkip("source_url_too_long", field="source_url")

    row_data = row.get("data")
    apply_url = first_present(
        row.get("redirectExternalUrl"),
        row_data.get("redirectExternalUrl")
        if isinstance(row_data, Mapping)
        else None,
    )

    dedupe_key = (
        _fold(title),
        _fold(company_name),
        _fold(location or ""),
    )

    if dedupe_key in seen:
        raise _RowSkip(
            "duplicate_in_batch",
            external_id=external_id,
        )

    seen.add(dedupe_key)

    return build_job(
        source=PROVIDER,
        source_url=source_url,
        title=title,
        company=company_name,
        external_id=clean_identifier(external_id),
        published_at=published_at,
        location=location,
        salary_min=salary_min,
        salary_max=salary_max,
        salary_currency=parse_salary_currency(salary_text),
        description=description,
        education_required=education,
        job_nature=job_nature,
        deadline=deadline,
        deadline_date_only=deadline_date_only,
        apply_url=apply_url,
    )


def _record_shape(
    row: Mapping[str, Any],
    external_id: str,
) -> tuple[str, Mapping[str, Any] | None]:
    raw_ext = row.get("ext")

    if isinstance(raw_ext, str) and raw_ext.strip().startswith("{"):
        try:
            ext = json.loads(raw_ext)
        except json.JSONDecodeError as error:
            raise _RowSkip(
                "invalid_ext_json",
                field="ext",
                external_id=external_id,
            ) from error

        if not isinstance(ext, Mapping):
            raise _RowSkip(
                "unrecognised_record_shape",
                external_id=external_id,
            )

        return "native", ext

    if (
        len(external_id) >= 12
        and raw_ext is None
        and (row.get("jobTitle") or row.get("description"))
    ):
        return "aggregator", None

    raise _RowSkip("unrecognised_record_shape", external_id=external_id)


def _external_id(value: Any) -> str | None:
    if isinstance(value, bool):
        return None

    if isinstance(value, int):
        return str(value) if value >= 0 else None

    if isinstance(value, str):
        text = value.strip()
        return text if _ID_RE.match(text) else None

    return None


def _required_text(
    value: Any,
    *,
    field: str,
    max_bytes: int,
    external_id: str,
) -> str:
    cleaned = clean_text(value)

    if cleaned is None:
        raise _RowSkip(
            f"missing_{field}",
            field=field,
            external_id=external_id,
        )

    size = len(cleaned.encode("utf-8"))

    if size > max_bytes:
        raise _RowSkip(
            f"{field}_too_long",
            field=field,
            external_id=external_id,
            limit_bytes=max_bytes,
            actual_bytes=size,
        )

    return cleaned


def _optional_text(value: Any, *, max_bytes: int) -> str | None:
    cleaned = clean_text(value)

    if cleaned is None:
        return None

    if len(cleaned.encode("utf-8")) > max_bytes:
        return None

    return cleaned


def _location_value(row: Mapping[str, Any]) -> Any:
    city_list = row.get("jobCityList")
    first_city = city_list[0] if isinstance(city_list, list) and city_list else None
    candidates = (
        row.get("jobCity"),
        row.get("city"),
        first_city,
        row.get("locations"),
    )

    for raw in candidates:
        normalized = normalize_cn.normalize_location_text(raw)

        if normalized is not None:
            return normalized

    return None


def _description(
    row: Mapping[str, Any],
    shape: str,
    ext: Mapping[str, Any] | None,
    external_id: str,
) -> str | None:
    if shape == "native":
        assert ext is not None
        parts = [
            clean_multiline(ext.get("infos")),
            clean_multiline(ext.get("requirements")),
        ]
        text = "\n\n".join(part for part in parts if part) or None
    else:
        text = clean_multiline(html_to_text(row.get("description")))

    if text is None:
        return None

    size = len(text.encode("utf-8"))

    if size > MAX_DESCRIPTION_BYTES:
        raise _RowSkip(
            DESCRIPTION_TOO_LARGE_CODE,
            field="description",
            external_id=external_id,
            limit_bytes=MAX_DESCRIPTION_BYTES,
            actual_bytes=size,
        )

    return text


def _published_at(row: Mapping[str, Any]) -> str | None:
    epoch = _epoch_millis(
        first_present(row.get("createTime"), row.get("publishedAt"))
    )

    if epoch is None or epoch <= 0:
        return None

    return parse_epoch_millis_utc(epoch)


def _deadline(row: Mapping[str, Any]) -> tuple[Any, Any]:
    raw = first_present(row.get("deliverEnd"), row.get("deadline"))

    if raw is None:
        return None, None

    epoch = _epoch_millis(raw)

    if epoch is not None and epoch > 0:
        stored = parse_epoch_millis_utc(epoch)
        return stored, (False if stored is not None else None)

    normalized: NormalizedTimestamp | None = normalize_cn.normalize_deadline_fields(
        raw,
        source_tz=BEIJING_TZ,
    )

    if normalized is None:
        return None, None

    return normalized.utc, normalized.date_only


def _job_nature(value: Any) -> int | None:
    number = _int_value(value)

    # The source's own detail mapping is 1 校招 / 2 实习 / 3 社招.
    return {1: 1, 2: 3, 3: 2}.get(number) if number is not None else None


def _education(row: Mapping[str, Any]) -> int | None:
    value = row.get("education")

    if value is None:
        # The native ``eduLevel`` (observed 5000) has no documented encoding;
        # only an explicit string is safe to map.
        level = row.get("eduLevel")
        value = level if isinstance(level, str) else None

    return normalize_cn.normalize_education(value)


def _epoch_millis(value: Any) -> int | float | None:
    if isinstance(value, bool):
        return None

    if isinstance(value, (int, float)):
        return value

    if isinstance(value, str):
        text = value.strip()
        return float(text) if _NUMBER_RE.match(text) else None

    return None


def _int_value(value: Any) -> int | None:
    if isinstance(value, bool):
        return None

    if isinstance(value, int):
        return value

    if isinstance(value, str) and value.strip().lstrip("-").isdigit():
        return int(value.strip())

    return None


def _fold(value: str) -> str:
    return re.sub(r"\s+", "", value).casefold()


__all__ = [
    "DESCRIPTION_TOO_LARGE_CODE",
    "FETCH_MODE",
    "MAX_DESCRIPTION_BYTES",
    "MIN_INTERVAL_SECONDS",
    "PROVIDER",
    "board_url",
    "parse_board",
]
