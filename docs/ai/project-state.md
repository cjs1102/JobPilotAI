# JobPilotAI Current Project State

> Snapshot: 2026-10-08
>
> Baseline HEAD: `a1074d5 docs(application): record application vertical slice baseline`
>
> `origin/main` matches this baseline. The 0010 Job Ingestion and 0011 Job
> Search / Ranking Vertical Slices are implemented and verified in the
> working tree but are not committed yet.

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
- The Job Read Vertical Slice is committed locally: public active-job search,
  pagination, detail, and requirements.
- The Application Vertical Slice is implemented, committed, and pushed:
  authenticated create/list/status/statistics with Job and ResumeVersion
  validation, duplicate protection, and a strict state machine.
- The Job Ingestion Vertical Slice is implemented and verified in the working
  tree: internal token-protected batch import, `source_url` idempotent upsert,
  transactional requirements replacement, and Import -> Job Read ->
  Application integration coverage.
- The Job Search / Ranking Vertical Slice is implemented and verified in the
  working tree: salary/skill hard filters, authenticated `rule_v1`
  recommendations, deterministic score breakdown, stable pagination, and a
  bounded ranking candidate set.
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
| Medium | No source adapter runner yet | ATS discovery is designed but not implemented |
| Medium | AI provider abstraction is not implemented yet | Core must remain functional without AI |
| Medium | Ranking candidate set is capped at 2000 | Results expose `truncated=true` |
| Low | No minimal end-user UI yet | Open-source demo remains API/CLI-first |

## Next Entry

1. Review and commit the completed 0010 and 0011 Vertical Slices.
2. Continue with 0012 Source Adapter + Freshness.
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
- Current ADR decisions: [../adr/README.md](../adr/README.md)
- Coding and testing rules:
  [../06-coding-standard.md](../06-coding-standard.md)
- API contract: [../04-api-design.md](../04-api-design.md)
- Database contract: [../03-database-design.md](../03-database-design.md)
- GitHub and tooling research:
  [references/github_resources.md](references/github_resources.md)

`docs/02-system-architecture.md` and `docs/08-execution-guide.md` remain
historical design material. Where they conflict with ADR-0007 through
ADR-0010, the current architecture document and accepted ADRs take precedence.
