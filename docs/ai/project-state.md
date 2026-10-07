# JobPilotAI Current Project State

> Snapshot: 2026-10-07
>
> Baseline HEAD: `90f52c9 docs(ai): record 0007A infrastructure baseline`
>
> `origin/main` matches this baseline. The 0007 Resume Vertical Slice is
> implemented and verified in the working tree but is not committed yet.

This is the concise takeover entry. Detailed evidence and historical sections
are preserved in
[references/project-state-full.md](references/project-state-full.md).

## Current Stage

- P0 config, logging, and schema are complete.
- P1 network/HTTP remains behaviorally frozen after the minimum correctness
  gate, with one approved working-tree exception for `PUT`/`DELETE` request
  method parsing and routing.
- Quality infrastructure is complete: ASan, UBSan, TSan, presets, and baseline
  benchmarks.
- The first P2 user vertical slice is complete:
  `register -> login -> Redis session -> Authorization -> me`.
- The 0006 profile/skill vertical slice is implemented and verified in the
  committed baseline: authenticated profile read/update and skill
  list/create/upsert/delete.
- The 0007A infrastructure slice is implemented and verified in the working
  committed baseline: bounded HTTP bodies, multipart parsing, MySQL
  transactions, local file storage, storage configuration, and shared session
  authentication.
- The Resume Vertical Slice is implemented and verified in the working tree:
  upload, paginated list, text version creation, and version listing.
- Job and application domains remain outside completed slices.

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
- `jobpilot_service_core` owns routing and User DAO/Service/Controller.
- P1 ownership rules remain mandatory for changes under
  `backend/{include,src}/jobpilot/{network,http}/`.

## Verified Snapshot

- Pre-0006 clean `service/` and root CTest: 54/54.
- Full Debug CTest after 0007: 76/76.
- ASan full CTest after 0007: 76/76.
- UBSan full CTest after 0007: 76/76.
- TSan default gate after 0007: 32/32.
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

## Next Entry

1. Review and commit the completed 0007 Resume Vertical Slice.
2. Continue P2 with job and application modules.
3. Keep all new business work separate from the frozen P1 network layer.

Do not start a new P2 module without its own approved plan.

## Key Entry Points

- AI routing: [README.md](README.md)
- Full historical state:
  [references/project-state-full.md](references/project-state-full.md)
- Canonical execution order: [../08-execution-guide.md](../08-execution-guide.md)
- Architecture and boundaries:
  [../02-system-architecture.md](../02-system-architecture.md)
- Coding and testing rules:
  [../06-coding-standard.md](../06-coding-standard.md)
- API contract: [../04-api-design.md](../04-api-design.md)
- Database contract: [../03-database-design.md](../03-database-design.md)
- GitHub and tooling research:
  [references/github_resources.md](references/github_resources.md)
