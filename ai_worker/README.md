# JobPilotAI AI Worker

Python worker for the 0013 AI matching capability. It claims `job_match` tasks
from the C++ internal API, optionally calls an OpenAI-compatible provider, and
merges **strictly validated, evidence-bound** output onto the deterministic
`match_rule_v1` baseline. The worker never touches business tables and never
recomputes the score
([ADR-0009](../docs/adr/0009-ai-pipeline-and-provider-strategy.md),
[ExecPlan 0013](../docs/exec-plans/completed/0013-ai-matching.md)).

## Guarantees

1. **The rule result is authoritative.** The merge may only add
   `explanation`, `suggestions` and `ai.*`. A payload that names `score`,
   `breakdown`, `matched_skills`, `missing_skills`, `score_version`, `source`
   or `ai` is rejected outright, and an internal invariant re-checks every
   protected field after the merge.
2. **The core works with no worker and no key.** A missing provider section, a
   missing key, an unreachable endpoint or unusable provider output all
   complete the task with the untouched baseline plus
   `ai.degraded: true` and a fixed reason code.
3. **Every added claim cites the input bundle.** A claim is kept only when it
   contains a term that also appears in the bundle (job, profile, skills,
   resume, rule result); unreferenced claims are dropped rather than persisted.
4. **The posting is untrusted data.** The system prompt states that embedded
   instructions are never followed and links are never fetched, the bundle is
   passed as JSON data, and requests are only ever made to the configured
   provider endpoints.
5. **No secrets in code, config, logs or results.** The key is read from the
   environment variable named by `key_env` only - never a CLI flag, never the
   config file, never an error result. A provider section that tries to carry a
   key field is rejected by name.
6. **No database, no SQL, no vendor SDK.** Standard library plus PyYAML; the
   test suite enforces the dependency allowlist and the absence of SQL.

## Layout

```text
ai_worker/
├── pyproject.toml              # only runtime dependency: PyYAML
├── config.example.yaml
└── jobpilot_ai/
    ├── cli.py                  # python -m jobpilot_ai worker ...
    ├── config.py               # YAML loading, validation, provider URL guard
    ├── client.py               # stdlib transport + /internal/ai client
    ├── provider.py             # OpenAI-compatible chat + embeddings
    ├── merge.py                # strict payload validation + evidence-bound merge
    ├── worker.py               # sequential claim loop
    └── tests/                  # unittest suite, fixtures, golden eval set
```

Requires Python 3.10+ and PyYAML.

## Usage

```bash
cd ai_worker

# process at most one claim, then exit (tests/CI)
python3 -m jobpilot_ai worker --config config.yaml --once

# run the loop with a custom empty-queue backoff and task budget
python3 -m jobpilot_ai worker --config config.yaml --interval 5 --max-tasks 10
```

Exit codes:

| Code | Meaning |
|---|---|
| 0 | the run finished; a degraded completion is still a success |
| 1 | the internal API could not be reached, or a task failed permanently |
| 2 | the configuration or the internal token is unusable |

A failure prints one `error: ...` / `config error: ...` line on stderr; no
traceback is the interface.

The internal token is read from the environment variable named by
`jobpilot.internal_token_env` before any network call. A missing token fails
**before** a client is built, and the token is never logged.

## Configuration

Copy `config.example.yaml` to `config.yaml`. Omitting the whole `provider`
section means "always complete with the rule baseline".

```yaml
jobpilot:
  base_url: http://127.0.0.1:8080
  internal_token_env: INTERNAL_API_TOKEN
  worker_id: jobpilot-ai-worker-1
  lease_seconds: 120            # claim lease requested from the service
  interval_seconds: 5           # empty-queue backoff
  max_attempts: 3               # must match the service ai.max_attempts
  task_types: [job_match]
  request_timeout_seconds: 15   # internal API timeout
  max_retries: 1                # bounded retries for 429/5xx/transport only

provider:                       # optional
  base_url: https://api.deepseek.com/v1
  model: deepseek-chat
  key_env: DEEPSEEK_API_KEY     # NAME of the env var; null for a local server
  embedding_model: null         # optional, best-effort ai.embedding_score
  timeout_seconds: 30
  max_retries: 1
  allow_private_endpoint: false
```

