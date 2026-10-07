# JobPilotAI Current Project State

> Snapshot: 2026-10-08
>
> Baseline HEAD: `e3a640a test(job): add end-to-end source sync and freshness
> coverage`
>
> `origin/main` matches this baseline. 0010 Job Ingestion, the Architecture
> Re-Research, 0011 Job Search / Ranking, and 0012 Steps 1-8 (freshness schema,
> source observation domain, Source Sync DAO/Service/API, freshness search,
> Python ATS adapters, end-to-end integration) are committed and pushed. The
> active plan is still 0012 Job Source Adapter + Freshness.

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
  Step 9 (documentation) and Step 10 (L4 verification) remain.
- AI Task remains outside completed slices.

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

## Active Plan

[0012-job-source-adapter-freshness.md](../exec-plans/active/0012-job-source-adapter-freshness.md)

Current step:

```text
Research / Capability Check / Design / Plan complete
Steps 1-8 implemented, committed, and pushed
Step 9 (documentation) and Step 10 (L4 verification) remain
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
- 0012 Steps 1-8 focused evidence: `SchemaTest` 8/8, source observation domain
  26/26, Source Sync DAO 12/12, Source Sync Service 6/6, source-sync HTTP 5/5,
  source sync E2E 1/1, `ctest -L job` 84/84, Python adapter suite 81/81.
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

1. Execute `active/0012-job-source-adapter-freshness.md`.
2. Do not start 0013 until 0012 is complete and reviewed.
3. Follow 0013 AI Matching, 0014 Resume Tailoring, and 0015 Open-source
   Packaging.
4. Keep AutoApply, RabbitMQ, general Crawler, Gateway, and SaaS RBAC out of
   the MVP unless a separate ADR changes the decision.

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
- Full historical state:
  [references/project-state-full.md](references/project-state-full.md)
- Current architecture:
  [../architecture/open-source-mvp.md](../architecture/open-source-mvp.md)
- Active ExecPlan:
  [../exec-plans/active/0012-job-source-adapter-freshness.md](../exec-plans/active/0012-job-source-adapter-freshness.md)
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
