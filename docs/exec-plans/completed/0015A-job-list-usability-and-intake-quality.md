# ExecPlan 0015A: Job List Usability + Pre-Ingest Data Quality

> Status: **Completed and archived** (2026-10-10). The stage's single L4 passed:
> Debug / ASan / UBSan **286/286 each** (sanitizer diagnostics 0); both `service/`
> and root Release builds; Python `source_adapters` **446** and `ai_worker`
> **190**; review-backfill isolation with the dev database explicitly SKIPped and
> the scratch database **7/7**. The plan is frozen here as historical evidence.
>
> **The §13 step boxes below were not maintained while the stage ran**, so no box
> may be read as a per-step acceptance record; the closure evidence is the recorded
> L4 gate (`docs/ai/project-state.md` `## Completed Plan (0015A)`). In particular
> Step 5's real-database backfill was **never applied** — apply/revert are proven
> only against the scratch database.
>
> **Stage number: `0015A` — CONFIRMED by the owner (2026-10-10).** The project stage table
> keeps **0015 = resume tailoring** and **0016 = open-source packaging**; this stage is a
> *0014 follow-up hardening* and therefore uses the `0015A` sub-number. No renumbering, no
> archived-plan numbering changes, no wide cross-reference edits. Rationale in §16.
>
> Historical planning-phase status, preserved as written: "Status: Active — **design
> finalized and all five §15 decisions closed by the owner (2026-10-10)**; no production
> code has been written."
>
> Task level: **L4 at stage close** (touches the `jobs` schema, `docs/03`, the public
> `docs/04` contract and ingestion validation across modules); implementation slices are
> L1/L2.
>
> Verification rules: `docs/ai/agent-workflow.md` (§2 levels, §2.2 L4 preconditions, §8
> execution budget and stop conditions). Testing levels: `docs/quality/README.md`.
>
> Dependencies: ExecPlan 0014 (completed; archived at
> `completed/0014-china-campus-sources-and-manual-capture.md`) — its F13/F14 contracts and
> six additive `jobs` columns are **used, not redefined**, by this plan. No dependency on
> 0014 Step 5 (China real sources), which remains environment-limited.
>
> Evidence base: the task-17 read-only audit (2026-10-10) and the 0014 §19.6 real-browser
> findings (【用户报告】), plus the five owner decisions recorded in §15.

## 1. Objective

Make the **existing** job-collection + backend data path trustworthy and directly usable,
**without new services and without an architecture refactor**, in exactly three outcomes:

1. **List rows can reach the original posting**: `GET /api/v1/jobs` and
   `GET /api/v1/jobs/recommended` return `source_url` per item, so a caller no longer needs
   one detail request per row to open the original job.
2. **Only anomalies reach a human, and only `clean` jobs are public**: deterministic
   pre-ingest checks split every record into `reject` (cannot be safely normalised → HTTP
   `400` with a diagnostic), `needs_review` (fixable defect → stored, hidden, queued with a
   **stable reason code + human-readable reason**), or `clean` (visible). Weak/ambiguous
   signals never hide a job (§6.1-§6.3).
3. **The 0014 fields become queryable**: `education_required`, `job_nature` and `deadline`
   enter search filtering and sorting under documented semantics (F13 stays
   display/urgency-only), with DAO parameters, `docs/04` **and** the `docs/03` index note
   updated **in the same change**.

This stage delivers **backend contracts + backend data quality only**. After it, the job list
is still JSON consumed by tests/operators, not by an end user (§3.2, §17).

## 2. Background and Current State (file-level evidence from the real tree)

Verified at HEAD `866037c` (branch `main`).

**Routes and layers** — 30 routes in `service/src/router.cpp:39-406`; the touched ones:

```text
GET  /api/v1/jobs              router.cpp:209  -> JobController::Search       job_controller.cpp:655
GET  /api/v1/jobs/{id}         router.cpp:233  -> JobController::Detail       job_controller.cpp:709
GET  /api/v1/jobs/recommended  router.cpp:221  -> JobController::Recommended  job_controller.cpp:771
POST /internal/jobs/import     router.cpp:299  -> InternalJobImportController
POST /internal/jobs/source-sync router.cpp:314 -> InternalSourceSyncController
```

**What is wired end-to-end vs not** (so this plan is never read as more than it is):

| Capability | Actual state |
|---|---|
| Ingestion (`/internal/jobs/import`, `/source-sync`) → MySQL → public search/detail APIs | Wired and exercised end-to-end at the API level (0010/0012/0014 evidence) |
| Manual capture channel (bookmarklet → local publisher → import API) | Wired; the real-browser **extraction** step was run once (0014 §19.6, 【用户报告】); publish/read-back were **not** executed in that run |
| `GET /api/v1/jobs/recommended` (`rule_v1` ranking, authenticated) | **Backend-only**: ranks inside the service and returns JSON; **there is no UI/consumer, so it is not an end-to-end user feature** |
| `match_rule_v1` scoring (`/api/v1/ai/match` → `ai_tasks.rule_result` → `/api/v1/ai/tasks/{id}`) | **Backend-only async task + polling endpoint**; it participates in **no** list, sort or display path |
| Resume upload (.pdf/.txt/.md) | Upload + version listing wired; **PDF stores no text** (`resume_service.cpp:148-149` → `content = NULL`), so a PDF-only resume contributes nothing to matching |
| Application states (0..7) | Row/state machine wired; **no** interview records, reminders, tailored resume or cover letter (the `interview_records`/`notifications` tables have **no code path**) |
| Any frontend / clickable job list / admin console | **Does not exist** (the only `.html` in the tree is a test fixture) |

**Job read model** — `JobSummary` (`job_dao_interface.h:15-45`) has id, company, title,
location, salary min/max/currency, source, create/update time, `published_at`,
`last_seen_at`, `last_verified_at`, `source_status`, derived `freshness`, and the six 0014
fields — but **no `sourceUrl`**. `JobDetail` (`:53-60`) adds `description`, `requirements`,
`sourceUrl`, `status`. `JobController::Detail` writes `description`/`source_url`
(`job_controller.cpp:741`, `:744`); `Search`/`Recommended` never emit `source_url`.

**DAO** — `JobDao::SearchJobs` (`job_dao.cpp:381`) selects `:398-407` (no `j.source_url`)
with `ORDER BY j.create_time DESC, j.id DESC` (`:409`); `SearchRankingCandidates` (`:451`)
selects `:465-472` with `ORDER BY` at `:476`; `FindActiveJobById` (`:603`) is the only
projection with `source_url`. Predicate building: status/closed `:154`, freshness
`:162-174`, location `:183`, keyword LIKE `:197-200`, skill `:212`, salary `:221-232`;
`CountJobs` at `:427` must mirror any new predicate.

**Search contract** — `JobSearchFilter` (`job_search_filter.h:9-23`) = keyword, location,
salaryMin/Max, skill, includeStale, postedWithinDays (**no education/job_nature/deadline**);
parsing in `JobController::ParseSearchQuery` (`job_controller.cpp:248`), validation in
`NormalizeJobSearchFilter` (`job_search_filter.cpp`); documented in `docs/04:281` (§5.1).

**0014 columns already exist** (`database/schema/03_job.sql`; `docs/03` §9.2 `:433`):
`education_required`, `experience_years_min/max`, `job_nature`, `deadline`,
`deadline_date_only` (F13 authority §9.1 `:384`). `docs/03:240-242` records the deliberate
0014 decision that these columns **added no index** and **do not enter search/ranking** —
this stage reverses that, so that note and `docs/04` §5.1 must change **in the same change**
(§5.6).

