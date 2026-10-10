# JobPilotAI Current Project State

> Snapshot: 2026-10-10
>
> Baseline HEAD: `ae85868 feat(resume): expose the human-confirmed
> tailored-artifact endpoints`
>
> 0010 Job Ingestion, the Architecture Re-Research, 0011 Job Search / Ranking,
> 0012 Steps 1-8 (freshness schema, source observation domain, Source Sync
> DAO/Service/API, freshness search, Python ATS adapters, end-to-end
> integration), 0013 AI Matching, and 0014 China Campus Sources + Manual Capture
> are complete and archived; the **0015A** job-list/intake hardening is closed.
> **0015 resume tailoring and explainable matching is the active stage**: Phases
> A-C are complete and Phase D is implemented through the D5 routes, which passed
> their L2 verification (commit `ae85868`; D6 deferred by owner decision), with
> the stage's single L4 pending (`## Active Plan (0015)`). 0014's Step 5 is
> environment-limited and carried forward (see the Completed Plan section and
> `## Next Entry` items 6/7). 0016 open-source packaging follows 0015.

Repository files and Git history are the cross-agent fact source. This concise
state plus the active ExecPlan should be enough for a new Agent to continue
without asking the user to replay project history.

This is the concise takeover entry. Detailed evidence and historical sections
are preserved in
[references/project-state-full.md](references/project-state-full.md).

## Current Stage

- Architecture direction is now open-source first and self-hosted, with a
  future path to SaaS only through separately approved decisions.
- P0 config, logging, and schema are complete.
- P1 network/HTTP remains behaviorally frozen after the minimum correctness
  gate, with one approved exception for `PUT`/`DELETE` request
  method parsing and routing.
- Quality infrastructure is complete: ASan, UBSan, TSan, presets, and baseline
  benchmarks.
- The first P2 user vertical slice is complete:
  `register -> login -> Redis session -> Authorization -> me`.
- The 0006 profile/skill vertical slice is implemented and verified in the
  committed baseline: authenticated profile read/update and skill
  list/create/upsert/delete.
- The 0007A infrastructure slice is implemented and verified in the committed
  baseline: bounded HTTP bodies, multipart parsing, MySQL
  transactions, local file storage, storage configuration, and shared session
  authentication.
- The Resume Vertical Slice is implemented, committed, and pushed:
  upload, paginated list, text version creation, and version listing.
- The Job Read Vertical Slice is committed and pushed: public active-job
  search, pagination, detail, and requirements.
- The Application Vertical Slice is implemented, committed, and pushed:
  authenticated create/list/status/statistics with Job and ResumeVersion
  validation, duplicate protection, and a strict state machine.
- The Job Ingestion Vertical Slice is implemented, committed, and pushed:
  internal token-protected batch import, `source_url` idempotent upsert,
  transactional requirements replacement, and Import -> Job Read ->
  Application integration coverage.
- The Job Search / Ranking Vertical Slice is implemented, committed, and
  pushed: salary/skill hard filters, authenticated `rule_v1`
  recommendations, deterministic score breakdown, stable pagination, and a
  bounded ranking candidate set.
- The 0012 Source Adapter + Freshness slice is implemented through Step 8 and
  pushed: additive freshness columns, Python Greenhouse/Lever/Ashby adapters,
  internal `POST /internal/jobs/source-sync` observations with completeness
  semantics, disappearance grace and reopen, and freshness-aware public search.
- The 0013 AI Matching slice is implemented, committed, and archived: additive
  lease/rule-baseline columns on `ai_tasks`, deterministic `match_rule_v1` in
  C++, public `POST /api/v1/ai/match` plus task polling, token-protected
  `/internal/ai/tasks/*`, and an optional Python `ai_worker/`.
- The 0014 China campus sources + manual capture slice is implemented through
  Step 8 with **Step 5 environment-limited**: F13 Beijing-time deadline
  semantics, F14 manual-record priority, the additive `jobs` migration (6
  nullable columns, 0 indexes), the Python capture framework, and the manual
  capture channel (bookmarklet + local publisher through
  `/internal/jobs/import`). No real China source adapter exists yet and no China
  `source` identifier is registered.
- **0015 (resume tailoring and explainable matching) is the active stage**, and
  **0015A (job-list usability and intake quality) is closed**; 0014's Step 5
  carry-forward is unchanged. Details: `## Active Plan (0015)` and
  `## Completed Plan (0015A)` below.

