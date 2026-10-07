# ExecPlan 0012: Job Source Adapter + Freshness

> Status: Research, Design, and Planning complete. Implementation not started.
>
> Task level: L4.

## 1. Objective

Add real job discovery and trustworthy freshness lifecycle semantics:

```text
Greenhouse / Lever / Ashby
  -> Python Source Adapter
  -> Normalize
  -> C++ Source Sync API
  -> jobs + source observation fields
  -> fresh / aging / stale / closed search semantics
```

The slice must introduce real ATS jobs without giving adapters direct database
access and without closing live jobs after a single failed or incomplete fetch.

## 2. Background

Current capabilities:

- 0010 provides transactional Job Ingestion.
- 0011 provides deterministic MySQL Search / Ranking.
- `jobs.source_url` is the current idempotency key.
- Current `jobs.status` uses `1 active` / `0 inactive`.
- `create_time` is effectively first-seen; `update_time` is not last-seen.
- No ATS source adapter exists.
- No freshness lifecycle is persisted.

Accepted constraints:

- ADR-0008: Source Adapter -> Normalize/Deduplicate -> Job Ingestion.
- ADR-0010: missing from one fetch is not closed.
- The 0012 contract split (dedicated observation endpoint) amends ADR-0008 and
  is recorded in `docs/04` §5.5.
- Canonical Job / multi-source alias is deferred.
- RabbitMQ, browser scraping, and AutoApply are out of scope.

## 3. Scope

### In Scope

- Manual Greenhouse, Lever, and Ashby board configuration.
- Python source-adapter runner.
- Public ATS API fetching and normalization.
- No adapter access to MySQL.
- Split adapter observation (`POST /internal/jobs/source-sync`) from
  manual/dataset import (`POST /internal/jobs/import`).
- C++ Source Sync API using the existing internal token boundary.
- Additive freshness/observation columns on `jobs`.
- Complete/incomplete observation semantics.
- `last_seen_at`, `last_verified_at`, `closed_at`, and `source_status`.
- Disappearance grace period and reopen behavior.
- Fresh / aging / stale classification.
- Search filters for stale and posted age.
- Fixture-based adapter tests.
- DAO, Service, HTTP, migration, and integration tests.
- Documentation and project-state updates.

### Out of Scope

- Canonical Job / JobSource / multi-source merging.
- Search engine or vector database.
- Browser/Playwright fallback.
- Workday or other long-tail HTML sources.
- Automatic board discovery.
- Crawler scheduling infrastructure.
- AI or ghost-score generation.
- Notification.
- RabbitMQ.
- AutoApply.
- Frontend.

## 4. Source Adapter Architecture

```text
source_adapters/
  pyproject.toml or requirements.txt
  config.example.yaml
  jobpilot_sources/
    cli.py
    config.py
    client.py
    normalizer.py
    providers/
      greenhouse.py
      lever.py
      ashby.py
    tests/
```

The adapter runner is external to the C++ service:

```text
python -m jobpilot_sources sync --config source_adapters/config.yaml
python -m jobpilot_sources sync --config ... --provider greenhouse
```

It uses Python standard-library HTTP/JSON where practical. No web framework or
database driver is introduced.

## 5. Manual Source Configuration

MVP configuration is explicit and local:

```yaml
jobpilot:
  base_url: http://127.0.0.1:8080
  internal_token_env: INTERNAL_API_TOKEN

sources:
  - type: greenhouse
    board: example-company
    enabled: true
  - type: lever
    board: example-company
    enabled: true
  - type: ashby
    board: example-company
    enabled: true
```

Rules:

- No source/board auto-discovery.
- Duplicate `(type, board)` entries are rejected.
- A missing token fails before network calls.
- Secrets never appear in config or logs.

## 6. Provider Adapters

### Greenhouse

- Use the public board posting API.
- Request description content when needed.
- Use the provider total metadata when available to prove completeness.

### Lever

- Use the public postings JSON endpoint.
- The provider response is the complete current board by contract.

### Ashby

- Use the public posting API.
- The provider response is the complete current board by contract.

### Normalization

All providers produce:

```text
source
source_scope
source_url
external_id
published_at
company
title
location
salary_min / salary_max / currency
description
requirements
```

MVP rule:

- Do not infer structured skills from description text.
- `requirements` is empty unless the provider supplies structured skill data.
- Never fabricate missing salary, location, or publication time.

## 7. Source Sync API

Use a dedicated source-observation endpoint rather than overloading manual
JSON import. `/internal/jobs/import` keeps its manual / dataset / test-data
semantics and its `1..100` batch limit; only `source-sync` carries `complete`,
`source_scope`, and `observed_at`, and only `source-sync` may drive the
freshness lifecycle (`last_seen_at`, missing counters, close, reopen).

```text
POST /internal/jobs/source-sync
X-Internal-Token: <secret>
```

