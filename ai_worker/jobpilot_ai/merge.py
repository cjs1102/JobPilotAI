"""Strict AI-output validation and evidence-bound merge onto the rule baseline.

The deterministic ``rule_result`` is authoritative: this module may only add
``explanation``, ``suggestions`` and ``ai.*``. Any payload that names a
deterministic field (``score``, ``breakdown``, ``matched_skills``,
``missing_skills``, ...) is rejected outright (ExecPlan 0013 section 5.5).

Every added claim must cite evidence present in the input bundle; claims that
cite nothing are dropped rather than persisted (ExecPlan 0013 section 17
amendment 4). The gate is term presence, not semantic entailment: it stops
claims that reference nothing in the bundle, but it cannot prove that a
sentence built from real bundle terms is true. Full provenance gating is 0014
scope and is documented as such in ``ai_worker/README.md``.

This module also owns the degradation-reason vocabulary, because it is the only
writer of ``ai.reason``: a reason is a fixed machine code, never free text from
a provider, so provider output can never leak into a persisted result.
"""

from __future__ import annotations

import copy
import math
import re
from dataclasses import dataclass
from typing import Any, Mapping

PROTECTED_KEYS = (
    "score_version",
    "score",
    "source",
    "breakdown",
    "matched_skills",
    "missing_skills",
)

ALLOWED_PAYLOAD_KEYS = ("explanation", "suggestions")

AI_ALLOWED_KEYS = ("degraded", "reason", "embedding_score", "llm_score")

MAX_EXPLANATION_CHARS = 2000
MAX_SUGGESTIONS = 5
MAX_SUGGESTION_CHARS = 500
MIN_ANCHOR_CHARS = 2

REASON_PROVIDER_NOT_CONFIGURED = "provider_not_configured"
REASON_PROVIDER_TIMEOUT = "provider_timeout"
REASON_PROVIDER_RATE_LIMITED = "provider_rate_limited"
REASON_PROVIDER_SERVER_ERROR = "provider_server_error"
REASON_PROVIDER_UNAVAILABLE = "provider_unavailable"
REASON_PROVIDER_UNAUTHORIZED = "provider_unauthorized"
REASON_PROVIDER_REQUEST_REJECTED = "provider_request_rejected"
REASON_PROVIDER_HTTP_ERROR = "provider_http_error"
REASON_PROVIDER_INVALID_JSON = "provider_invalid_json"
REASON_PROVIDER_SCHEMA_INVALID = "provider_schema_invalid"
REASON_EMBEDDINGS_UNAVAILABLE = "embeddings_unavailable"

DEGRADATION_REASONS = frozenset(
    {
        REASON_PROVIDER_NOT_CONFIGURED,
        REASON_PROVIDER_TIMEOUT,
        REASON_PROVIDER_RATE_LIMITED,
        REASON_PROVIDER_SERVER_ERROR,
        REASON_PROVIDER_UNAVAILABLE,
        REASON_PROVIDER_UNAUTHORIZED,
        REASON_PROVIDER_REQUEST_REJECTED,
        REASON_PROVIDER_HTTP_ERROR,
        REASON_PROVIDER_INVALID_JSON,
        REASON_PROVIDER_SCHEMA_INVALID,
        REASON_EMBEDDINGS_UNAVAILABLE,
    }
)

TOKEN_PATTERN = re.compile(r"[0-9A-Za-z][0-9A-Za-z+#._'-]*")

# Words that carry no evidence on their own, so matching them is not a citation.
STOPWORDS = frozenset(
    {
        "a",
        "an",
        "the",
        "and",
        "or",
        "of",
        "to",
        "in",
        "on",
        "at",
        "for",
        "with",
        "your",
        "you",
        "our",
        "their",
        "this",
        "that",
        "these",
        "those",
        "is",
        "are",
        "was",
        "were",
        "be",
        "been",
        "as",
        "by",
        "it",
        "its",
        "from",
        "will",
        "would",
        "can",
        "could",
        "should",
        "may",
        "might",
        "must",
        "not",
        "no",
        "yes",
        "we",
        "they",
        "he",
        "she",
        "his",
        "her",
        "them",
        "us",
        "i",
        "me",
        "my",
        "more",
        "most",
        "very",
        "also",
        "than",
        "then",
        "there",
        "here",
        "what",
        "which",
        "who",
        "when",
        "where",
        "how",
        "why",
    }
)