The revised architecture direction is:
[../architecture/open-source-mvp.md](../architecture/open-source-mvp.md).

Key decisions:

- Job Discovery, Search, and Crawler are separate concerns.
- Source Adapter -> Normalize/Deduplicate -> Job Ingestion replaces a general
  crawler.
- AI uses `Hard Filter -> Rule Score -> Embedding -> LLM`.
- AI Providers are BYOK and provider-neutral.
- RabbitMQ, Gateway, general Crawler, and AutoApply are not MVP requirements.
- Freshness lifecycle is introduced with 0012 Source Adapter, not 0011.
- A single missing fetch never closes a Job.

## Active Plan (0015)

[`active/0015-resume-tailoring-and-explainable-matching.md`](../exec-plans/active/0015-resume-tailoring-and-explainable-matching.md)
— Phases A-C complete, Phase D implemented through D5 (D6 deferred by owner
decision, plan §17); D5 passed its L2 verification (commit `ae85868`) and the
stage's single L4 is pending.

Delivered:

- Phase A: `profiles.education_level` / `graduation_year` / `campus_status` are
  nullable columns with no backfill (NULL = unknown); `test_schema` covers the
  live, fresh and pre-0015 migrated databases. Resume PDF text status is
  **derived** (`ok` / `text_empty` / `pdf_no_text` / `text_unavailable`) — no new
  column.
- Phase B: `rule_v1` weights unchanged; `match_explanation` adds deadline
  urgency, deal-breakers as data and reasons; 7 additive keys on `/recommended`
  and on the job list with `with_match=true`; the default payload is
  byte-identical; `strict=true` paginates in the shared ORDER BY window, `total`
  is the surviving count and a capped window is flagged `truncated`.
- Phase C: `tools/job_report/render_report.py` — stdlib-only offline
  self-contained HTML report with 11 deterministic tests.
- Phase D: D0/D1 contracts (`docs/03` §10.6, `docs/04` §9); D2 `tailor_rule_v1`
  deterministic draft; D3 `resume_artifacts` table + DAO + schema tests; D4
  mechanical fact-provenance gate (`fact_check_status=2` blocks confirmation);
  D5 four authenticated routes — `POST`/`GET /api/v1/resumes/artifacts`,
  `GET /api/v1/resumes/artifacts/{id}`,
  `POST /api/v1/resumes/artifacts/{id}/confirm` — with draft
  (`confirmed_at IS NULL`) vs confirmed distinguishable, **409** on a failed fact
  gate or an already-confirmed artifact, `/submit` / `/apply` / `/send` probes
  → 404, and **no submission endpoint anywhere**
  (`tools/checks/no_submission_path.py`). `IResumeDao` also
  gained the `FindOwnedVersion` seam used by draft generation.
  D5's L2 evidence (commit `ae85868`, pushed): release preset build rc=0;
  selected ctest 30/30 (`test_resume_http` 5/5, `test_resume_artifact_dao` 4/4,
  `test_resume_dao` 2/2, `test_resume_service` 6/6, `test_ai_task_service` 13/13);
  `tools/checks/no_submission_path.py` rc=0; `git diff --check` rc=0.

The stage's single L4 has not run yet; no stage-level test totals exist (the D5
figures above are L2 scope only).

## Completed Plan (0015A)

[`active/0015A-job-list-usability-and-intake-quality.md`](../exec-plans/active/0015A-job-list-usability-and-intake-quality.md)
— **closed 2026-10-10** (the plan file is still under `active/`; see
`## Next Entry` item 9).

Progress:

```text
Steps 0-8 complete; the stage's single L4 passed
L4: Debug / ASan / UBSan 286/286 each; both Release builds ok; Python 446 and 190;
    sanitizers clean; backfill isolation: dev database explicitly SKIP, scratch 7/7
```

- Public job search defaults to `clean` only (`AND j.review_status = 0`);
  `needs_review` rows are reachable only through the internal token-protected
  review surface (`GET /internal/jobs/review-queue`,
  `POST /internal/jobs/review/{id}`, `POST /internal/jobs/review/backfill`).
- The list payload carries `source_url`; `education_max` / `job_nature` /
  `deadline_before` / `deadline_after` filters and `sort` (`created_desc` /
  `deadline_asc` / `education_asc`) are live.
