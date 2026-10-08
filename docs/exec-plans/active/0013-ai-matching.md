# ExecPlan 0013: AI Matching (job_match)

> Status: Research, Design, and Planning complete. Implementation not started.
>
> Task level: L4.
>
> Supersedes nothing. Requires a recorded amendment to ADR-0009 (see section 6.1).

## 1. Objective

Deliver the AI Task platform plus one complete matching capability:

```text
POST /api/v1/ai/match {job_id, resume_id}
  -> ai_tasks row (pending or already resolved)
  -> deterministic match_rule_v1 result (always, even with no AI)
  -> optional AI enhancement claimed by the Python worker
  -> GET /api/v1/ai/tasks/{id} exposes status + rule_result + result
```

The capability must be fully usable with **no worker running and no provider
key configured**, must never let the AI layer become the authority for the
score, and must keep the worker away from business tables.

## 2. Background

Verified current state:

- `ai_tasks` and `interview_records` exist in `database/schema/05_ai.sql` and in
  `docs/03-database-design.md` (line 163 DDL, line 216 JSON result decision,
  line 226 index strategy). The dev database has both tables; `ai_tasks` has
  0 rows.
- There is **no AI code at all**: `grep -rn "ai_tasks|AiTask|aiTask" service
  backend --include=*.h --include=*.cpp` returns only `backend/tests/
  test_schema.cpp` table/column assertions. No `/api/v1/ai/*` route, no
  `/internal/ai/*` route, no AI module under `service/{include,src}/jobpilot/
  service/`.
- `ai_tasks` columns today: `id, user_id, task_type, job_id, resume_id,
  status(0pending/1processing/2success/3failed), result JSON, error_msg,
  retry_count, create_time, update_time` with indexes `idx_user_status`,
  `idx_status`, `idx_type_status`.
- **There is no owner/lease column**, so `atomic claim`, `lease timeout`, and
  `stale recovery` (required by ADR/architecture and by
  `docs/quality/README.md`) cannot be implemented safely today.
- Contracts already reserved: `docs/04-api-design.md` section 7 (public AI
  endpoints, lines 640-700), section 1.5 (async task convention, statuses, line
  57), section 10.3 (internal worker endpoints, line 801: `POST
  /internal/ai/tasks/claim`, `POST /internal/ai/tasks/{id}/complete`, `POST
  /internal/ai/tasks/{id}/fail`, `GET /internal/ai/tasks/{id}/input`).
- Decisions: ADR-0009 (Accepted) fixes `Hard Filter -> Rule Score -> Embedding
  (optional) -> LLM (optional)`, C++ owning task state/transitions/polling/
  validation/persistence, worker claiming tasks and failing gracefully to
  rule-only; ADR-0006 is Superseded for MVP (worker must not touch business
  tables); ADR-0005 is a target direction with MQ deferred.
- Reusable C++ building blocks: `jobpilot/service/job/job_ranking.h` exposes
  `ScoreJob(job, requirements, profile, skills, filter, now)` returning
  `RankedJob{score, scoreVersion, breakdown{skill,title,salary,location,
  firstSeen}, matchedSkills, missingSkills}`; `IUserProfileReader` supplies
  `UserProfile` + `UserSkill`; `IJobDao` supplies job + requirements;
  `IResumeDao` supplies `ResumeRecord` (with `latestVersion`) and
  `ResumeVersionRecord.content`.
- Repository patterns to follow: Controller -> Service -> DAO with `IxxxDao`
  seams, `ServiceResult<T>` + `ErrorCode`, `ConnectionPool::Transaction`,
  additive migrations with a documented existing-database ALTER (0012
  precedent in `docs/03` section 7), Python side as a fixture-driven CLI
  package with stdlib only (`source_adapters/` precedent).

## 3. Scope

### In Scope

- Additive `ai_tasks` migration: worker ownership + lease + rule baseline.
- `job_match` task type end to end.
- Deterministic `match_rule_v1` scoring in C++ (public result).
- Public API: `POST /api/v1/ai/match`, `GET /api/v1/ai/tasks/{id}`.
- Internal worker API: claim / complete / fail / input, token protected.
- Atomic claim, lease timeout, stale recovery, retry cap, owner enforcement,
  idempotent task creation.