class MergeError(Exception):
    """Raised when AI output cannot be trusted onto the rule baseline."""


@dataclass(frozen=True)
class AiPayload:
    explanation: str | None
    suggestions: tuple[str, ...]


@dataclass(frozen=True)
class EvidenceIndex:
    """Terms that a claim may cite as evidence."""

    terms: frozenset[str]

    def anchors(self, text: Any) -> tuple[str, ...]:
        return tuple(token for token in _tokens(text) if token in self.terms)

    def supports(self, text: Any) -> bool:
        return bool(self.anchors(text))


def _tokens(value: Any) -> tuple[str, ...]:
    """Case-folded content tokens; numeric-only tokens are not evidence."""

    if not isinstance(value, str):
        return ()

    found: list[str] = []

    for raw in TOKEN_PATTERN.findall(value.casefold()):
        token = raw.strip("._'-")

        if len(token) < MIN_ANCHOR_CHARS:
            continue

        if token.isdigit():
            continue

        if token in STOPWORDS:
            continue

        found.append(token)

    return tuple(found)


def _collect_strings(value: Any, out: list[str]) -> None:
    if isinstance(value, str):
        out.append(value)
    elif isinstance(value, Mapping):
        for item in value.values():
            _collect_strings(item, out)
    elif isinstance(value, (list, tuple)):
        for item in value:
            _collect_strings(item, out)


def build_evidence_index(bundle: Any) -> EvidenceIndex:
    """Collect every term that appears anywhere in the input bundle.

    The input bundle is the only source of truth the worker may cite: job,
    profile, skills, resume and the deterministic rule result.
    """

    strings: list[str] = []
    _collect_strings(bundle if bundle is not None else {}, strings)

    terms: set[str] = set()

    for text in strings:
        terms.update(_tokens(text))

    return EvidenceIndex(frozenset(terms))


def validate_rule_result(rule_result: Any) -> dict[str, Any]:
    """Validate the deterministic baseline and return an isolated copy."""

    if not isinstance(rule_result, Mapping):
        raise MergeError("rule_result must be a JSON object")

    missing = [
        key
        for key in PROTECTED_KEYS
        if key not in rule_result
    ]

    if missing:
        raise MergeError(
            "rule_result is missing required field(s): " + ", ".join(missing)
        )

    version = rule_result["score_version"]

    if not isinstance(version, str) or not version.strip():
        raise MergeError("rule_result.score_version must be a non-empty string")

    source = rule_result["source"]

    if not isinstance(source, str) or not source.strip():
        raise MergeError("rule_result.source must be a non-empty string")

    score = rule_result["score"]

    if isinstance(score, bool) or not isinstance(score, (int, float)):
        raise MergeError("rule_result.score must be a number")

    if not 0.0 <= float(score) <= 100.0:
        raise MergeError("rule_result.score must be within [0, 100]")

    if not isinstance(rule_result["breakdown"], Mapping):
        raise MergeError("rule_result.breakdown must be an object")

    for key in ("matched_skills", "missing_skills"):
        value = rule_result[key]

        if not isinstance(value, list) or any(
            not isinstance(item, str) for item in value
        ):
            raise MergeError(f"rule_result.{key} must be an array of strings")

    return copy.deepcopy(dict(rule_result))


def validate_payload(payload: Any) -> AiPayload:
    """Validate the model payload strictly; extra fields are rejected."""

    if not isinstance(payload, Mapping):
        raise MergeError("AI payload must be a JSON object")

    keys = [str(key) for key in payload]
    protected = sorted(set(keys) & (set(PROTECTED_KEYS) | {"ai"}))

    if protected:
        raise MergeError(
            "AI payload must not contain the deterministic field(s) "
            + ", ".join(protected)
            + "; the rule result is authoritative"
        )

    unknown = sorted(key for key in keys if key not in ALLOWED_PAYLOAD_KEYS)

    if unknown:
        raise MergeError(
            "AI payload contains unsupported field(s): " + ", ".join(unknown)
        )

    return AiPayload(
        explanation=_optional_explanation(payload.get("explanation")),
        suggestions=_suggestions(payload.get("suggestions")),
    )


def _optional_explanation(value: Any) -> str | None:
    if value is None:
        return None

    if not isinstance(value, str):
        raise MergeError("AI payload 'explanation' must be a string")

    text = " ".join(value.split())

    if not text:
        return None

    if len(text) > MAX_EXPLANATION_CHARS:
        raise MergeError(
            f"AI payload 'explanation' exceeds {MAX_EXPLANATION_CHARS} characters"
        )

    return text