**Existing `jobs` indexes today** (`database/schema/03_job.sql`): `PRIMARY`, `uk_source_url`,
`idx_company`, `idx_title`, `idx_location`, `idx_status`,
`idx_source_scope_status (source, source_scope, status)`. There is **no** index on
`education_required` / `job_nature` / `deadline`. Any new index must be justified by a real
query plan (§5.3, decision 3).

**Description storage** — `jobs.description` is MySQL `TEXT`, i.e. up to **65,535 bytes**
(not characters); the application must bound it well below that (§5.5.3, decision 5).

**Config shapes (input to decision 4)** — the C++ service reads
`service/config/config.yaml` with sections `server`, `mysql`, `redis`, `log`, `storage`, `ai`
(yaml-cpp is already a dependency); the Python side reads
`source_adapters/config.example.yaml` (`jobpilot:` + a `sources:` list) via
`jobpilot_sources/config.py`, and the provider registry lives in
`source_adapters/jobpilot_sources/providers/__init__.py:14` with the allowlist in
`config.py:15`. There is currently **no shared platform-name data file** and no divergent
Python/C++ blacklist (none exists at all today).

**Ingestion today** — `job_ingestion_service.cpp` enforces byte caps (`:20-28`), batch
1..100 (`:166-179`), the `manual/import/crawler` source enum (`:64-66`) and per-item field
validation (`:69-137`); `JobIngestionResult` (`job_ingestion_service.h:22-26`) is only
`created/updated/total` — **no quality verdict, no findings, no review state**;
`job_extra_fields.cpp` owns 0014 field normalisation. The existing internal endpoints use
`X-Internal-Token` with a constant-time comparison (0010 precedent) — the mechanism reused by
decision 2.

**Capture side today** — `capture/artifact.py` enforces required `company`/`title`/
`source_url` (`:69-71`), byte caps (`:37-47`) and shape checks (`:89-190`);
`publisher.py:22-43` rejects duplicate `source_url` inside a batch; `bookmarklet.js` truncates
the description at 4,000 characters by default (`DEFAULT_MAX_DESCRIPTION`) — kept unchanged
(decision 5). These are **shape** checks only: they cannot tell a platform name from an
employer, a homepage from a job page, or navigation text from a job description.

**Schema tests** — `backend/tests/test_schema.cpp` asserts `jobs` columns/indexes (`:81`,
`:181`, `:220`, `:266`, `:316`, `:338`); new columns/table/indexes must be asserted there on
a fresh DDL database **and** a migrated pre-0015A database (the 0014 precedent).

**Real-browser findings this stage must address** (0014 §19.6, 【用户报告】): Edge succeeded
on a real job **detail** page; 实习僧 **homepage** produced JSON but is **not a valid
single-job sample**; 牛客 **desktop** produced JSON with the **employer misidentified as the
platform name** and the **description polluted by recommended-job content**; 牛客 **mobile**
produced JSON with duties/requirements but **company missing**. `publish-capture`, the backend
import and MySQL persistence were **not** executed in that run.

**No C++ URL helper exists** — a search for `UrlParser|ParseUrl|class Url|IsValidUrl` across
`backend/{include,src}` and `service/{include,src}` (2026-10-10) returns nothing; the URL
shape check in §6.1 is new, small and self-contained.

## 3. Scope

### 3.1 First batch — exactly three items

1. **`source_url` in the job LIST API** (`/api/v1/jobs`, `/api/v1/jobs/recommended`),
   including nullability, the data contract §5.5.1, `docs/04` §5.1 and its tests.
2. **Deterministic pre-ingest checks + `needs_review` queue + protected review endpoints**
   (§6.1-§6.4, §5.2, §5.5.2), wired into `/internal/jobs/import` and
   `/internal/jobs/source-sync`, with `reject` diagnostic errors, approve/fix/reject
   resolution, the public-visibility rule and the **historical classification/backfill
   procedure** (§5.4.2).
3. **Education / job_nature / deadline into query filter + sort** (§6.5), through
   `JobSearchFilter` → `NormalizeJobSearchFilter` → DAO parameters → `docs/04`, with the
   `docs/03` index note and `docs/04` §5.1 updated **in the same change** (§5.6) and every
   new index justified by a real query plan (§5.3).

### 3.2 Explicitly excluded from the first batch (four items)

| # | Excluded item | Why now | Where it belongs |
|---|---|---|---|
| 1 | **User-visible integration of match score / reasons / sorting** | The deterministic score is real but thin while candidate data is incomplete (no education/graduation/campus status; PDF resumes yield no text); making the list depend on `ai_tasks` state is a separate contract decision | Follow-up stage (§17) after the candidate-data work |
| 2 | **A frontend and a fully clickable job list** | Requires a presentation and access-model decision; the backend deliberately has no roles (user sessions + internal tokens only, decision 2 keeps it that way) | Follow-up stage (§17); this stage only makes the payload complete enough to be displayed |
| 3 | **Any new China source adapter** | 0014 Step 5 is environment-limited; no compliant list contract exists, so an adapter cannot be verified | Re-entry conditions in the 0014 plan §20.4 |
| 4 | **Auto-apply / auto-submit** | Standing ADR-0011 non-goal (the two upstream projects refuse it too) | Not planned; would need a new ADR |

Also out of scope (kept from 0014 decisions): canonical multi-source identity/merge, source
`MissingPolicy`, deadline-driven auto-closing. **Retro-flagging is in scope only as the
auditable, owner-approved procedure of §5.4.2 — never as an automatic migration side effect.**

## 4. Research Findings

From the task-17 read-only audit (JobPilotAI vs `career-ops-hq/career-ops`,
`MadsLorentzen/ai-job-search`; GitHub contents/trees API, 2026-10-10):

1. **Triage, not per-item review.** ai-job-search `/rank` separates triage scores from final
   evaluation, excludes what is already tracked, and surfaces a bounded shortlist
   (`rank.md` L15-L43). Applied: the queue is a triage inbox with reason codes.
2. **"Score only from actually fetched content"** (`rank.md` L58) and "postings are untrusted
   input … fetches no links from their body" (README L145) match JobPilotAI's rule (never
   re-fetch posting links). Applied: checks use only the delivered payload.
3. **Robots-first / decline login-walled sources** already exist in both codebases
   (`source_adapters/jobpilot_sources/robots.py`, `fetch.py`; `tools/robots_check.py`).
4. **Mechanism over prompt** for data quality: career-ops ships an in-code fact check
   (README L161) and provenance-labelled requirement weights (L167) — this stage follows the
   *mechanism* idea only (deterministic checks).
5. **Not copied**: file-canonical storage with a derived index (career-ops `DATA_CONTRACT.md`
   L36/L38) — MySQL stays authoritative; business logic in prompts/markdown (upstream
   `modes/`, `.claude/commands/`) — our checks and state transitions stay typed C++.
6. **No role system anywhere upstream or locally**: upstream gates human actions with
   explicit approval steps, not RBAC; JobPilotAI has user sessions + internal tokens only —
   which is exactly why decision 2 reuses the internal token instead of inventing roles.

## 5. Contracts

### 5.1 Public API (`docs/04` §5.1, §5.2)

- **Additive response key**: every item of `GET /api/v1/jobs` and
  `GET /api/v1/jobs/recommended` gains `"source_url": string|null`, same semantics as the
  detail response (`null` when the row has none).