`max_attempts` is the worker's read-only copy of the service's attempt cap. The
service owns task state: a retryable provider failure calls
`fail(retryable=true)` while attempts remain, and once
`task.retry_count >= max_attempts` the worker completes degraded instead of
failing forever. An absent `retry_count` is treated as "last attempt", so the
worker can never loop on a service contract violation.

## Providers

One wire protocol, no vendor SDK:

- `POST {base_url}/chat/completions`
- `POST {base_url}/embeddings`

DeepSeek, Qwen, OpenAI, vLLM and Ollama work by changing `base_url` + `model`.
Retries are bounded with exponential backoff plus jitter, and a `Retry-After`
header is honoured up to a 30s clamp. Embeddings are best-effort: an
unavailable model or endpoint marks `ai.degraded` with
`reason=embeddings_unavailable` but still keeps the LLM explanation.

`response_format` is intentionally not sent, so older OpenAI-compatible servers
are not rejected; the parser instead accepts a bare JSON object or a fenced
```json block and rejects anything else.

## Endpoint guard

The provider `base_url` is validated when the config loads. Loopback, private,
link-local, multicast, unspecified, reserved and CGNAT addresses (including
`169.254.169.254`, `::1`, `fd00::/8`, IPv4-mapped forms), plus `localhost` and
the well-known metadata hostnames, are refused unless
`provider.allow_private_endpoint: true` is set explicitly - the opt-in for a
local server such as Ollama.

The guard is literal, not DNS-resolving: a public hostname that resolves to a
private address is not caught. Documented, not hidden.

## Merge rules

The model must answer with one JSON object whose only allowed keys are
`explanation` (string) and `suggestions` (array of strings). Anything else -
including a score - fails validation, which is treated like invalid JSON: never
persisted.

Final result:

```json
{
  "score_version": "match_rule_v1",
  "score": 82,
  "...": "the deterministic baseline, untouched",
  "explanation": "The rule baseline matched C++ and Redis.",
  "suggestions": ["Add a Kubernetes project to your resume."],
  "ai": {"degraded": false}
}
```

`ai.reason` is always one of a fixed set of machine codes
(`provider_not_configured`, `provider_timeout`, `provider_rate_limited`,
`provider_server_error`, `provider_unavailable`, `provider_unauthorized`,
`provider_request_rejected`, `provider_invalid_json`,
`provider_schema_invalid`, `provider_http_error`,
`embeddings_unavailable`). Provider text never reaches a result field.

### What the evidence gate does and does not do

A claim is kept only if it contains at least one non-numeric, non-stopword term
that also appears somewhere in the input bundle; numeric-only tokens are not
treated as evidence. This stops claims that reference nothing in the bundle
(for example an invented employer), which is what amendment 4 requires. It is a
term-presence gate, not entailment: a false statement assembled from real
bundle terms can still pass. Mechanical fact provenance is 0014 scope.

## Tests

```bash
cd ai_worker
python3 -m unittest discover -s jobpilot_ai/tests -t .
```

The suite is fixture driven, offline and standard-library only: config
parsing/validation, the internal client (verbs, token header, 429/5xx/timeout
retry and give-up, malformed bodies), the provider client (chat, embeddings,
timeout, 429, 5xx, invalid JSON, missing key), merge validation (extra keys
rejected, protected fields never overwritten, unreferenced claims dropped), the
worker loop, the CLI exit codes, the untrusted-input/endpoint guards, an
architecture guard (no DB driver, no SQL, dependency allowlist, no secret CLI
flags) and a golden evaluation set built from recorded provider responses.

A live smoke is opt-in and never part of the suite: point a config at a real
service and provider and use `--once`.