- Description length is capped at 48 KiB of UTF-8 bytes; over-long input is
  rejected or routed to review with diagnostics, never silently truncated.

Recorded database state (observation, not a defect, no cause asserted): `jobs`
has 4 rows, all `review_status=0`, and `job_review_items` has 0 rows; a
historical dry run once saw 6 active jobs with no baseline snapshot, and the
cause is unproven.

## Completed Plan (0014)

[`completed/0014-china-campus-sources-and-manual-capture.md`](../exec-plans/completed/0014-china-campus-sources-and-manual-capture.md)
— archived 2026-10-09; the plan is frozen as historical evidence.

Progress:

```text
Steps 1-8 executed (Step 2/3 delivered except source registration; Step 5
environment-limited and CARRIED FORWARD)
Step 5: environment-limited (no compliant list contract verified) — accepted
Step 8: L4 passed once (2026-10-09): Debug / ASan / UBSan 254/254 each,
        service + root Release builds ok, Python 337 and 190
```

Delivered:

- Step 1/4: `jobs` additive fields + contracts (`docs/03` §9, `docs/04` §5.1/§5.2/
  §5.4/§5.5), both JSON parsers, both validators, both upserts, read-back and
  response mapping; existing-database ALTER executed against a pre-0014 database
  and re-verified with the schema suite.
- Step 2/3: source register + offline fixtures, and the Python China framework
  (robots per-path fail-closed, retry/backoff, charset precedence, HTML/JSON-LD/
  RSS/sitemap parsers, SSRF guard, size/request/page budgets, salary rules, F13).
- Step 6: manual capture (credential-free bookmarklet + local publisher reusing
  `POST /internal/jobs/import` with `source="manual"`; F14 proven end-to-end).
- Step 7: documentation sync (root README, this file, the plan, agent-workflow §8).
- Step 8: the stage's single L4 — Debug/ASan/UBSan 254/254 each, service and root
  Release builds ok, Python `source_adapters` 337/337 and `ai_worker` 190/190 (plan
  §21.2).

**Carried forward (incomplete): Step 5.** The first real China source could not be
verified compliantly — 国聘's job list requires its client-side request signature
(anti-automation risk control) and 清华's job-list host
`career.cic.tsinghua.edu.cn` returns a persistent `robots.txt` HTTP 503
(fail-closed). No China adapter is implemented and the four-place allowlist is
unchanged; the registered sources remain `greenhouse` / `lever` / `ashby`. This
stays visible in `## Next Entry` items 6/7. Evidence:
[`../../source_adapters/README.md`](../../source_adapters/README.md) source
register and
[`../../source_adapters/jobpilot_sources/tests/fixtures/cn_source_probe_2026-10-09.json`](../../source_adapters/jobpilot_sources/tests/fixtures/cn_source_probe_2026-10-09.json).

Remaining work / next steps:

1. Step 5 re-entry: only with a newly verifiable candidate (written permission or
   a documented public API for 国聘; a re-probe once
   `career.cic.tsinghua.edu.cn/robots.txt` stops returning 503; or a different
   server-rendered, unsigned school list — the last needs the product owner's
   authorization to widen the candidate set).
2. The real-browser bookmarklet acceptance remains **UNVERIFIED** (plan §19.5
   template); it is owned by the product owner and is not a stage blocker.
3. Stage **0015** (resume tailoring) is next, then **0016** (open-source packaging).

Note: `## Next Entry` items 1-5 and the stage-numbering table below were written on
2026-10-08 as the recorded stage-numbering decision and are preserved verbatim
(items 6/7 are the 0014 carry-forward added when 0014 closed; items 8/9 record
the 0015 opening); the current status is this section.

## Stage Handoff Map

