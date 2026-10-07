"""Turn a normalized board into ``/internal/jobs/source-sync`` requests.

Batch rules (ExecPlan 0012 section 7): a request carries at most 200 jobs, and
a board larger than that limit must be published with ``complete=false`` so a
partial observation can never close jobs.
"""

from __future__ import annotations

from typing import Any, Mapping, Sequence

from .client import HttpClient

MAX_BATCH_SIZE = 200
SYNC_PATH = "/internal/jobs/source-sync"


class BatchError(Exception):
    """Raised when an observation cannot become valid source-sync requests."""


def _validate_unique_source_urls(
    jobs: Sequence[Mapping[str, Any]],
) -> None:
    """Reject duplicate keys before the server has to.

    ``source_url`` is the platform idempotency key, so a batch containing the
    same URL twice would be rejected by the API as an invalid parameter.
    """

    seen: set[str] = set()

    for job in jobs:
        url = job.get("source_url")

        if not isinstance(url, str) or not url.strip():
            raise BatchError("observation job is missing source_url")

        folded = url.strip().casefold()

        if folded in seen:
            raise BatchError(
                f"duplicate source_url in observation: {url}"
            )

        seen.add(folded)


def build_batches(
    *,
    source: str,
    scope: str,
    observed_at: str,
    jobs: Sequence[Mapping[str, Any]],
    complete: bool,
) -> list[dict[str, Any]]:
    """Split one observation into valid source-sync payloads."""

    _validate_unique_source_urls(jobs)

    total = len(jobs)
    effective_complete = bool(complete) and total <= MAX_BATCH_SIZE

    if total == 0:
        if not complete:
            # An incomplete observation with nothing to report has no valid
            # payload: complete=false requires at least one job.
            return []

        return [
            _payload(
                source=source,
                scope=scope,
                observed_at=observed_at,
                jobs=[],
                complete=True,
            )
        ]

    batches: list[dict[str, Any]] = []

    for start in range(0, total, MAX_BATCH_SIZE):
        batches.append(
            _payload(
                source=source,
                scope=scope,
                observed_at=observed_at,
                jobs=list(jobs[start : start + MAX_BATCH_SIZE]),
                complete=effective_complete,
            )
        )

    return batches


def _payload(
    *,
    source: str,
    scope: str,
    observed_at: str,
    jobs: list[Mapping[str, Any]],
    complete: bool,
) -> dict[str, Any]:
    return {
        "source": source,
        "source_scope": scope,
        "complete": complete,
        "observed_at": observed_at,
        "jobs": jobs,
    }


def publish_batches(
    client: HttpClient,
    *,
    base_url: str,
    token: str,
    batches: Sequence[Mapping[str, Any]],
) -> list[Any]:
    """POST every batch with the internal token header.

    The token is only ever sent as a header; it is never logged or embedded in
    an error message.
    """

    url = base_url.rstrip("/") + SYNC_PATH
    responses: list[Any] = []

    for batch in batches:
        responses.append(
            client.post_json(
                url,
                batch,
                headers={"X-Internal-Token": token},
            )
        )

    return responses
