# ADR-0008: Source adapters instead of a general crawler

## Status

Accepted

## Context

Comparable projects discover jobs through a combination of public ATS APIs,
RSS/JSON feeds, search-engine discovery, HTML pages, and browser automation.
Maintaining a general HTML crawler immediately creates compliance, anti-bot,
fragility, and operational cost problems.

JobPilotAI already has a trusted Job Ingestion API based on `source_url`
idempotency and transactional requirements replacement.

## Decision

Use the following pipeline:

```text
Source Adapter
  -> Normalize
  -> Deduplicate
  -> POST /internal/jobs/source-sync
  -> MySQL
```

`POST /internal/jobs/import` remains the manual / dataset / test-data path.
Only the source-observation endpoint carries `complete`, `source_scope`, and
`observed_at` and may therefore drive the freshness lifecycle.

MVP source adapters:

1. Greenhouse.
2. Lever.
3. Ashby.
4. Minimal JSON/CSV or dataset import.

Deferred:

- RSS and additional feeds.
- Search-engine discovery.
- Workday and long-tail HTML providers.
- Playwright/browser fallback.

Source adapters may be implemented in Python for integration speed, but they
must use the C++ internal ingestion APIs (`/internal/jobs/source-sync` for
adapter observations, `/internal/jobs/import` for manual/dataset batches) and
must not write MySQL directly.

## Amendment (0012, 2026-10-08)

The original decision routed adapters through `POST /internal/jobs/import`.
ExecPlan 0012 split adapter observation from manual import, because a source
observation must express whether the whole board was observed (`complete`,
`source_scope`, `observed_at`), while a manual import has no such semantics.

- Adapter observation: `POST /internal/jobs/source-sync`; `0..200` jobs when
  `complete=true` (`0` is an empty board), `1..200` when `complete=false`.
- Manual / dataset import: `POST /internal/jobs/import`, `1..100` jobs.

The rest of this ADR is unchanged. The endpoint contract is tracked in
`docs/04-api-design.md` §5.5 and ExecPlan 0012.

## Consequences

### Positive

- Public structured APIs are more stable than HTML scraping.
- JobPilotAI avoids duplicating an entire crawler ecosystem.
- All writes flow through the tested internal ingestion contracts.
- New sources can be added without changing search or application logic.

### Negative

- Coverage depends on curated source/company lists.
- Browser-only providers are not available in the MVP.
- A small adapter runner and its configuration surface must be maintained.

## Rejected Alternatives

- **General browser crawler as MVP**: too much compliance and fragility for
  the project goal.
- **Direct database writes from crawlers/adapters**: bypasses ingestion
  validation, transactions, and idempotency.
- **Scraping LinkedIn/Indeed/Glassdoor with proxy rotation**: outside the
  project's ethical and legal boundary.
