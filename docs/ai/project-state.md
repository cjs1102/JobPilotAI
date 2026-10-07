# JobPilotAI Current Project State

> Snapshot: 2026-10-07
>
> Baseline HEAD: `e6547b7 docs(state): record completed user vertical slice`
>
> `origin/main` matched the baseline before the context-optimization changes.

This is the concise takeover entry. Detailed evidence and historical sections
are preserved in
[references/project-state-full.md](references/project-state-full.md).

## Current Stage

- P0 config, logging, and schema are complete.
- P1 network/HTTP is behaviorally frozen after the minimum correctness gate.
- Quality infrastructure is complete: ASan, UBSan, TSan, presets, and baseline
  benchmarks.
- The first P2 user vertical slice is complete:
  `register -> login -> Redis session -> Authorization -> me`.
- Work continues in P2. Resume, job, application, profile, and skill domains
  remain outside the completed slice.

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

- Clean `service/` and root CTest: 54/54.
- ASan full CTest: 54/54.
- UBSan full CTest: 54/54.
- TSan concurrency/User subset: 17/17.
- TSan external-service subset: 6/6.
- Real HTTP register/login/me flow passed; test data was cleaned afterward.

Quality commands and recorded results:
[../quality/README.md](../quality/README.md).

Performance baseline:
[../benchmarks/README.md](../benchmarks/README.md).

## Current Risks

| Priority | Risk | Boundary |
|---|---|---|
| High | No HTTP request/header/body limits | Resource-exhaustion hardening remains open |
| High | Raw `Channel *` in epoll active list | Requires strict channel lifetime discipline |
| Medium | No graceful shutdown/signal handling | Service cannot stop cleanly on signals |
| Medium | Expired vs invalid Redis token cannot be distinguished | Both currently return `20001`; `20002` is reserved |
| Medium | Profile and skill endpoints are not implemented | User domain is auth-only |
| Medium | Live MySQL/Redis required by integration tests | CTest is not self-contained |

## Next Entry

1. Plan the profile and skill fragment of the user domain.
2. Continue P2 with resume, job, and application modules.
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