| Stage | ExecPlan evidence | Primary implementation/tests | Primary commits |
|---|---|---|---|
| P0 Config / Logging / Schema | `completed/0002-p0-config-logging-schema.md` | `backend/src/core`, `database/schema`, backend config/logger/schema tests | `1d3bcbd`, `2c2f344`, `682e26d` |
| Quality / Benchmark | `completed/0003-quality-and-performance-baseline.md` | CMake presets, backend benchmarks | `baefe55` |
| P1 Correctness Gate | `completed/0004-p1-correctness-gate.md` | `backend/src/network`, `backend/src/http`, P1 tests | `2f54cb6`, `2655e05` and preceding P1 fixes |
| User / Auth | `completed/0005-p2-user-vertical-slice.md` | `service/{include,src}/jobpilot/service/user`, `service/tests/test_user_*` | `8e501ce`, `6ed94cb`, `70051c7`, `889e1e7` |
| Profile / Skill | `completed/0006-p2-profile-skill-vertical-slice.md` | User profile/skill DAO/Service/Controller and tests | `73b1172` |
| Resume Infrastructure | `completed/0007A-resume-infrastructure.md` | HTTP body/multipart, Transaction, FileStorage, SessionAuthenticator | `5d145d7`, `516837f`, `dd7cc78`, `638d73f`, `ecf9b68`, `90f52c9` |
| Resume | `completed/0007-p2-resume-vertical-slice.md` | `service/{include,src}/jobpilot/service/resume`, resume tests | `644c7b0`, `1b95602` |
| Job Read | `completed/0008-p2-job-read-vertical-slice.md` | `service/{include,src}/jobpilot/service/job`, job read tests | `3c027b1`, `5f49551` |
| Application | `completed/0009-p2-application-vertical-slice.md` | `service/{include,src}/jobpilot/service/application`, application tests | `2bebcfe`, `a1074d5` |
| Job Ingestion | `completed/0010-p2-job-ingestion-vertical-slice.md` | Job ingestion DAO/Service/Controller, `/internal/jobs/import`, ingestion tests | `9e87686` |
| Architecture Re-Research | `../architecture/open-source-mvp.md`, `../adr/README.md` | ADR-0007 through ADR-0010 | `635fc31` |
| Job Search / Ranking | `completed/0011-job-search-ranking.md` | Job ranking/search modules and tests | `214dc9e` |

## Completed Plan (0013)

[0013-ai-matching.md](../exec-plans/completed/0013-ai-matching.md)

Progress:

```text
Steps 1-10 complete
L4: Debug / ASan / UBSan / Release 230/230 each; Python 85 and 190
```

0013 scope: `job_match` end to end — additive owner/lease/rule-baseline columns
on `ai_tasks`, deterministic `match_rule_v1` scoring in C++, public
`POST /api/v1/ai/match` plus `GET /api/v1/ai/tasks/{id}`, token-protected
`/internal/ai/tasks/{claim,complete,fail,input}`, and an optional Python
`ai_worker/` that adds embedding/LLM explanation without ever replacing the
deterministic score. `resume_optimize` / `interview_gen` stay reserved for 0015 (the
stage-numbering decision in `## Next Entry` renumbered resume tailoring from 0014
to 0015).

## Completed Plan (0012)

[../exec-plans/completed/0012-job-source-adapter-freshness.md](../exec-plans/completed/0012-job-source-adapter-freshness.md)

Progress:

```text
Steps 1-10 complete
L4 verified: Debug / ASan / UBSan 172/172 each, Release builds passed
```

0012 scope:

- Manual Greenhouse / Lever / Ashby source configuration.
- Python source adapters publishing through C++ internal APIs.
- Source Adapter observations use `POST /internal/jobs/source-sync`
  (`complete=true` accepts `0..200`, `complete=false` accepts `1..200`); manual
  and dataset loads keep using `POST /internal/jobs/import` (`1..100`).
- Complete observation and source-sync semantics.
- Additive freshness fields and safe disappearance handling.
- Fresh / aging / stale / closed search behavior.

0012 non-goals include canonical multi-source Job identity, RabbitMQ, browser
crawler, AI freshness authority, AutoApply, and schema changes outside the
additive freshness fields.

Stop and return to design if complete board coverage cannot be proven, the
provider requires authentication, adapters need direct MySQL access, or a
larger schema redesign becomes necessary.

## Architecture Summary

```text
HTTP request
  -> HttpServer / HttpRouter
  -> UserController
  -> UserService
  -> UserDao / SessionManager
  -> MySQL ConnectionPool / RedisClient
```

- `service/` is the canonical runnable build entry; root is the unified
  developer entry.
- `jobpilot_core` owns network/HTTP, config/logging, database, and cache
  primitives.
- `jobpilot_service_core` owns routing plus User, Resume, Job, Application,
  and Job Ingestion modules.
