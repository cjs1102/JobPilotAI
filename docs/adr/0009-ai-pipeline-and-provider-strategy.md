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