- Degradation: no worker and/or no provider still yields the rule result.
- Python `ai_worker/` package: worker loop, provider-neutral HTTP client
  (OpenAI-compatible chat + embeddings), strict output validation, rule-only
  fallback, fixture tests.
- Documentation: `docs/03` section 8, `docs/04` sections 7/10.3, `docs/adr`
  amendment, `ai_worker/README.md`, `docs/ai/project-state.md`.

### Out of Scope

- `resume_optimize` and `interview_gen` task types (contract stays reserved;
  0014 owns resume tailoring).
- Local `sentence-transformers` / any heavy ML dependency; vector database;
  full RAG.
- RabbitMQ or any queue; notification; frontend; AutoApply; multi-tenant SaaS.
- Streaming responses, cost accounting, quotas, ghost scoring.
- Changing existing public job search / ranking behaviour (`rule_v1` stays).

## 4. Research Findings (file-level evidence)

| Finding | Evidence |
|---|---|
| AI tables exist, unused | `database/schema/05_ai.sql` lines 3-30; dev DB `SHOW TABLES` + `SELECT COUNT(*) FROM ai_tasks` = 0 |
| No AI code / routes | `grep -rn "api/v1/ai|/internal/ai" service backend` → no match |
| No lease/owner columns | `05_ai.sql` lines 3-18 column list |
| Async contract + statuses | `docs/04` section 1.5 (line 57), section 7.3 (line 669) |
| Internal worker contract reserved | `docs/04` section 10.3 (line 801) |
| Rule primitives reusable | `service/include/jobpilot/service/job/job_ranking.h` lines 15-41 |
| Profile/skills read seam | `service/include/jobpilot/service/user/user_dao_interface.h` lines 45-55 |
| Resume content seam | `service/include/jobpilot/service/resume/resume_dao_interface.h` lines 59-79 |
| 0013 quality gate wording | `docs/quality/README.md`: "0013 worker tests must verify atomic claim, lease timeout, retry, stale recovery, provider failure, and rule-only fallback" |
| Migration precedent | `docs/03` section 7 (0012 additive migration + existing-DB ALTER) |

## 5. Contracts

### 5.1 Additive `ai_tasks` migration

```sql
locked_by       VARCHAR(64) NULL COMMENT '当前 worker 标识(0013)'
lease_expire_at DATETIME    NULL COMMENT '租约到期时间(0013)'
rule_result     JSON        NULL COMMENT '确定性规则基线结果(0013)'
KEY idx_status_lease (status, lease_expire_at)
```

- `result` stays the **final** result; `rule_result` is the deterministic
  baseline written at creation and never overwritten by the worker.
- `status` semantics unchanged (`0/1/2/3`); `retry_count` counts attempts.
- Additive only: existing rows stay readable, new columns default NULL, no
  uniqueness or type change. `docs/03` section 3 DDL and section 5 index table
  are updated, section 8 records the existing-database ALTER.

### 5.2 Public API (authenticated)

```text
POST /api/v1/ai/match        { "job_id": 20001, "resume_id": 30001 } -> { "task_id": 1024 }
GET  /api/v1/ai/tasks/{id}   -> { task_id, task_type, status, rule_result, result,
                                  error_msg, created_at, updated_at }
```

Rules:

- Job must exist and be active: otherwise `40001`.
- Resume must belong to the caller and have at least one version: otherwise
  `40002`. The latest version's `content` is the matching input (docs/04 uses
  `resume_id`, so no contract change).
- Idempotency: an existing non-terminal `job_match` task for the same
  `(user_id, job_id, resume_id)` is returned instead of creating a duplicate.
- `GET` returns `50001` for both "not found" and "not owned" so existence does
  not leak.
- `rule_result` is populated from the first moment; `result` is populated when
  the task becomes terminal.

### 5.3 Internal worker API (token protected, same trust boundary as 0012)

```text
POST /internal/ai/tasks/claim            {worker_id, lease_seconds, task_types?}
  -> {task: {id, task_type, job_id, resume_id, retry_count} | null}
POST /internal/ai/tasks/{id}/complete    {worker_id, result}          -> {status}
POST /internal/ai/tasks/{id}/fail        {worker_id, error_msg, retryable} -> {status}
GET  /internal/ai/tasks/{id}/input?worker_id=... -> bundled input (see 5.4)
```