- P1 ownership rules remain mandatory for changes under
  `backend/{include,src}/jobpilot/{network,http}/`.

## Verified Snapshot

- Pre-0006 clean `service/` and root CTest: 54/54.
- Full Debug CTest after 0011: 115/115.
- ASan full CTest after 0011: 115/115.
- UBSan full CTest after 0011: 115/115.
- TSan default gate after 0010: 42/42.
- TSan external-service subset: 6/6.
- Focused Release regression after the `PUT`/`DELETE` method extension: 9/9.
- Both `service/` and root Release builds completed successfully.
- 0012 L4: full Debug, ASan, and UBSan CTest 172/172 each (0 failures, no
  sanitizer diagnostics); `service/` and root Release builds passed; Python
  adapter suite 85/85; `ctest -L job` 84/84.
- 0014 L4 (2026-10-09, one run, service presets, sequential phases): full Debug,
  ASan and UBSan CTest **254/254 each** (0 failures, no sanitizer diagnostics, no
  `LastTestsFailed.log` in any preset); `service/` Release built and
  `jobpilot_service` links cleanly; root Release built; Python `source_adapters`
  **337/337** and `ai_worker` **190/190**. Raw logs and per-phase timestamps are
  recorded in the 0014 plan §21.
- 0015A L4 gate: full Debug, ASan and UBSan CTest **286/286 each** (sanitizers
  clean); both `service/` and root Release builds passed; Python
  `source_adapters` **446** and `ai_worker` **190**; the review-backfill
  isolation ran with the dev database explicitly SKIPped and the scratch database
  7/7.
- 0015 stage L4: **not run yet** — the stage is active and its single L4 runs at
  close, so no stage-level test totals exist in this snapshot.
- Opt-in live smoke (not part of CI): Greenhouse 217 jobs, Lever 10, Ashby 160,
  all published with `complete=false` only where the 200-job limit required it.
- Real HTTP register/login/me and profile/skill ownership flows passed; test
  data was cleaned afterward.

Quality commands and recorded results:
[../quality/README.md](../quality/README.md).

Performance baseline:
[../benchmarks/README.md](../benchmarks/README.md).

## Current Risks

| Priority | Risk | Boundary |
|---|---|---|
| High | No HTTP request-header limit | Body is capped at 12 MiB; header exhaustion remains open |
| High | Raw `Channel *` in epoll active list | Requires strict channel lifetime discipline |
| Medium | No graceful shutdown/signal handling | Service cannot stop cleanly on signals |
| Medium | Expired vs invalid Redis token cannot be distinguished | Both currently return `20001`; `20002` is reserved |
| Medium | P1 HTTP methods remain a minimal subset | Further methods require a new reviewed change |
| Medium | Live MySQL/Redis required by integration tests | CTest is not self-contained |
| Medium | Internal import token is application-layer only | Gateway network isolation is still pending |
| Medium | Source adapters publish no salary | Lever/Ashby units do not map to the monthly-thousands column contract |
| Medium | Adapters need a running C++ service | The CLI publishes over HTTP; there is no queue or scheduler yet |
| Medium | Lever/Ashby company comes from configuration | Those APIs return no company name, so `company` falls back to config or the board token |
| Medium | AI provider abstraction is not implemented yet | Core must remain functional without AI |
| Medium | Ranking candidate set is capped at 2000 | Results expose `truncated=true` |
| Low | No minimal end-user UI yet | Open-source demo remains API/CLI-first |

## Next Entry

1. Review the archived 0013 plan and its L4 evidence
   (`completed/0013-ai-matching.md`); 0013 is complete and archived.
2. The China-first source layer decided in
   `adr/0011-china-first-sourcing-and-compliance.md` (public sources plus
   manual capture import, campus focus, company career pages) is planned as
   stage **0014**: its active ExecPlan is authored and implementation has not
   started. The three overseas adapters from 0012 become optional sources.
3. `resume_optimize` and `interview_gen` remain contract-only; the Chinese
   resume/ATS tailoring work is stage **0015**.
4. Follow stage **0016** (open-source packaging) after that.
5. Keep AutoApply, RabbitMQ, general Crawler, Gateway, and SaaS RBAC out of
   the MVP unless a separate ADR changes the decision.