```json
{
  "source": "greenhouse",
  "source_scope": "example-company",
  "complete": true,
  "observed_at": "2026-10-08 12:00:00",
  "jobs": []
}
```

MVP limits:

```text
complete=true  -> 0 <= jobs.size() <= 200  (0 = empty board)
complete=false -> 1 <= jobs.size() <= 200
manual /internal/jobs/import keeps 1 <= jobs.size() <= 100
```

If a board exceeds the MVP batch limit, the adapter sends `complete=false`;
it may still upsert the observed jobs, but the server must not close missing
jobs.

Response:

```json
{
  "created": 10,
  "updated": 2,
  "seen": 12,
  "closed": 0,
  "complete": true
}
```

## 8. Additive Schema Migration

The current `jobs` table remains the canonical row. Canonical Job / JobSource
split remains future work.

Add:

```sql
source_scope      VARCHAR(128) NULL
external_id       VARCHAR(255) NULL
published_at      DATETIME NULL
last_seen_at      DATETIME NULL
last_verified_at  DATETIME NULL
closed_at         DATETIME NULL
missing_count     INT NOT NULL DEFAULT 0
source_status     VARCHAR(32) NOT NULL DEFAULT 'unknown'
```

Suggested index:

```sql
KEY idx_source_scope_status (source, source_scope, status)
```

Compatibility:

- Existing manual/imported jobs remain compatible.
- `create_time` continues to represent first-seen.
- Existing `status=0` maps to inactive.
- Existing rows default to `source_status='unknown'`.

This migration is additive and does not change `source_url UNIQUE`.

## 9. Observation and Disappearance Semantics

### Complete Success, Job Present

```text
last_seen_at = observed_at
last_verified_at = observed_at
missing_count = 0
closed_at = NULL
source_status = ok
status = 1
```

### Complete Success, Job Absent

```text
missing_count += 1
source_status = missing
```

Close only when:

```text
missing_count >= 2
OR observation age exceeds source grace period
```

ATS default grace:

```text
48 hours
```

On close:

```text
closed_at = observed_at
status = 0
source_status = closed
```

### Incomplete Observation

```text
partial pagination / rate limit / timeout / schema uncertainty
```

Behavior:

- Upsert jobs actually observed.
- Update their `last_seen_at`.
- Do not increment missing counters.
- Do not close any job.

### Job Reappears

```text
closed_at = NULL
missing_count = 0
last_seen_at = observed_at
source_status = ok
status = 1
```

## 10. Freshness Classification

Computed, not manually stored:

```text
fresh: now - last_seen_at <= 24h
aging: 24h < age <= 7d
stale: age > 7d
closed: closed_at IS NOT NULL
```

Thresholds may be configured per source. Initial MVP uses the defaults above.

Posted age and freshness age remain distinct:

```text
posted_age = now - published_at
freshness_age = now - last_seen_at / last_verified_at
```

## 11. Search Contract Changes

Default public search:

- `status = 1`
- `closed_at IS NULL`
- exclude stale

New optional parameters:

```text
include_stale=true
posted_within_days=N
```

New response fields:

```text
posted_at
first_seen_at
last_seen_at
last_verified_at
freshness
source_status
```

`posted_within_days` excludes jobs with no reliable `published_at`.

The existing `rule_v1` ranking formula remains unchanged in 0012. Freshness is
a filter/visibility signal, not a new ranking weight.

## 12. DAO / Service Design

### DAO

Add:

```cpp
SourceSyncDao::ApplyObservation(
    Transaction &transaction,
    const SourceObservation &observation);

SourceSyncDao::CloseUnseenForScope(
    Transaction &transaction,
    const SourceObservation &observation);
```

### Service

Add `SourceSyncService`:

- validate source/scope/complete/observed_at/jobs;
- reuse the existing normalized Job DTO and validation;
- open one transaction;
- upsert each observed Job and requirements;
- update freshness fields;
- close missing Jobs only for complete observations;
- return created/updated/seen/closed counters.

The existing `JobIngestionService` remains the manual/dataset ingestion path.

## 13. Internal Controller / Router

Add:

```text
POST /internal/jobs/source-sync
```

Rules:

- Require the same `INTERNAL_API_TOKEN`.
- Reuse the existing error envelope and internal trust boundary.
- Reject incomplete source metadata that attempts to close jobs.
- Keep response compact and source-run oriented.

## 14. Testing Plan

### Schema

- New columns exist with expected nullability/defaults.
- Existing rows remain readable.
- `source_url` uniqueness is unchanged.

### Source Sync DAO

- First observation creates Jobs and sets freshness fields.
- Complete observation with absence increments missing count.
- Two complete misses close.
- Incomplete observation never closes.
- Reappearance reopens and resets counters.
- Scope isolation prevents closing another board.

### Source Sync Service