def _suggestions(value: Any) -> tuple[str, ...]:
    if value is None:
        return ()

    if not isinstance(value, list):
        raise MergeError("AI payload 'suggestions' must be an array")

    if len(value) > MAX_SUGGESTIONS:
        raise MergeError(
            "AI payload 'suggestions' must contain at most "
            f"{MAX_SUGGESTIONS} items"
        )

    items: list[str] = []

    for index, item in enumerate(value):
        if not isinstance(item, str):
            raise MergeError(
                f"AI payload 'suggestions[{index}]' must be a string"
            )

        text = " ".join(item.split())

        if not text:
            raise MergeError(
                f"AI payload 'suggestions[{index}]' must not be empty"
            )

        if len(text) > MAX_SUGGESTION_CHARS:
            raise MergeError(
                f"AI payload 'suggestions[{index}]' exceeds "
                f"{MAX_SUGGESTION_CHARS} characters"
            )

        if text not in items:
            items.append(text)

    return tuple(items)


def validate_ai_metadata(metadata: Any) -> dict[str, Any]:
    """Validate the ``ai`` object the worker attaches to a result."""

    if metadata is None:
        return {"degraded": False}

    if not isinstance(metadata, Mapping):
        raise MergeError("AI metadata must be an object")

    unknown = sorted(
        str(key) for key in metadata if key not in AI_ALLOWED_KEYS
    )

    if unknown:
        raise MergeError(
            "AI metadata contains unsupported field(s): " + ", ".join(unknown)
        )

    degraded = metadata.get("degraded", False)

    if not isinstance(degraded, bool):
        raise MergeError("ai.degraded must be a boolean")

    ai: dict[str, Any] = {"degraded": degraded}
    reason = metadata.get("reason")

    if reason is not None:
        if not isinstance(reason, str) or reason not in DEGRADATION_REASONS:
            raise MergeError(
                "ai.reason must be one of the documented degradation codes"
            )

        ai["reason"] = reason
    elif degraded:
        raise MergeError("ai.degraded requires an ai.reason code")

    for key in ("embedding_score", "llm_score"):
        value = metadata.get(key)

        if value is None:
            continue

        if isinstance(value, bool) or not isinstance(value, (int, float)):
            raise MergeError(f"ai.{key} must be a number")

        number = float(value)

        if not math.isfinite(number) or not 0.0 <= number <= 1.0:
            raise MergeError(f"ai.{key} must be within [0, 1]")

        ai[key] = round(number, 4)

    return ai


def verify_rule_authority(
    before: Mapping[str, Any],
    after: Mapping[str, Any],
) -> None:
    """Defensive invariant: protected fields may never change."""

    for key in PROTECTED_KEYS:
        if key not in before:
            continue

        if before[key] != after.get(key):
            raise MergeError(
                f"merge attempted to change the deterministic field '{key}'"
            )


def merge_result(
    bundle: Any,
    rule_result: Any,
    payload: Any,
    *,
    ai_metadata: Mapping[str, Any] | None = None,
    evidence: EvidenceIndex | None = None,
) -> dict[str, Any]:
    """Merge validated, evidence-bound AI output onto the rule baseline."""

    baseline = validate_rule_result(rule_result)
    index = evidence if evidence is not None else build_evidence_index(bundle)
    parsed = validate_payload(payload)

    merged = dict(baseline)

    if parsed.explanation and index.supports(parsed.explanation):
        merged["explanation"] = parsed.explanation

    kept = [text for text in parsed.suggestions if index.supports(text)]

    if kept:
        merged["suggestions"] = kept

    merged["ai"] = validate_ai_metadata(dict(ai_metadata or {}))

    verify_rule_authority(baseline, merged)

    return merged


def degraded_result(
    rule_result: Any,
    reason: str,
    *,
    ai_metadata: Mapping[str, Any] | None = None,
) -> dict[str, Any]:
    """Complete a task with the untouched rule baseline and a degrade marker."""

    if reason not in DEGRADATION_REASONS:
        raise MergeError(
            "degradation reason must be one of the documented codes"
        )

    baseline = validate_rule_result(rule_result)
    metadata: dict[str, Any] = dict(ai_metadata or {})
    metadata["degraded"] = True
    metadata["reason"] = reason

    result = dict(baseline)
    result["ai"] = validate_ai_metadata(metadata)

    return result