- `X-Internal-Token` is required (constant-time compare, same as
  `/internal/jobs/source-sync`).
- `complete`/`fail`/`input` require `locked_by == worker_id` and a live lease,
  otherwise `50001` (task not found for this worker).
- Errors reuse the `{code,message,data}` envelope; success bodies are compact
  objects like the source-sync endpoint.

### 5.4 Internal input bundle

`GET /internal/ai/tasks/{id}/input` returns everything the worker needs, so it
never reads business tables (ADR-0009, ADR-0006 superseded):

```json
{
  "task": {"id": 1024, "task_type": "job_match", "retry_count": 0},
  "job": {"id": 20001, "company": "...", "title": "...", "location": "...",
          "salary_min": 20, "salary_max": 35, "salary_currency": "CNY",
          "description": "...", "requirements": [{"skill": "C++", "level": 3}]},
  "profile": {"target_position": "...", "location": "...",
              "salary_min": 20, "salary_max": 35, "summary": "..."},
  "skills": [{"name": "C++", "level": 3}],
  "resume": {"resume_id": 30001, "version": 3, "content": "..."},
  "rule_result": { "... match_rule_v1 ..." }
}
```

### 5.5 Result schema

Deterministic baseline (`rule_result`, version `match_rule_v1`):

```json
{
  "score_version": "match_rule_v1",
  "score": 82.5,
  "source": "rule",
  "breakdown": {"skill": 0.91, "title": 0.75, "salary": 1.0, "location": 1.0},
  "matched_skills": ["C++", "Redis"],
  "missing_skills": ["Kubernetes"],
  "strengths": [{"text": "命中 2/3 项岗位技能", "evidence": "skills"}],
  "gaps": [{"text": "缺少 Kubernetes", "evidence": "job_requirements"}],
  "resume_evidence": [{"skill": "Redis", "mentioned": true}]
}
```

- `score` is an integer-valued number in `[0, 100]`, deterministic for the same
  inputs (no clock, no randomness). `first_seen` is intentionally **excluded**:
  recency is a ranking signal, not a match-quality signal.
- `strengths`/`gaps` cite structured evidence (`skills`, `job_requirements`,
  `profile`, `resume_text`). `resume_evidence` is deterministic keyword
  presence in the latest resume version; it never asserts a claimed skill.
- AI enhancement may append `explanation`, `suggestions`, `ai.embedding_score`,
  `ai.llm_score`, and `ai.degraded/reason`. It **must not overwrite
  `score`/`breakdown`/`matched_skills`/`missing_skills`**; the deterministic
  score stays authoritative.

## 6. Selected Design

### 6.1 Layering (the central decision)

```text
C++ (authority)                                    Python ai_worker (optional)
----------------------------------------------     ---------------------------
POST /api/v1/ai/match
  validate (job active, resume owned)
  compute match_rule_v1  <-- reuses ScoreJob()
  store rule_result
  enhancement disabled ? status=2 + result=rule_result
                        : status=0 (pending)
POST /internal/ai/tasks/claim  --(lease)-------->  claim loop
                                                    embedding (optional)
GET  /internal/ai/tasks/{id}/input --------------->  LLM explanation (optional)
POST /internal/ai/tasks/{id}/complete <-----------  validated merge payload
POST /internal/ai/tasks/{id}/fail  <--------------  retryable / permanent
GET  /api/v1/ai/tasks/{id} (rule_result + result)
```

**Deviation to record**: ADR-0009 lists "Executes deterministic scoring" under
the Python worker. 0013 moves the *deterministic* stage into C++ and leaves the
worker only the optional embedding/LLM stages. Reasons: the scoring primitives
already exist and are tested in C++ (`ScoreJob`), re-implementing them in Python
would create two divergent formulas; and the product requirement "core works
without AI" is satisfied much more strongly when a match result exists with no
worker and no key. Mitigation: an explicit amendment section is added to
`docs/adr/0009-ai-pipeline-and-provider-strategy.md` in the documentation step,
and the worker keeps a documented rule-only fallback for its own payloads (it
simply does not recompute the score).