6. **0014 carry-forward — Step 5 (first real China source) is environment-limited
   and NOT done.** It can only complete with a compliant, verifiable candidate:
   written permission or a documented public API for 国聘; a re-probe once
   `career.cic.tsinghua.edu.cn/robots.txt` stops returning 503; or a different
   server-rendered, unsigned school job list that needs the product owner's
   authorization. Until then **no China source adapter is registered** (only
   `greenhouse`/`lever`/`ashby`).
7. **0014 carry-forward — the real-browser bookmarklet acceptance is still
   UNVERIFIED.** The plan's §19.5 holds the redacted-evidence template; this is
   owned by the product owner and is not a stage blocker.
8. **0015 is active.** Phases A-C are complete and Phase D is implemented through
   D5, whose L2 verification passed (commit `ae85868`); only D7 — this docs sync
   plus the stage's single L4 — remains. The plan's D7 checkbox stays open until
   the L4 evidence is recorded.
9. **0015A is closed but its plan file still sits under `active/`** with its own
   status header and §13 checkboxes unupdated; updating them and archiving the
   file is the Lead's call (plan §5.5 assigns that file to the Lead).

Stage numbering (decided 2026-10-08). It supersedes older references inside the
archived completed plans, which are historical evidence and are never rewritten:

| Stage | Scope | Plan |
|---|---|---|
| 0014 | China campus sources and manual capture | [`completed/0014-china-campus-sources-and-manual-capture.md`](../exec-plans/completed/0014-china-campus-sources-and-manual-capture.md) |
| 0015 | Resume tailoring (Chinese resume and ATS) | not started; labeled 0014 by `completed/0013-ai-matching.md` sections 3/14/17 |
| 0016 | Open-source packaging | not started |

Note added 2026-10-09 when 0014 closed: the 0014 row above records the
2026-10-08 decision and is preserved verbatim; the archived plan now lives at
[`completed/0014-china-campus-sources-and-manual-capture.md`](../exec-plans/completed/0014-china-campus-sources-and-manual-capture.md).

Note added 2026-10-10 when 0015 opened: **0015 is now active** (Phases A-C
complete, Phase D implemented through D5 — see `## Active Plan (0015)`), and
**0015A is closed**; the table rows above are preserved verbatim.

0011 must remain:

```text
MySQL
Hard Filter
Rule Ranking
no LLM
no Embedding
no freshness lifecycle
no Search Engine
no RabbitMQ
```

Do not start a new P2 module without its own approved plan.

## Key Entry Points

- AI routing: [README.md](README.md)
- Active ExecPlan (0015):
  [../exec-plans/active/0015-resume-tailoring-and-explainable-matching.md](../exec-plans/active/0015-resume-tailoring-and-explainable-matching.md)
- Full historical state:
  [references/project-state-full.md](references/project-state-full.md)
- Current architecture:
  [../architecture/open-source-mvp.md](../architecture/open-source-mvp.md)
- Latest completed ExecPlan:
  [../exec-plans/completed/0013-ai-matching.md](../exec-plans/completed/0013-ai-matching.md)
- Competitive research note:
  [../research/2026-10-08-open-source-job-search-agents.md](../research/2026-10-08-open-source-job-search-agents.md)
- Product direction decision:
  [../adr/0011-china-first-sourcing-and-compliance.md](../adr/0011-china-first-sourcing-and-compliance.md)
- Latest completed ExecPlan:
  [../exec-plans/completed/0012-job-source-adapter-freshness.md](../exec-plans/completed/0012-job-source-adapter-freshness.md)
- Current ADR decisions: [../adr/README.md](../adr/README.md)
- Coding and testing rules:
  [../06-coding-standard.md](../06-coding-standard.md)
- API contract: [../04-api-design.md](../04-api-design.md)
- Database contract: [../03-database-design.md](../03-database-design.md)
- GitHub and tooling research:
  [references/github_resources.md](references/github_resources.md)

`docs/00-project-overview.md`, `docs/01-requirements.md`,
`docs/02-system-architecture.md`, `docs/05-development-plan.md`,
`docs/07-deployment.md`, `docs/08-execution-guide.md`, and
`docs/project-structure.md` remain historical design material, each marked
with a Superseded banner. Where they conflict with ADR-0007 through ADR-0010,
the current architecture document and accepted ADRs take precedence.
