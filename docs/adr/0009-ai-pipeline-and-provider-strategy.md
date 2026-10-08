# ADR-0009: Deterministic-first AI pipeline and provider-neutral execution

## Status

Accepted

## Context

AI matching can be implemented as a direct LLM call, a pure embedding system,
or a layered pipeline. Direct LLM calls are expensive, slow, hard to test, and
non-deterministic. A pure embedding system lacks explainable rule-based
constraints and can fail without a model.

Provider lock-in and platform-funded keys are poor fits for a self-hosted
open-source project.

## Decision

Use:

```text
Hard Filter
  -> Rule Score
  -> Embedding Similarity (optional)
  -> LLM Explanation / Judgment (optional)
```

The C++ backend owns:

- AI task creation and user ownership.
- Task state transitions.
- Polling/compensation behavior.
- Input validation and result persistence.

The Python AI worker:

- Claims pending tasks.
- Executes deterministic scoring.
- Executes optional embedding and LLM stages.
- Returns structured results.
- Fails gracefully to rule-only results when AI is unavailable.

Provider model:

- OpenAI-compatible endpoint.
- DeepSeek.
- Qwen.
- Ollama/local model.

Keys are Bring Your Own Key and must never be committed.

For the MVP, the Python worker communicates through C++ internal APIs and does
not access business tables directly. RabbitMQ is deferred.

## Consequences

### Positive

- Matching remains testable and explainable.
- The project works without an AI key, with reduced functionality.
- Users can choose cost/privacy trade-offs.
- AI failures do not break the core application.

### Negative

- More boundaries and tests than a single direct LLM call.
- Internal worker APIs must be designed carefully.
- Embedding availability varies by host.

## Rejected Alternatives

- **LLM-only matching**: costly, slow, non-deterministic, and hard to test.
- **Provider-specific SDK in core business logic**: creates lock-in.
- **RabbitMQ in MVP**: unnecessary operational complexity for a single-user
  local worker.
- **AI direct access to business tables**: weakens module boundaries and
  secret/privacy isolation.
- **Fabricating resume claims**: unacceptable; missing evidence remains a gap.

## Amendment (2026-10-08, ExecPlan 0013)

The bullet "Executes deterministic scoring" under **The Python AI worker** is
revised for the MVP: the deterministic `match_rule_v1` stage now lives in C++,
where the existing, tested rule primitives already are (`ScoreJob` exposes the
skill/title/salary/location components plus matched/missing skills), and the
Python worker owns only the optional embedding and LLM stages.

Reasons:

- Re-implementing the scoring formula in Python would create two divergent
  formulas for the same number.
- A complete, explainable result must exist with **no worker running and no
  provider key**: when enhancement is disabled (the default) the C++ side
  resolves the task immediately with the rule result.
- The worker becomes a pure enhancement layer, so a provider outage can only
  degrade quality, never remove capability.

"The Python AI worker ... Fails gracefully to rule-only results when AI is
unavailable" still holds: the worker validates provider output strictly, may
only append `explanation`/`suggestions`/`ai.*`, and completes with the baseline
it received through `/internal/ai/tasks/{id}/input` whenever AI is unavailable.

Everything else in this ADR is unchanged: provider-neutral OpenAI-compatible
execution, BYOK keys from the environment only, no RabbitMQ, and no AI access to
business tables.