### 6.2 Task lifecycle and concurrency

```text
pending(0)   --claim(worker, lease)-->  processing(1)
processing   --complete-->              success(2)   [result finalized, rule_result kept]
processing   --fail(retryable)-->       pending(0)   if attempts < max_attempts
processing   --fail(permanent|cap)-->   failed(3)    [error_msg set]
processing   --lease expired-->         reclaimable: the next claim requeues and
                                        claims it atomically, attempts += 1;
                                        if the attempt cap is already reached the
                                        row becomes failed(3) instead
pending      --enhancement disabled-->  success(2)   at creation, no worker needed
```

- Claim is one transaction: expire/requeue stale rows beyond the attempt cap
  (`failed(3)`, `error_msg='max attempts exceeded'`), then `SELECT ... FOR
  UPDATE` the oldest eligible row (`status=0`, or `status=1` with an expired
  lease and attempts below the cap) ordered by `id`, then update it to
  `processing` with `locked_by`, `lease_expire_at = NOW() + lease`,
  `retry_count += 1`. Two concurrent claimers therefore never receive the same
  task.
- Defaults: `lease_seconds = 120`, `max_attempts = 3`, both configurable
  (config file + environment override), never hardcoded at call sites.
- The C++ side owns only `ai.enhancement_enabled` (default `false`),
  `ai.lease_seconds`, and `ai.max_attempts`, read through the existing config
  module (`config.yaml` + environment overrides, same as `INTERNAL_API_TOKEN`).
  Provider base_url/model/key stay on the worker side; the C++ service never
  holds an AI key.
- `complete` stores the validated final result and clears the lease;
  `fail` clears the lease and either requeues (retryable, below cap) or
  terminates with `error_msg`.

### 6.3 Degradation policy

| Situation | Behaviour |
|---|---|
| `ai.enhancement_enabled=false` (default) | Task completes at creation with the rule result; no worker required |
| Worker not running, enhancement enabled | Task stays `pending`; `rule_result` is already available to the client |
| Provider not configured / key missing | Worker completes with the rule baseline plus `ai.degraded=true, reason=provider_not_configured` |
| Provider timeout / 429 / 5xx / invalid JSON | Worker calls `fail(retryable=true)` up to the cap, then completes degraded with the rule baseline preserved |
| LLM returns output failing schema validation | Treated as invalid JSON: never persisted, degrade as above |

The rule result is never lost: it is stored at creation and the worker may only
add fields.

### 6.4 Provider abstraction (worker side, BYOK)

- Single provider-neutral client speaking the OpenAI-compatible wire protocol:
  `POST {base_url}/chat/completions` and `POST {base_url}/embeddings`.
- Configuration: `base_url`, `model`, optional `embedding_model`,
  `key_env` (environment variable name, never the value). DeepSeek, Qwen, and
  Ollama/local servers are supported by changing `base_url` + `model`; local
  servers may use an empty key.
- Timeouts and one bounded retry live in the client; no vendor SDK is added.
- Keys are never logged, never written to the task result, and never committed.
- Embeddings are best-effort: unavailable model/endpoint → skip, mark degraded.

### 6.5 Worker CLI

```bash
python -m jobpilot_ai worker --config ai_worker/config.yaml
python -m jobpilot_ai worker --config ai_worker/config.yaml --once
python -m jobpilot_ai worker --config ai_worker/config.yaml --interval 5 --max-tasks 10
```

Sequential processing (one task at a time); empty-queue backoff; `--once` for
tests/CI. No database driver, no SQL, stdlib HTTP (same guard rules as
`source_adapters/`).

## 7. Invariants

- The deterministic score is authoritative; AI output may add but never
  replace `score`, `breakdown`, `matched_skills`, or `missing_skills`.
- A task is claimed by at most one worker at a time; ownership is enforced on
  `input`, `complete`, and `fail`.
- A single worker failure or lease expiry never destroys the rule result.
- `ai_tasks.status` values and meanings are unchanged.
- No business table is written by the Python worker; it only calls
  `/internal/ai/*` with the internal token.
- Provider keys come from the environment only and never appear in logs,
  results, error messages, or git.