- **Additive optional query parameters** (validated in `NormalizeJobSearchFilter`; invalid
  values → the existing `400` envelope):
  - `education_max=<1..5>` → `education_required IS NULL OR education_required <= ?`
    (unknown requirement stays visible; documented).
  - `job_nature=<1|2|3>` → exact match.
  - `deadline_before` / `deadline_after=<UTC 'YYYY-MM-DD HH:MM:SS'>` → instant range; NULL
    deadlines match neither bound.
  - `open_only=true` → `deadline IS NULL OR deadline >= NOW()`; **never** touches
    `status`/`closed_at`/freshness.
  - `sort=created_desc|deadline_asc|education_asc` → whitelisted; default `created_desc`
    (unchanged); `deadline_asc` puts NULLs last; ties break on `id`.
- **Public quality rule (decision 1)**: public search and recommended results return **only
  `review_status = 0` (`clean`) rows**. Rows in `needs_review` are reachable **only** through
  the protected internal queue and are **not exposed by any public endpoint**. `rejected`
  input is never stored as a job.
- **Compatibility**: the public predicate is the stage's only behavioural change; it is
  enabled **only after** the historical classification assessment of §5.4.2, and can be
  disabled by configuration without a schema change (§10).

### 5.2 Internal API (`docs/04` §5.4, §5.5 + new section) — decision 2

- `/internal/jobs/import` and `/internal/jobs/source-sync` responses gain per item
  `"findings": [{"code","reason","outcome","review_status"}]` plus counters
  `"needs_review"`, `"rejected"`, `"clean"`; existing keys keep their meaning. `code` is a
  stable machine code, `reason` a human-readable sentence.
- **New** `GET /internal/jobs/review-queue?status=needs_review&page&page_size`: queued jobs
  with **review reason (code + human-readable text)**, **current review status**, and the
  **job identifiers + triage fields** (job id, title, company, `source_url`, `source`,
  description excerpt).
- **New** `POST /internal/jobs/review/{job_id}` body
  `{"decision":"approve"|"fix"|"reject","reviewer":"<label>","note":"...","fix":{...}}`;
  the response returns the job identifiers, the new status and the recorded decision.
- **New** `POST /internal/jobs/review/backfill?mode=dry_run|apply|revert` — the auditable
  historical procedure of §5.4.2 (dry-run writes nothing).
- **Access control**: these endpoints **reuse the existing internal token mechanism**
  (`X-Internal-Token`, constant-time comparison, exactly as `/internal/jobs/import` does).
  There is **no new role system**; `reviewer` is an audit label, not an authenticated
  identity. **The token is never logged** (existing invariant, asserted by a log-scan test).
  Queue queries and review actions are reachable **only** through these protected internal
  endpoints.

### 5.3 Schema (`docs/03` §9.2/§9.3 + `database/schema/03_job.sql`)

**Used, not new** (already present from 0014): `education_required`, `job_nature`,
`deadline`, `deadline_date_only` (+ `experience_years_min/max`, projection only).

**New columns/table**:

```text
jobs.review_status   TINYINT      NOT NULL DEFAULT 0   -- 0 clean / 1 needs_review / 2 quarantined
jobs.reviewed_at     DATETIME     NULL
jobs.review_note     VARCHAR(255) NULL
job_review_items(
  id BIGINT UNSIGNED PK, job_id BIGINT UNSIGNED NOT NULL,
  reason_code VARCHAR(64) NOT NULL, reason_text VARCHAR(255) NULL,
  outcome VARCHAR(16) NOT NULL,          -- 'rejected' | 'needs_review' | 'accepted_advisory'
  resolution VARCHAR(16) NOT NULL DEFAULT 'open',  -- open | approved | fixed | rejected
  resolved_by VARCHAR(64) NULL, resolved_at DATETIME NULL,
  create_time DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
  KEY idx_job (job_id), KEY idx_reason (reason_code), KEY idx_resolution (resolution)
)
KEY idx_review_status (review_status, id)   -- queue scan / public exclusion
```

**Index design (decision 3) — proposals only, each needs a real query plan.**
`education_required`, `job_nature` and `deadline` are the three focus fields. **Before
implementing**, review the current schema, the seven existing `jobs` indexes and the real
query conditions; then choose single-column vs composite **based on `EXPLAIN` output** and
record for each new index: the exact query it serves, the plan before/after, and why a
smaller/larger index was rejected. **No duplicate index; no index without evidence.**
Candidate queries to plan for:

| Candidate query | Predicate shape | Index hypothesis to test with `EXPLAIN` |
|---|---|---|
| Deadline-ascending list | `status=1 AND closed_at IS NULL AND review_status=0 ORDER BY deadline ASC, id DESC` | single-column `idx_deadline (deadline)` vs `(status, review_status, deadline)` |
| Campus filter | `job_nature = ? AND education_required <= ?` (plus the default predicates) | single-column `idx_job_nature`, `idx_education_required`, or composite `(job_nature, education_required)` |
| Existing scope/status path | `source, source_scope, status` | already served by `idx_source_scope_status` — must **not** be duplicated |

Special note to verify with `EXPLAIN`: the `education_required IS NULL OR education_required
<= ?` form can defeat index use; if the plan shows no benefit, **do not add the index** and
record that finding in the step evidence instead.

`uk_source_url` remains the only idempotency key; `review_status` never participates in it.

### 5.4 Migration and historical data

#### 5.4.1 Schema migration

There is no `database/migrations/` directory: fresh installs use
`database/schema/03_job.sql`, existing databases are upgraded by an explicitly recorded ALTER
block in `docs/03` §9.4 (0014 precedent: `docs/03:303`, §9.4 `:477`). This stage (1) adds the
DDL above to `database/schema/03_job.sql`, (2) adds the matching ALTER block to `docs/03`
§9.4, (3) extends `backend/tests/test_schema.cpp` to assert it on **both** a fresh-DDL
database and a migrated pre-0015A database. All **column/table** changes are additive and
nullable; no backfill runs as part of the migration itself.

#### 5.4.2 Historical rows: measure → classify → dry-run report → approved backfill (decision 1)

Existing rows must **not** be blanket-marked `clean` and must **not** silently disappear.
The procedure is explicit, auditable and reversible:

1. **Measure** — read-only counts of existing rows by `review_status` (post-migration: all
   `0`) and by active/closed/freshness, recorded as the baseline.
2. **Classify (dry run)** — `POST /internal/jobs/review/backfill?mode=dry_run` evaluates
   existing rows with the **same** deterministic C++ check function used at ingest (single
   source of logic; no duplicated SQL heuristics), writes **nothing**, and returns: the
   count that *would* become `needs_review` per reason code, the share of active jobs
   affected, and a bounded, credential-free sample of the top causes.
3. **Compatibility assessment** — before enabling the public `review_status = 0` predicate,
   the dry-run report is reviewed against the current active-job count. If the share that
   would become hidden exceeds the documented threshold (**proposal: > 5 % of active jobs or
   > 100 rows**), work stops for an owner decision (§12 stop condition) instead of mass-hiding
   legacy jobs.
4. **Approved backfill** — `mode=apply` is run only after explicit owner approval; it sets
   `review_status`/`job_review_items` for the classified rows, records **counts before and
   after** (per reason code and totals) in the stage evidence, and keeps a row-level audit
   trail with `resolved_by` = the approving maintainer label.
5. **Rollback statement** — `mode=revert` restores the pre-backfill `review_status`
   distribution from the recorded audit trail, and independently the public predicate can be
   disabled by configuration, which restores visibility immediately **without touching data**.
   Neither operation deletes rows.

No automatic startup job, no silent migration-time classification, no "everything is clean"
default claim in the documentation.

### 5.5 Data contracts and acceptance criteria

