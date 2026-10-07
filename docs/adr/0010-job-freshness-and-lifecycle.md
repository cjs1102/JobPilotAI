# ADR-0010: Job freshness and lifecycle

## Status

Accepted

## Context

Job postings disappear, reappear, stop accepting applications, and remain
published long after active hiring has stopped. A naive implementation that
closes a job whenever a fetch does not return it will hide live jobs and
damage search quality.

The project already has:

- `jobs.source_url` as the MVP idempotency key.
- `jobs.status` with `1 active` and `0 inactive`.
- `jobs.create_time` and `jobs.update_time`.
- A future Source Adapter pipeline that will call the internal Job Ingestion
  API.

The project does not yet have a complete source-observation model.

## Decision Drivers

- A false `CLOSED` verdict hides a live job and is expensive to notice.
- An uncertain job costs only a later re-check.
- ATS APIs provide different amounts of proof about complete board coverage.
- Search should distinguish posting age from verification freshness.
- Ghost jobs are a risk signal, not proof that a job is closed.

## Decision

Adopt a soft lifecycle with explicit evidence:

```text
ACTIVE
  -> aging / stale (heuristic)
  -> CLOSED (only on positive evidence or proven complete absence)
```

A single failed or incomplete fetch must never close a Job.

## 1. Lifecycle Fields

| Field | Meaning | Classification |
|---|---|---|
| `published_at` | Source-provided publication time | Fact when present |
| `first_seen_at` | First local observation | Local fact |
| `last_seen_at` | Last complete observation containing the Job | Source-observation fact |
| `last_verified_at` | Last liveness check result | Fact about the check |
| `closed_at` | Soft-close timestamp | Fact or high-confidence heuristic |
| `source_status` | Result of the latest source run | Source-level fact |

The current schema has no dedicated versions of these fields.

Current mappings/proxies:

- `create_time` may be treated as `first_seen_at`.
- `update_time` is NOT `last_seen_at`; it changes only when stored metadata
  changes.
- `status = 0` remains the current inactive/soft-close representation.

## 2. Source Observation

Future observation model:

```text
Job
  + JobSource / JobObservation
```

Recommended observation attributes:

```text
source
source_url
external_id
published_at
first_seen_at
last_seen_at
last_verified_at
source_status
complete
observation_time
```

`complete = true` means the adapter can prove it observed the full set of
postings for the relevant board or source scope.

Examples:

- Greenhouse: compare returned rows with the API total when available.
- Lever: adapter proves it reached the complete board response.
- Ashby: adapter proves it reached the complete board response.
- Search discovery and browser fallback: generally cannot prove completeness.

## 3. Disappearance Rules

| Observation | Action |
|---|---|
| Complete success and Job present | Refresh `last_seen_at` |
| Complete success and Job absent | First absence is a warning, not closure |
| Repeated complete absences | Close after the source-specific grace period |
| Request failure | No lifecycle change |
| Rate limit | No lifecycle change |
| Network timeout | No lifecycle change |
| Partial pagination | No lifecycle change |
| Explicit removed/closed event | Close immediately |

Recommended ATS rule:

```text
complete observation
  + Job absent
  + two consecutive misses or 48h since last_seen_at
  -> CLOSED
```

The exact threshold must be configurable per adapter because provider contracts
differ.

## 4. Freshness Policy

Suggested default policy:

| State | ATS | Feed/RSS | Discovery/Browser |
|---|---|---|---|
| Fresh | <= 24h | <= 48h | <= 7d |
| Aging | 1-7d | 2-14d | 7-30d |
| Stale | > 7d | > 14d | > 30d |
| Closed | Positive evidence | Feed removal or complete misses | Positive evidence only |

Thresholds are source-specific policy, not universal truth.

## 5. Posted Age vs Freshness Age

- **Posted age** is `now - published_at`.
- **Freshness age** is `now - last_seen_at` or
  `now - last_verified_at`.

Search should expose both. A job can be posted long ago but still be actively
verified, or posted recently but no longer present at the source.

## 6. Ghost Jobs

```text
still published != actively hiring
```

Ghost-job analysis produces only:

```text
ghost_score
stale_score
risk_reasons
```

It must not set `closed_at`.

AI may contribute to these scores, but cannot turn a heuristic directly into a
lifecycle fact.

## 7. Search Behavior

Current MVP:

- Search only `jobs.status = 1`.
- No automatic freshness closure.
- `create_time` can provide first-seen ordering.

Phase 2:

- Default search returns `ACTIVE` and excludes `STALE/CLOSED`.
- `include_stale=true` can explicitly include stale jobs.
- Responses expose `posted_at`, `first_seen_at`, `last_seen_at`,
  `last_verified_at`, `freshness`, and `source_status`.
- `posted_within_days=N` filters by source publication age.

## 8. Evolution Boundary

### MVP

- Keep current `jobs` schema.
- Keep `source_url UNIQUE`.
- Do not infer closure from one missing fetch.
- Do not add freshness columns yet.

### Phase 2

- Add additive freshness/observation fields or a `job_sources` table.
- Introduce complete-observation signals in Source Adapters.
- Introduce stale/closed lifecycle and search filtering.
- Keep the existing `jobs` row addressable by public Job ID.

### Future

- Split `Canonical Job` from `JobSource / Observation`.
- Add alias and merge/split rules after real multi-source duplicates exist.
- Add per-source liveness probes and richer ghost-job evidence.

## Consequences

### Positive

- Search quality improves without risking mass false closures.
- Source failures, rate limits, and partial pagination remain safe.
- Posting age and verification freshness become explainable.
- Ghost-job analysis remains separate from lifecycle truth.

### Negative

- Some stale jobs remain visible longer.
- Complete observation must be implemented per adapter.
- Freshness requires a later schema migration.

## Rejected Alternatives

- **Close on first missing fetch**: can hide live jobs after transient failure.
- **Use `update_time` as `last_seen_at`**: it is a metadata-change timestamp.
- **Let AI set `closed_at`**: heuristic output must not become lifecycle fact.
- **Treat ghost score as closed**: not actively hiring is not the same as not
  published.
- **Introduce canonical multi-source identity immediately**: no real
  duplicate dataset yet.