- Existing job search, ranking, ingestion, and application behaviour is
  untouched; `rule_v1` is unchanged.

## 8. Testing Plan

C++ (gtest):

- Schema: new columns and `idx_status_lease` exist with expected
  nullability/defaults; existing rows remain readable.
- Rule scorer (pure): deterministic repeat, `[0,100]` bounds, breakdown has no
  `first_seen`, matched/missing skills, missing profile/skills behaviour,
  resume keyword evidence.
- DAO: create + idempotent lookup; atomic claim under two concurrent claimers;
  lease expiry reclaim increments attempts; expired over-cap row becomes
  `failed(3)`; `complete`/`fail`/`input` reject wrong owner and dead lease;
  retryable fail requeues below the cap and terminates at the cap.
- Service: validation (`40001`/`40002`), degradation decision, `rule_result`
  persists on every path, status transitions, task-not-owned → `50001`.
- HTTP: public auth + ownership; internal token missing/wrong; malformed
  bodies; full lifecycle over the internal API (claim → input → complete →
  public poll shows the merged result).
- E2E: create with enhancement disabled → immediate success with
  `match_rule_v1`; act as a worker over HTTP → merged result visible.

Python (`ai_worker/jobpilot_ai/tests`, unittest, fixtures, no network, no DB):

- provider client: chat + embeddings success, timeout, 429, 5xx, invalid JSON;
  key read from env only and never logged.
- result validation: valid payload accepted; invalid/extra payload rejected;
  rule baseline never overwritten; degradation marked.
- worker loop: `--once`; empty queue backoff; retryable vs permanent failure;
  owner/lease identifiers echoed.
- guards: no database-driver import, no SQL statement, dependency allowlist.

## 9. Implementation Steps

1. **Schema migration** — additive columns + index, `docs/03` sections 3/5/8,
   schema tests. Dependency: none.
2. **Match rule domain** — `match_rule_v1` over `ScoreJob`, result DTO,
   pure unit tests. Dependency: step 1.
3. **AI Task DAO** — `IAiTaskDao` + `AiTaskDao` (create/find/claim/complete/
   fail/input-bundle queries), integration tests. Dependency: steps 1-2.
4. **AI Task Service** — validation, degradation decision, transitions,
   idempotency, counters; fake-DAO service tests. Dependency: step 3.
5. **Public + internal HTTP** — controllers, router wiring, main wiring,
   HTTP tests. Dependency: step 4.
6. **Python worker** — config, internal client, provider client, merge
   validation, CLI, fixture tests. Dependency: step 5.
7. **E2E + documentation** — end-to-end test; `docs/04` sections 7/10.3;
   ADR-0009 amendment; `ai_worker/README.md`; `docs/ai/project-state.md`.
   Dependency: steps 1-6.
8. **L4 verification** — full Debug, ASan, UBSan CTest; service and root
   Release builds; Python worker suite; `git diff --check`. TSan only if new
   concurrency is introduced (none planned; the claim path is request-scoped).

## 10. Verification and Rollback

- Each step carries its own focused tests; the stage closes with L4.
- Rollback boundary: additive columns may stay; removing the routes/controllers
  and the worker package restores prior behaviour. `ai.enhancement_enabled`
  defaults to false, so a fresh install behaves exactly like today until the
  worker is configured.
- Nothing in this plan changes `docs/03` existing columns, `docs/04` existing
  public shapes, or `rule_v1`.

## 11. Risks

| Priority | Risk | Mitigation |
|---|---|---|
| High | Double-claiming a task | Single-transaction claim with row lock + tests with two claimers |
| High | AI output silently replacing the deterministic score | Merge whitelist + validation tests |
| High | Lease leak (worker dies mid-task) | Lease expiry reclaim + attempt cap + stale tests |
| Medium | Provider drift (payload/behaviour) | OpenAI-compatible wire shape only + fixture tests + degradation |
| Medium | Secret leakage | Key from env only; explicit "never logged" tests |
| Medium | Contract drift with `docs/04` | Endpoint shapes taken from sections 7/10.3 and asserted in HTTP tests |
| Medium | Resume with no text content | Validation error, no fabricated evidence |
| Low | `ai_tasks` grows unbounded | Documented follow-up (retention policy), not a 0013 requirement |