#### 5.5.1 `source_url`

| Aspect | Contract |
|---|---|
| Scheme/authority | absolute `http` or `https` only; no credentials in the authority; no fragment (stripped) |
| Length | ≤ 255 bytes after normalisation (column cap) |
| Detail-page plausibility | path must not be empty/`/` or a known list/search path (`/jobs`,`/job`,`/search`,`/list`,`/positions`, site-root variants); a query-only URL is never a detail page; at least one id-like segment (a run of ≥4 digits or a UUID-like token) **or** a recognised detail marker (`/job/`,`/jobs/`,`/position/`,`/detail`,`/jobinfo`,`/job-detail`) |
| Uniqueness / idempotency | `uk_source_url` remains the sole key; a repeat observation/import performs the existing `INSERT … ON DUPLICATE KEY UPDATE` on the **same** row |
| Collision with a manual row | F14 unchanged: an observation matching `source='manual'` is skipped whole and reported via `skipped_manual`/`skipped_manual_urls` |
| On failure | non-http(s), unparsable or > 255 B → **`reject`** with a diagnostic (`source_url_unusable`); implausible-but-storable shape → **`needs_review`** (`source_url_not_detail`) |
| **Acceptance criteria** | (1) list and detail return the identical value; (2) `null` for rows without one; (3) unusable URL → `400` with code + field + reason and **nothing written**; (4) implausible shape → hidden + queued with the reason code; (5) a repeated publish/observation never creates a second row; (6) the F14 manual-row test still passes |

#### 5.5.2 `company`

| Aspect | Contract |
|---|---|
| Presence | non-empty after trim; whitespace-only = **`needs_review`** (`company_missing`, option B 2026-10-10) — stored hidden so a reviewer can supply the employer; **never** a whole-item reject |
| Length | ≤ 128 bytes (existing cap); over-cap → **reject** (`400`), never truncated |
| Platform-name rule (decision 4, option B) | the `employer_is_platform` verdict is asserted **only** when the normalised company exactly matches a registry entry **and** the `source_url` host resolves to that same platform through the registry's `platform_hosts:` mapping (exact/subdomain). An unknown, unmapped or different host records the advisory `platform_host_unverified` and asserts **no** verdict |
| Determinism | employer identity is judged primarily from **deterministic signals** — the source platform identity (`source`/`source_scope`/registry host) and the normalised company name; fuzzy keyword matching is at most a secondary advisory signal and **never** a queue trigger by itself |
| Plausibility | length ≥ 2; not all digits/punctuation; not the `source_url` host without its TLD |
| On failure | missing/whitespace → **`needs_review`** (`company_missing`, stored hidden); over-cap → **`reject`** (diagnostic); platform-name match **with** a confirming host → **`needs_review`** (`employer_is_platform`); platform-name match with an unknown/other host → **clean + advisory** (`platform_host_unverified`); degenerate but storable (`title_equals_company`, implausible-but-present) → **`needs_review`** (`title_equals_company`) or **clean + advisory** (`employer_implausible`) |
| Provenance | the accepted employer is whatever the payload declares; the plan never invents an employer from the platform name or from the title |
| **Acceptance criteria** | (1) empty/whitespace → hidden + queued (`company_missing`), **nothing rejected and the row is stored**; (2) exact platform match with a confirming host → hidden + queued; (3) platform match with an unknown/other host → visible + advisory, never guessed; (4) over-cap → `400`, row unchanged; (5) unknown platform names never queue (fail-open); (6) the registry file (names **and** host mapping) is the only place a platform is listed — a test asserts no hard-coded platform list exists in either language |

#### 5.5.3 `description` (decision 5)

| Aspect | Contract |
|---|---|
| Optionality | optional; absence is **not** an anomaly and never blocks a job |
| Hard cap | **48 KiB of UTF-8 bytes = 49,152 bytes** at the application layer **and** throughout the ingest chain |
| Unit at each boundary | C++: `std::string::size()` already counts **bytes**; Python: strings are Unicode — the check must be `len(value.encode("utf-8"))`, **never** `len(value)`; the bookmarklet counts **characters** (UTF-16 code units) and keeps its unchanged 4,000-character extraction default; MySQL: `jobs.description` is `TEXT` = **65,535 bytes** capacity, so 48 KiB fits with headroom and the column is unchanged |
| Over-long content | **never silently truncated**: it is **`reject`ed** with a diagnostic (`description_too_large`, `limit_bytes` + `actual_bytes`), or routed into an explicit repair flow (re-capture, or `fix` on a queued record). The bookmarklet default is **not** widened |
| Pollution detection | a versioned marker list (推荐岗位 / 相关职位 / 热门职位 / 猜你喜欢 / 浏览更多 / 登录注册 / breadcrumb patterns) with a **conjunctive** threshold: ≥ 3 distinct markers **and** marker byte-share ≥ 20 % of the description (or an exact match to a known navigation block template) |
| Preservation | otherwise stored **verbatim** (no rewriting, no auto-stripping); a single stray marker is advisory, not a defect |
| Stored vs dropped | stored: the description exactly as delivered (≤ cap); dropped: nothing — an over-cap description is rejected, not cut |
| **Acceptance criteria** | (1) clean description → byte-identical round-trip; (2) 49,152 bytes exactly → accepted; 49,153 bytes → `400` with `limit_bytes=49152`; (3) a multi-byte UTF-8 case (e.g. CJK) proves the boundary is counted in bytes, not characters; (4) polluted fixture above the conjunctive threshold → hidden + queued; a single stray marker → **clean + advisory** and still visible; (5) no code path truncates a description (test asserts stored bytes == delivered bytes, or a `400`); (6) the bookmarklet default stays 4,000 characters |

### 5.6 Same-change contract sync (must not diverge)