- Atomic batch behavior.
- Complete/incomplete validation.
- Counters.
- Empty complete board behavior.
- JobId/source identity preservation.

### HTTP

- Missing/wrong internal token.
- Malformed observation.
- Complete sync.
- Incomplete sync.
- Disappearance grace and reopen.

### Python Adapters

- Fixture parsing for Greenhouse/Lever/Ashby.
- Completeness classification.
- Normalization.
- Duplicate source URLs.
- Batches above MVP limit become incomplete.
- No direct database imports.
- Token never logged.

### Search

- Closed Jobs excluded.
- Stale Jobs excluded by default.
- `include_stale=true`.
- `posted_within_days`.
- Response freshness fields.

## 15. Implementation Steps

### Step 1: Schema Migration

- Goal: add additive freshness/observation fields and indexes.
- Dependency: ADR-0010.
- Verification: schema tests and clean database initialization.
- Risk: migration compatibility.

### Step 2: Source Observation Domain

- Goal: define source sync DTOs, status, complete semantics, and validation.
- Dependency: Step 1.
- Verification: pure unit tests.
- Risk: ambiguous completeness rules.

### Step 3: Source Sync DAO

- Goal: transactional upsert, freshness updates, missing counters, closure.
- Dependency: Steps 1-2.
- Verification: DAO integration tests.
- Risk: accidental mass closure.

### Step 4: Source Sync Service

- Goal: one transaction per observation and safe counters.
- Dependency: Step 3.
- Verification: fake DAO Service tests.
- Risk: partial observation affecting lifecycle.

### Step 5: Internal Source Sync API

- Goal: expose `/internal/jobs/source-sync`.
- Dependency: Step 4.
- Verification: HTTP tests.
- Risk: auth/body validation parity.

### Step 6: Search Freshness Integration

- Goal: filter stale/closed and expose freshness fields.
- Dependency: Step 1.
- Verification: DAO/Service/HTTP tests.
- Risk: regressions in public search.

### Step 7: Python Source Adapters

- Goal: Greenhouse/Lever/Ashby fixture-driven fetch and normalize.
- Dependency: Steps 1-6.
- Verification: adapter tests and optional live smoke.
- Risk: provider payload changes.

### Step 8: End-to-End Integration

- Goal: adapter sync -> ingestion -> search visibility -> disappearance -> reopen.
- Dependency: Steps 1-7.
- Verification: controlled local integration test.
- Risk: test dependence on live ATS APIs.

### Step 9: Documentation

- Goal: document source config, freshness behavior, and API fields.
- Scope: `docs/04`, project-state, quality, source adapter README.
- Dependency: Steps 1-8.
- Risk: documenting unobserved behavior.

### Step 10: L4 Verification

- Debug full CTest.
- ASan full CTest.
- UBSan full CTest.
- TSan only if source-sync worker introduces concurrency.
- service/root Release builds.
- Python adapter test suite.
- `git diff --check`.

## 16. Risks

| Priority | Risk | Mitigation |
|---|---|---|
| High | Incomplete fetch mass-closes jobs | Complete proof is mandatory |
| High | Provider reports success with truncated data | Provider-specific completeness checks |
| High | Source schema changes | Fixture tests and complete=false fallback |
| Medium | Source URL changes on repost | Treat as new Job in 0012; alias follows later |
| Medium | Board larger than MVP batch limit | Mark incomplete; do not close |
| Medium | Time-zone drift | Store normalized UTC/DB timestamps consistently |
| Medium | Search regression from stale filtering | Focused regression tests |
| Low | Manual/import Jobs lack freshness | Keep `unknown` and fallback to first-seen |

## 17. Stop Conditions

Stop and return to design if:

- complete board coverage cannot be proven for a provider;
- Provider APIs require authentication;
- multi-source canonical identity becomes necessary immediately;
- source adapter requires direct database access;
- freshness closure requires AI;
- browser scraping becomes the only viable source;
- a schema migration beyond the additive fields is required.

## 18. Current Progress

- [x] Research
- [x] Capability Check
- [x] Design
- [x] ExecPlan
- [ ] Schema migration
- [ ] Source observation domain
- [ ] Source Sync DAO
- [ ] Source Sync Service
- [ ] Internal Source Sync API
- [ ] Search freshness integration
- [ ] Python source adapters
- [ ] End-to-end integration
- [ ] Documentation
- [ ] L4 verification

## 19. Rejected Alternatives

- **Generic crawler/browser fallback**: compliance and maintenance cost.
- **Adapter direct MySQL writes**: bypasses validation and transaction rules.
- **Close after one missing fetch**: unsafe.
- **Use `update_time` as `last_seen_at`**: semantically incorrect.
- **Canonical multi-source identity now**: no real duplicate dataset yet.
- **AI as freshness authority**: heuristics cannot become lifecycle facts.
- **RabbitMQ for adapter orchestration**: local cron/CLI is sufficient.