## 12. Stop Conditions

Stop and return to design if:

- claim/lease cannot be made atomic with the additive columns;
- the worker would need direct business-table access;
- the score must become non-deterministic to be useful;
- a queue, vector database, or heavy local ML dependency becomes required;
- resume tailoring or interview generation needs to be pulled into 0013;
- a schema change beyond the additive columns becomes necessary.

## 13. Current Progress

- [x] Research
- [x] Capability Check
- [x] Design
- [x] ExecPlan
- [x] Schema migration
- [x] Match rule domain
- [ ] AI Task DAO
- [ ] AI Task Service
- [ ] Public + internal HTTP
- [ ] Python worker
- [ ] E2E + documentation
- [ ] L4 verification

## 14. Known Issues and Follow-up Work

- `interview_gen` / `resume_optimize` remain contract-only (0014).
- Local `sentence-transformers` embeddings from ADR-0005 are deferred; remote
  OpenAI-compatible embeddings are the 0013 path.
- Task retention/cleanup policy is not part of 0013.
- Score blending (LLM contributing to the authoritative score) is deliberately
  not implemented; if it is ever wanted it needs its own ADR.

## 15. Rejected Alternatives

- **Rule scoring inside the Python worker** (ADR-0009 literal reading):
  duplicates a tested C++ formula in a second language and makes basic matching
  depend on a running worker.
- **LLM blends into the authoritative score**: loses reproducibility, ordering
  stability, and explainability.
- **LLM-only matching**: violates deterministic-first; unusable without a key.
- **Local embedding model as a hard dependency**: heavy install for a
  self-hosted C++ showcase; deferred.
- **Explicit stale-sweep endpoint**: implicit reclaim inside `claim` is
  simpler and equally testable.
- **Separate lease table**: extra joins and two-write transactions for no
  benefit at this scale.
- **Long-poll claim**: would require blocking/sleeping inside the P1-frozen
  reactor; the worker backs off instead.
- **Per-provider SDKs**: vendor lock-in against ADR-0009.
- **RabbitMQ**: ADR-0009 defers it; a single-user local worker does not need it.
- **Storing results in a new table**: `ai_tasks.result` JSON was explicitly
  chosen in `docs/03` to avoid schema churn per feature.

## 16. Implementation Decisions

### Step 1 — schema migration (committed `127acfd`)

- `locked_by`, `lease_expire_at`, `rule_result` and `idx_status_lease` are
  additive; `docs/03` section 3 DDL, section 5 index table, and new section 8
  record the contract and the existing-database ALTER.
- Evidence: `SchemaTest` 9/9 against the migrated dev database and 9/9 against a
  freshly initialized `jobpilot_0013_check` database (init scripts and the ALTER
  produce the same contract).

### Step 2 — match_rule_v1 domain

- `ScoreMatch` reuses `ScoreJob` and renormalizes over the present components
  after dropping the ranking-only recency component; the `rule_v1` weights are
  now exported from `job_ranking.h` so there is a single source of truth.
- **Defect found and fixed (0011 scope)**: `SkillFit` could exceed `1.0` for
  over-qualified skills (`min(userLevel, requiredLevel) * requiredLevel`
  summed over requirements), which let `rule_v1` return scores above the
  documented 0-100 range. It is now clamped at `1.0` and covered by a
  regression assertion in `MatchRuleTest.IgnoresRecencyUnlikeRuleV1`.
- `ScoreMatch` additionally clamps every component to `[0, 1]` and the final
  score to `[0, 100]` so the 0013 contract cannot be violated by upstream
  changes.
- Resume matching is word-boundary aware (`Redis` does not match
  `RedisCluster`), case-insensitive, and treated as evidence rather than proof.
- `score` is integer-valued in `[0, 100]`; `breakdown` keeps full precision.
- Evidence: `MatchRuleTest` 6/6; `test_job_ranking_service` 3/3 and
  `test_job_service` 7/7 unchanged; `-Wall -Wextra -Werror` clean for
  `src/ai/match_rule.cpp`, `src/job/job_ranking.cpp`, `tests/test_match_rule.cpp`.