The `docs/03` note at `:240-242` ("0014's six new columns get **no index** and **do not enter
search/ranking**") and `docs/04` §5.1 (parameter + response list) **must be updated in the
same change that lands item 3's queries** — the same commit/slice, not a follow-up doc task.
Rationale: the documented contract is the interface; a code-first landing with a stale
contract is a divergence defect. Concretely: when Step 5 lands, the same change updates
(a) `docs/03` §9.2/§9.3 + the no-index note (now: *which* indexes exist and *which* queries
they serve, per §5.3), (b) `docs/04` §5.1 (new optional parameters + the `source_url`
response key), and (c) `backend/tests/test_schema.cpp` for any index that survived the
`EXPLAIN` check.

## 6. Selected Design

### 6.1 Deterministic pre-ingest outcomes and triggers

Every trigger is a pure function of the delivered payload (no I/O, no clock, no randomness,
no LLM). There are **three outcomes** (decision 1):

- **`reject`** — cannot be safely normalised: HTTP `400` with a **diagnosable error**
  (`code` + `field` + `reason`), **nothing written**.
- **`needs_review`** — storable but would mislead a user if published: stored with
  `review_status = 1`, **hidden from the public API**, queued with a **stable reason code +
  human-readable reason**, and returned in the response so the caller learns immediately.
- **`clean`** — everything else, including **weak/ambiguous signals**, which are recorded as
  *advisory* findings (`outcome = 'accepted_advisory'`) for audit **without** changing
  `review_status`; the row is visible. This is the explicit answer to "a transient parsing
  miss must not silently hide a good job".

| # | Stable code | Deterministic condition + threshold | Outcome | Why |
|---|---|---|---|---|
| 1 |`company_missing`| `company` empty/whitespace after trim | **needs_review** (option B, 2026-10-10) | A missing employer is a **fixable** data-quality problem: store it hidden and let a reviewer supply the employer instead of discarding the posting |
| 1b |`company_too_large` / `title_too_large` / `location_too_large` / `source_url_too_large` / `skill_too_large` | existing byte caps `job_ingestion_service.cpp:20-28` exceeded (128/128/64/255/64 B) | **reject** (`400`) | Existing behaviour, now with a diagnostic code per field |
| 1c |`source_url_unusable`| not absolute http(s) or unparsable | **reject** | Cannot be stored or opened at all |
| 1d |`description_too_large`| description > 48 KiB UTF-8 bytes (§5.5.3) | **reject** (never truncate) | Decision 5; a whole-page grab is an upstream defect the caller must fix |
| 2 |`employer_is_platform`| normalised `company` equals a registry platform entry **and** the `source_url` host resolves to that **same** platform via the registry's deterministic `platform_hosts:` mapping (exact/subdomain) | **needs_review** | Showing the platform as the employer is misleading; both signals must agree, exact match only, curated list |
| 3 |`platform_host_unverified`| normalised `company` matches a registry platform entry but the host is unknown, unmapped, or resolves to a **different** platform | **clean + advisory** (option B, 2026-10-10) | The host does not confirm the platform; the rule must not guess, so no verdict is asserted and the row stays visible |
| 4 |`source_url_not_detail`| absolute http(s), ≤255 B, but path is empty/`/`, in the list/search set, query-only, or has no id-like segment and no detail marker | **needs_review** | The "open original posting" affordance would be wrong (the 实习僧-homepage case) |
| 5 |`title_equals_company`| normalised `title == company` | **needs_review** | The employer is unknown/degenerate; a human decides the real employer |
| 6 |`description_polluted`| ≥ 3 distinct markers **and** marker byte-share ≥ 20 % (or exact known navigation-block match) | **needs_review** | Above this conjunctive threshold the text would visibly mislead; below it → advisory only |
| 7 |`duplicate_content_in_batch`| ≥ 2 items in one payload with equal normalised company+title+location but different normalised `source_url` | **needs_review** (all of them) | Duplicate postings in a public list are a visible defect |
| 8 |`deadline_flag_inconsistent`| `deadline_date_only = 1` and the stored instant ≠ the Beijing-noon UTC instant, or `deadline_date_only = 0` for a parser-marked date-only source | **needs_review**, never auto-corrected | F13 integrity; a human confirms the kind |
| 9 |`employer_implausible`| company length < 2, or all digits/punctuation, or the bare `source_url` host | **clean + advisory** | Weak signal; hiding a job over it would violate §6.3 |
| 10 |`description_marker_minor`| 1–2 distinct markers, or marker share < 20 % | **clean + advisory** | Same reason |
| 11 |`source_url_shape_unrecognised`| absolute http(s), ≤255 B, no recognisable list/detail pattern either way | **clean + advisory** | Unknown shapes must fail open |
| 12 |`platform_name_unknown`| company matches no registry entry | **clean** (no finding) | Fail-open: unknown is not suspicious |

`review_status` derivation: any `reject` → the whole item is refused (nothing written);
otherwise any `needs_review` finding → `1`; otherwise `0` (advisory findings may still exist).

### 6.2 Post-review outcomes

| Decision | Effect |
|---|---|
| **approve** | `review_status` → `0`; `reviewed_at`/`review_note`/`resolved_by` recorded; the job appears in public search from the next query; the waived reason codes are marked `approved` on the review items |
| **fix** | The reviewer supplies corrected `company`/`description`/`source_url`; the service validates them against the **same** §5.5 contracts (including the 48 KiB byte cap); on success it updates only those fields, records a per-field before/after audit row, sets `review_status` → `0`, and the job becomes visible. Validation failure → `400` with a diagnostic, **no state change** |
| **reject** | `review_status` → `2`; hidden permanently, `note` required, row **retained** for audit (never deleted) |
| Re-resolution | repeating the identical decision is an idempotent `200` no-op; a conflicting second decision → `409` |
| Diagnostics | every rejection (ingest or review) returns a stable `code`, the affected `field`, a human-readable `reason`, and the job/queue identifiers where applicable |

### 6.3 How a normal job is never silently blocked

1. **Heuristics do not hide.** Codes 9-11 are advisory and leave `review_status = 0`; only
   the defect codes 2-8 can queue, and each has a concrete threshold.
2. **Fail-open on the unknown.** An unrecognised platform name or URL shape is `clean`; only
   a *curated* platform match or a *known* list/search URL shape queues.
3. **No silent action.** Every outcome is returned in the import/source-sync response
   (`findings` with code + human-readable reason + outcome, plus `needs_review`/`rejected`/
   `clean` counters) *and* persisted in `job_review_items`; a quarantined row is reported in
   the same request that stored it.
4. **Reversible.** Every `needs_review`/`rejected` row has an `approve`/`fix` path; nothing
   is deleted; the public predicate can be switched off by configuration.
5. **Structured adapters are exempt from the heuristics.** `greenhouse`/`lever`/`ashby`
   observations carry structured fields, so codes 4/5/6/10/11 do not apply; only the hard
   codes (1-3, 7, 8) can trigger — and those are unambiguous defects or duplicates.
6. **A transient parsing miss stays visible or reported.** A one-off shape failure either
   lands as advisory (`clean`) or is reported as `needs_review` in the same response and the
   queue — never as a silent disappearance.

### 6.4 Queue and public visibility

`GET /api/v1/jobs` and `/recommended` add `review_status = 0` to the default WHERE clause
(beside `job_dao.cpp:154`); `needs_review` rows are reachable only through the protected
internal queue; approve/fix returns a row to the default result set. The ingestion counters
make the state visible without the queue. The predicate is enabled only after §5.4.2's
compatibility assessment and is configuration-switchable.

### 6.5 Query filter + sort

Extend `JobSearchFilter` (`job_search_filter.h:9-23`) with `educationMax`, `jobNature`,
`deadlineBefore`, `deadlineAfter`, `openOnly`, `sort`; extend
`NormalizeJobSearchFilter` (`job_search_filter.cpp`) with enum/range validation and the
documented NULL semantics; extend the WHERE builder beside `job_dao.cpp:154-232` and switch
the ORDER BY at `:409`/`:476` on a whitelisted enum (never string-interpolated); add the new
parameters to `JobController::ParseSearchQuery` (`job_controller.cpp:248`); make `CountJobs`
(`:427`) apply the identical predicate set. Every index this needs is chosen by `EXPLAIN`
evidence (§5.3) and the contract sync of §5.6 happens in the same change.

### 6.6 List payload

`JobSummary` gains `std::optional<std::string> sourceUrl`; both SELECTs
(`job_dao.cpp:398-407`, `:465-472`) add `j.source_url`; the list writers in
`JobController::Search`/`Recommended` emit `"source_url"` with the same nullability rule as
the detail response (`:744`). No other key changes.

### 6.7 Dependency table: Schema / DTO / DAO / Service / Controller

| Layer | Change | Additive? | Who calls it / what it forces |
|---|---|---|---|
| **Schema** | `jobs.review_status/reviewed_at/review_note`, `job_review_items`, `idx_review_status`; **candidate** indexes decided by `EXPLAIN` | Additive (columns/table); indexes evidence-gated | Forces the DAO write path, `test_schema` assertions, `docs/03` §9.2/§9.4 + the index note, and the fresh-DDL file |
| **Config/registry (decision 4)** | one platform-name list in the **source registry layer** (single file), read by Python and by the C++ service (yaml-cpp already present) via a `service/config/config.yaml` path key validated at startup | Additive | Forces a Python loader, a C++ loader, and a test asserting no second/hard-coded list exists; **Step 0 confirms the exact existing config shape first** |
| **DTO** | `JobSummary.sourceUrl`; new `JobReviewItem`/`JobReviewQueuePage`/`JobReviewDecision`/`QualityFinding` result types | Additive | Forces both SELECT projections and the two writers; forces the `docs/04` §5.1 response row |
| **DAO** | `j.source_url` in `SearchJobs` + `SearchRankingCandidates`; new predicates; new `ListReviewQueue`/`ResolveReview`/`InsertReviewItems`/`BackfillReviewStatus` | Additive methods | Called by `JobService`/`JobIngestionService`; forces the DAO impl + `test_job_dao`; needs the schema first |
| **Service** | `JobIngestionService::EvaluateJobQuality` (pure) + writing verdicts inside the existing upsert transaction; backfill dry-run/apply/revert uses the **same** function; `JobService`/`JobRankingService` pass the new filter/sort fields | Behavioural (new verdicts) | Called by the controllers; forces the DAO methods and tests; **no new service process** |
| **Controller** | `JobController` (list writers + `ParseSearchQuery`); **new class** `InternalJobReviewController` for queue/review/backfill | Additive endpoints | Follows the existing Controller→Service→DAO pattern (a class in the same binary, not a new module boundary); forces routes in `router.cpp` + `docs/04` |
| **Python** | mirrored pre-flight checks (same reason codes, byte-based length check) + findings in `capture/artifact.py`/`publisher.py` output | Additive | UX only; **never** authoritative — the C++ gate decides |

Change-order forcing: schema → DAO impl → service (compile) → tests; `sourceUrl` DTO → two
DAO projections → two writers; new params → normaliser → DAO predicates → controller parsing
→ `docs/04` + `docs/03` (same change); config file → both loaders → checks.

### 6.8 No new services or architecture refactor

No new microservice, process, message queue, cache layer or Python package; no layering
change. The work extends the existing `Controller → Service → DAO → MySQL` chain inside
`service/` plus additive checks inside the existing
`source_adapters/jobpilot_sources/capture/` package. The new `InternalJobReviewController` is
a class in the existing service binary, matching the project's one-controller-per-concern
pattern, and the historical procedure reuses the same controller/service/DAO.

### 6.9 Rejected Alternatives

1. **Blanket-marking all existing rows `clean`** — would silently declare unverified data
   good; rejected for the measure→classify→dry-run→approved-backfill procedure (§5.4.2).
2. **Mass-hiding legacy rows on migration** — would make jobs disappear at scale without
   assessment; rejected (the predicate is enabled only after the compatibility check).
3. **Python-only quality gate** — the service must not trust the client; C++ is authoritative.
4. **Hard-reject everything suspicious** — destroys legitimate jobs on a heuristic; only
   *unnormalisable* input is rejected, defects queue, weak signals are advisory.
5. **Silent auto-fix / silent truncation** (blanking a platform name, stripping nav text,
   cutting an over-long description) — mutates delivered data without a decision; rejected
   (decision 5 requires a diagnostic rejection/repair path).
6. **A role/permission system for the queue** — rejected: the existing internal token is the
   established mechanism and the project has no RBAC (decision 2).
7. **Two hard-coded platform lists (Python + C++)** — divergence risk; rejected for one
   registry-layer file (decision 4).
8. **Adding indexes without a plan** — rejected; every index needs `EXPLAIN` evidence of the
   query it serves, no duplicates, no speculatively composite indexes (decision 3).
9. **Fuzzy keyword-only employer judgement** — rejected in favour of deterministic signals
   (source platform identity + normalised name); fuzzy matching is advisory at most.
10. **In-memory filtering/sorting** — breaks exact pagination; filtering stays in SQL with a
    whitelisted ORDER BY.
11. **A search engine / full-text index now** — premature (`docs/03:244` records the
    LIKE-first plan).
12. **`experience_years_*` as filter inputs in this batch** — the brief scopes education /
    job_nature / deadline; more inputs multiply NULL semantics without user evidence.
13. **A bare `needs_review` boolean** — rejected: every trigger carries a stable code, a
    condition/threshold and an outcome (§6.1).

## 7. Invariants

1. **F13 (deadline)** unchanged: date-only sources store the Beijing-noon UTC instant with
   `deadline_date_only = 1`; explicit times are preserved; unknown kind stays `NULL`;
   filtering/sorting **never** closes a job, sets `status`, or writes freshness; a
   `deadline_flag_inconsistent` finding never auto-corrects the value.
2. **F14 (manual wins)** unchanged: an observation matching `source='manual'` is skipped whole
   and reported in `skipped_manual`/`skipped_manual_urls`; checks and the queue must not
   weaken it.
3. **Only `clean` is public**: no public endpoint may return a row with `review_status != 0`;
   the queue is internal and token-protected only.
4. **`uk_source_url` remains the only idempotency key**; `review_status` never participates.
5. **Observation completeness** unchanged: `complete=false` never closes a job; no quality
   verdict may close a job.
6. **No untrusted re-fetch**: checks use only the delivered payload; posting bodies and their
   links are never fetched.
7. **No credential leakage**: the internal token is never logged or echoed (test-asserted);
   `reviewer` is a label, not a verified identity.
8. **Description bound is byte-based**: 48 KiB = 49,152 UTF-8 **bytes** at both the
   application and ingest layers; over-long is rejected with diagnostics, never truncated;
   the bookmarklet keeps its 4,000-character default.
9. **Single source of truth for platform names**: exactly one registry-layer file; neither
   language may hard-code a second list (test-asserted).
10. **No index without evidence**: each new index documents the query it serves plus the
    `EXPLAIN` before/after; no duplicate of an existing index.
11. **Historical rows are never implicitly trusted or hidden**: no blanket `clean` marking, no
    mass hiding; classification is measured, dry-run, approved and reversible.
12. **Contracts additive**: no existing key/parameter/status/error code changes meaning; the
    public predicate is the only behavioural change and is switchable.
13. **Deterministic verdicts**: same payload → same outcome; no clock, randomness, network or
    LLM in the decision path.
14. **Public search default** stays `status = 1 AND closed_at IS NULL` plus
    `review_status = 0`; `include_stale`/`posted_within_days` semantics untouched.
15. **Layer direction** preserved; the check function is pure and lives in the service layer.
16. **Documented contract == real interface**: `docs/03` and `docs/04` are updated in the same
    change that lands the behaviour (§5.6).
17. **No silent data loss**: rejected input writes nothing; quarantined rows are retained;
    descriptions are never mutated without a review decision.

## 8. Testing Plan

Targeted first, affected label second, **L4 once at stage close**.

**L1 (per touched binary)**

| Area | Binary/suite | Cases |
|---|---|---|
| List payload | `test_job_http.cpp`, `test_job_dao.cpp` | `source_url` present/correct in `/jobs` and `/recommended`; `null` when NULL; detail unchanged |
| Filters/sort | `test_job_dao.cpp`, `test_job_service.cpp`, `test_job_http.cpp` | `education_max` (incl. NULL rows), `job_nature` exact, `deadline_before/after`, `open_only` (NULL visible); `deadline_asc` NULL-last + stable tie-break; invalid value → `400`; `CountJobs` == returned total for every combination |
| Public quality rule | `test_job_http.cpp`, `test_job_dao.cpp` | a `needs_review` row is **absent** from `/jobs` and `/recommended` but present in the internal queue; `approve`/`fix` makes it appear; `reject` keeps it hidden |
| Reject diagnostics | `test_job_ingestion_http.cpp`, `test_job_ingestion_service.cpp` | one case per reject code (`company_missing`, caps, `source_url_unusable`, `description_too_large`): `400` + `code`/`field`/`reason`, and **no row written** |
| Queue triggers | `test_job_ingestion_service.cpp` | one case per §6.1 code; clean payload → no findings/`review_status=0`; determinism (same input twice); advisory codes (9-11) leave the row **visible**; structured-adapter exemption for codes 4-6/10/11 |
| Description boundary | `test_job_ingestion_service.cpp`, `test_job_ingestion_http.cpp` | exactly 49,152 bytes accepted; 49,153 rejected with `limit_bytes`/`actual_bytes`; a multi-byte CJK case proves byte (not character) counting; stored bytes == delivered bytes for accepted input |
| Platform registry | `test_job_ingestion_service.cpp` + a repo test | a registry entry queues `employer_is_platform`; an unknown name does **not**; a test asserts no second hard-coded platform list exists in Python or C++ |
| Review endpoints/auth | `test_job_http.cpp` (internal) | missing/wrong token → `401`/`400` for queue, review and backfill; responses carry reason + status + job ids; **log-scan test asserts the token never appears in logs** |
| Historical procedure | `test_job_http.cpp` + service tests | `mode=dry_run` writes nothing and returns per-code counts + bounded sample; `mode=apply` writes with before/after counts; `mode=revert` restores the distribution; no startup auto-run |
| Schema/migration | `backend/tests/test_schema.cpp` | new column/table on fresh DDL **and** migrated DB; each accepted index asserted; no duplicate index |
| Capture pre-flight | `test_capture_artifact.py`, `test_capture_publisher.py` | mirrored codes for platform-as-employer / missing company / homepage URL / polluted description; the 48 KiB check uses `encode("utf-8")`; exit-code semantics unchanged; the 4,000-character default unchanged |

**L2** — `ctest -L job` once the C++ slices land. **L4 (once, at close)** — service Debug +
ASan + UBSan full CTest, service and root Release builds, both Python suites, per
`docs/ai/agent-workflow.md` §2.2/§3 with the environment probe first.

## 9. Phased Steps (dependencies + acceptance criteria)

| # | Step | Depends on | Acceptance criteria |
|---|---|---|---|
| 0 | **Recon (decision 3+4)**: review the schema, the seven existing `jobs` indexes and the real query conditions; run `EXPLAIN` for the candidate queries in §5.3; confirm the existing config shape (C++ `service/config/config.yaml`, Python `source_adapters/config.example.yaml`) and choose the single registry-file path | — | a written index decision (which index, which query, `EXPLAIN` before/after, duplicates ruled out) and the confirmed registry-file location; recorded in the step evidence |
| 1 | **List `source_url`**: DTO field, two SELECTs, two writers, `docs/04` §5.1 | — | `test_job_http` asserts presence/nullability for both endpoints; docs updated in the same change |
| 2 | **Schema + migration**: review columns/table + `idx_review_status` + the Step-0-approved indexes; fresh DDL + `docs/03` §9.4 ALTER + `test_schema` | 0 | schema tests green on fresh **and** migrated DBs; §9.2/§9.3/index note updated; every index carries its query + `EXPLAIN` evidence |
| 3 | **Trigger engine + wiring**: pure `EvaluateJobQuality`, reject diagnostics, write verdicts in the import/source-sync transaction, response fields, registry loader in both languages | 2 | one L1 case per §6.1 code; rejected input writes nothing; advisory codes stay visible; registry single-source test; no I/O in the checker |
| 4 | **Queue + review endpoints + visibility**: `InternalJobReviewController` (queue/review), approve/fix/reject, `review_status = 0` predicate, token reuse, log hygiene | 2, 3 | queue lists with reason+status+ids; approve/fix → visible; reject → hidden+retained; `401` without the token; token never logged; count parity |
| 5 | **Historical classification + enablement (decision 1)**: dry-run report → compatibility assessment → approved `apply` with before/after counts, then enable the public predicate | 3, 4 | dry-run writes nothing; report reviewed; if the hidden share exceeds the threshold → stop for the owner; `apply`/`revert` reproduce counts; the predicate is switchable |
| 6 | **Filter + sort**: education/job_nature/deadline (+ `open_only`, whitelisted `sort`), normaliser, DAO, `docs/04` **and** `docs/03` index note in the same change | 0, 2 | filter matrix + `CountJobs` parity + NULL semantics; `400` on invalid; §5.6 sync verified in the same slice |
| 7 | **Capture pre-flight mirror** + findings in `--json` output (incl. the byte-based 48 KiB check) | 3 | Python tests for the mirrored codes and the byte boundary; exit codes unchanged; bookmarklet default unchanged |
| 8 | **Docs sync + L4 once** | 1-7 | README/AGENTS/project-state/this plan consistent; single full L4 green |

Step 0 gates 2/6; Step 2 gates 3/4/6; Step 4 gates 5; Step 5 gates the public behaviour change.

## 10. Compatibility and Rollback

- **Additive**: new nullable columns, one table, one required index (+ evidence-approved
  ones), one response key, six optional parameters, three internal endpoints and one registry
  data file. Existing clients, the Python adapters and `docs/03` field semantics are unchanged.
- **The single behavioural change** is the public `review_status = 0` predicate. It is
  enabled **after** the §5.4.2 assessment and is **configuration-switchable**; rollback =
  disable the switch (visibility restored immediately, no data change) or remove the clause.
- **Historical data rollback**: `mode=revert` restores the recorded `review_status`
  distribution; the run report keeps counts before/after per reason code.
- **Migration rollback**: the ALTER is additive — leaving unused columns while reverting code
  is safe; dropping the table/columns is only needed if the feature is abandoned.
- **Contract rollback**: the `docs/03`/`docs/04` changes revert with the code; no deployed
  client depends on the new keys.
- **No backfill at migration time**, no destructive operation, no existing row rewritten in
  place without an audit trail.

## 11. Risks

| Risk | Impact | Mitigation |
|---|---|---|
| Enabling the public predicate hides many legacy jobs | Users lose visible jobs at scale | Measure → classify → dry-run report → compatibility threshold → owner approval before enabling; switchable predicate; `mode=revert` |
| False positives (a company genuinely named like a platform entry) | A job queues instead of publishing | Curated registry, exact normalised match, contradicting-signal path, fast human approve/fix, advisory (non-queueing) fallback for weak signals |
| 48 KiB rejections surprise callers | Ingest failures | Stable diagnostic with `limit_bytes`/`actual_bytes`; pre-flight check in the Python publisher; bookmarklet default unchanged (4,000 chars) |
| UTF-8 byte vs character confusion | Wrong boundary enforcement | Unit asserted at each boundary (`encode("utf-8")` in Python, byte size in C++, `TEXT` capacity documented) + a multi-byte test |
| Two platform lists drift apart | Inconsistent employer judgement | One registry-layer file as the single source of truth + a test that no second list exists |
| Speculative indexes | Write cost, no read benefit | `EXPLAIN` evidence required per index; composite indexes only when the plan justifies them; `IS NULL OR <=` treated as a no-index outcome when it does not help |
| Queue backlog | Flagged rows stay invisible | Triage-sized payload with reason + reason text; counters in the ingest response; the historical report exposes the scale |
| No role model | Weak audit identity | Internal token + `reviewer` label documented as the maximum (decision 2); RBAC is explicitly out of scope |
| Contract drift | Docs ≠ interface | §5.6 same-change rule is an invariant and a Step-6 acceptance criterion |
| Description/nav heuristics misfire | Noise or missed pollution | Conjunctive threshold keeps queueing highly specific; single markers are advisory; thresholds in one versioned table |

## 12. Stop Conditions

Stop and report to the Lead (do not widen scope) if:

1. A check would require **fetching the source page or following links** → stop.
2. The design needs **canonical multi-source identity/merge** → stop (separate contract).
3. The queue would need a **role/permission model** instead of the internal token → stop
   (decision 2 fixes the mechanism; anything else is a product decision).
4. The **historical dry-run shows more than the agreed threshold** of active jobs would
   become hidden (> 5 % or > 100 rows, proposal) → stop for an owner decision before enabling
   the predicate.
5. An index cannot be justified by `EXPLAIN` → **do not add it** (record the finding; not a
   stop).
6. New sort/filters would require a **non-additive contract change** or a search engine →
   stop.
7. **F13/F14 semantics** or the 48 KiB byte cap would have to change → stop.
8. A required `docs/03`/`docs/04` change cannot be made **in the same change** → stop and
   report the blocker (§5.6).

## 13. Progress Checkboxes

- [ ] Step 0 recon: index evidence (`EXPLAIN`) + registry-file location confirmed
- [ ] Step 1 `source_url` in the list payload + `docs/04` §5.1 (same change)
- [ ] Step 2 schema + migration + evidence-approved indexes + `test_schema`
- [ ] Step 3 trigger engine + reject diagnostics + registry loader (single source of truth)
- [ ] Step 4 queue + review endpoints + public `clean`-only predicate + token/log hygiene
- [ ] Step 5 historical measure → classify → dry-run → compatibility → approved backfill
- [ ] Step 6 education/job_nature/deadline filter + sort + `docs/03`/`docs/04` same-change sync
- [ ] Step 7 capture pre-flight mirror (byte-based 48 KiB check)
- [ ] Step 8 docs sync + L4 once at stage close

## 14. Known Issues and Follow-ups

- NULL semantics remain documented product choices: `education_max` includes
  unknown-requirement rows; `deadline` range bounds exclude NULL deadlines.
- `experience_years_min/max` stay projection-only in this batch.
- The 48 KiB cap is enforced in bytes; the bookmarklet's pre-check counts characters, so a
  capture near the cap may be rejected at ingest — the publisher must surface the byte count
  to make that diagnosable.
- Queue aging/notification is not built; the `notifications` table exists with **no code
  path** — reusing it is a later decision.
- The review endpoints are internal-only; a human admin surface is the same decision as a
  clickable list (§3.2 item 2).
- The registry file and the marker/threshold table need a maintainer (a small versioned data
  file, not code churn); decision 4 names the project maintainer.
- 0014 §19.6 findings are 【用户报告】; the triggers are designed from them, not reproduced
  locally.
- `reject` codes are returned per item; a batched import containing a rejected item writes
  none of that item's data (the batch response reports per-item outcomes).
- **Test database isolation (0015A, 2026-10-10):** the review backfill `apply`/`revert`
  cases (`test_job_review_http`) must never run implicitly against the shared dev database.
  They require an explicit scratch database (`MYSQL_DATABASE` == `JOBPILOT_TEST_REVIEW_DB`,
  e.g. `jobpilot_review_scratch` loaded from `database/schema/*.sql`), assert the connected
  schema via `SELECT DATABASE()`, skip explicitly when the variable is absent, and fail loudly
  if the dev database is targeted.
- **Scratch-DB credentials (0015A, 2026-10-10):** the `jobpilot` MySQL account has **no grant**
  on `jobpilot_review_scratch`, so the isolated run needs either an explicit
  `GRANT ALL ON jobpilot_review_scratch.* TO 'jobpilot'@'%'` or the documented container-root
  invocation (`MYSQL_USER=root` + the root password); do not assume the default test
  credentials can reach the scratch schema.

## 15. Closed Decisions (owner, 2026-10-10)

All five open decisions are **closed**; the design above implements them verbatim:

1. **Public query vs quality status.** Public job search returns **only `clean`** by default.
   `needs_review` jobs go to a **protected internal review queue** and are **not exposed by
   the public search API**. Invalid input that cannot be safely normalised is **rejected at
   import with a diagnosable error**. Fixable quality problems go to `needs_review` with a
   **stable reason code and a human-readable reason**. Historical data is **measured and
   assessed first**: existing rows are never blanket-marked `clean`, and legacy jobs are never
   allowed to disappear from queries at scale without a compatibility assessment — the safe,
   auditable procedure is in §5.4.2 (measure → classify → dry-run report → approved backfill,
   with counts before/after and a rollback statement).
2. **Review-queue access control.** Reuse the **existing internal token/auth mechanism**;
   queue queries and review actions are reachable **only** through existing-style protected
   internal endpoints; **no new role system**; the **token is never logged**; responses return
   the review reason, the current status and the necessary job identifiers (§5.2).
3. **Index design.** `education_required`, `job_nature` and `deadline` are the three focus
   fields. Before implementing, review the current schema, the existing indexes and the real
   query conditions, then choose single-column vs composite **based on actual query plans** —
   no duplicate indexes, no index without evidence, and every new index records the query it
   serves and why (§5.3, Step 0).
4. **Platform-name list ownership.** The list lives in the project's **source
   registry/configuration layer**, maintained by the **project maintainer**, as a **single
   source of truth** (no divergent hard-coded lists in Python and C++). The existing config
   shape is confirmed first and the minimal-change implementation chosen; employer identity
   prefers **deterministic signals** (source platform identity, normalised company name) over
   fuzzy keywords alone (§5.5.2, §6.1, Step 0/3).
5. **Description length.** The application layer and the ingest chain enforce a hard cap of
   **48 KiB of UTF-8 bytes**; the unit is stated at each language boundary and the MySQL
   `TEXT` byte capacity is noted; over-long content is **never silently truncated** — it is
   rejected or routed into an explicit repair flow with diagnostics; the bookmarklet keeps its
   current **4,000-character default** (§5.5.3).

No decision remains open. Operational confirmations still required during implementation
(not open product decisions): the exact registry-file path (Step 0, using the confirmed config
shapes), the specific index set from `EXPLAIN` (Step 0/2), and the dry-run compatibility
threshold check before enabling the public predicate (Step 5 → §12 stop condition 4).

## 16. Stage Numbering (confirmed)

The owner **confirmed `0015A`** (2026-10-10). The project stage table keeps **0015 = resume
tailoring** and **0016 = open-source packaging** unchanged; this file keeps the name
`0015A-job-list-usability-and-intake-quality.md`. Rationale: this work is 0014 hardening, the
sub-number preserves the recorded stage table and every archived/historical "0014/0015"
reference, and no renumbering churn is justified. No archived plan's numbering is touched.

## 17. Follow-up (later work — not part of this batch, no frontend required now)

Owner's roadmap goal, recorded verbatim:

> 在现有确定性评分与候选人资料能力的实际审计结果基础上，规划只读岗位列表或报表，将匹配分数、可解释的匹配与差距、排序结果及原始岗位链接整合到完整的求职者工作流。

Interpretation for planning only: a **read-only** job list or report that integrates the
existing deterministic score, explainable matches/gaps, sorting and the original posting link
into the applicant workflow. It is explicitly **not** part of the `0015A` first batch and does
not require a frontend in this stage; it depends on the candidate-data work
(education/graduation/campus status, resume text extraction) identified in the task-17 audit,
and it must not be described anywhere as already wired. The four exclusions in §3.2 are the
items that feed this follow-up.
