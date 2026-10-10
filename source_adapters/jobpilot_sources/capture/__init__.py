"""Manual capture: browser bookmarklet + local publisher (ExecPlan 0014 Step 6).

The manual channel is how a job behind a login wall gets into JobPilotAI
**legitimately**: the user captures what their own logged-in browser already
shows, and publishes it from their own machine. Nothing here logs in, bypasses a
CAPTCHA, spoofs a cookie/User-Agent, or talks to the source site at all.

Two halves, deliberately separated:

``capture/bookmarklet.js`` (+ :mod:`jobpilot_sources.capture.bookmarklet`)
    Runs in the *browser page context*. It reads the current DOM and copies a
    JSON capture to the clipboard. It carries **no credential** and performs
    **no network request**: the only thing that leaves the page is text the user
    can already see.

:mod:`jobpilot_sources.capture.artifact` /
:mod:`jobpilot_sources.capture.quality` /
:mod:`jobpilot_sources.capture.publisher`
    Run *locally*. They parse and validate the untrusted capture artifact,
    run the mirrored 0015A pre-ingest pre-flight (same stable codes as the C++
    engine, byte-based 48 KiB cap), normalize it with the same deterministic
    modules the automatic adapters use, and publish it through the **existing**
    manual-import endpoint ``POST /internal/jobs/import`` with
    ``source="manual"``. Only this local process reads the internal token, from
    its own environment, and it never logs or stores it.

The authorization boundary is therefore: the browser side is untrusted and
credential-free; the local side is trusted, holds the token, and treats the
artifact as untrusted input. See ``source_adapters/README.md``.
"""

from __future__ import annotations

from .artifact import (
    CaptureError,
    CapturedJob,
    MAX_CAPTURE_VERSION,
    MAX_CAPTURES_PER_RUN,
    parse_artifact,
    parse_captures,
)
from .bookmarklet import (
    bookmarklet_href,
    bookmarklet_source,
    instructions_html,
)
from .publisher import (
    IMPORT_PATH,
    MANUAL_SOURCE,
    MAX_IMPORT_BATCH_SIZE,
    ManualPublisher,
    PublishError,
    PublishOutcome,
    build_import_payload,
    build_import_payloads,
    publish_captures,
    resolve_publish_token,
)
from .quality import (
    MAX_DESCRIPTION_BYTES,
    OUTCOME_ADVISORY,
    OUTCOME_NEEDS_REVIEW,
    OUTCOME_REJECTED,
    CaptureQualityReport,
    Finding,
    ItemQuality,
    QualityCounters,
    capture_error_finding,
    evaluate_batch_quality,
    evaluate_item_quality,
)

__all__ = [
    "IMPORT_PATH",
    "MANUAL_SOURCE",
    "MAX_CAPTURE_VERSION",
    "MAX_CAPTURES_PER_RUN",
    "MAX_DESCRIPTION_BYTES",
    "MAX_IMPORT_BATCH_SIZE",
    "OUTCOME_ADVISORY",
    "OUTCOME_NEEDS_REVIEW",
    "OUTCOME_REJECTED",
    "CaptureError",
    "CaptureQualityReport",
    "CapturedJob",
    "Finding",
    "ItemQuality",
    "ManualPublisher",
    "PublishError",
    "PublishOutcome",
    "QualityCounters",
    "bookmarklet_body",
    "bookmarklet_href",
    "bookmarklet_source",
    "build_import_payload",
    "build_import_payloads",
    "capture_error_finding",
    "evaluate_batch_quality",
    "evaluate_item_quality",
    "instructions_html",
    "parse_artifact",
    "parse_captures",
    "publish_captures",
    "resolve_publish_token",
]
