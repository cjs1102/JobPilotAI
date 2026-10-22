# JobPilotAI Current Project State

> Snapshot: **2026-10-22**. `main = 3b4c60b`; the repository is public and
> `v0.1.0` is tagged. Stages 0001–0026 are complete. **No code plan is active**;
> the current change is the docs-only repository-hygiene PR (0027).

This file is the concise live state. Stage bodies and long evidence are in Git
history; the stage index is [`../exec-plans/README.md`](../exec-plans/README.md).
Repository files and Git history are authoritative over chat history.

## Current capabilities

- **C++17 service** (`service/`) with a Controller → Service → DAO layout and a
  small core library in `backend/` (`jobpilot_core`: network/HTTP, config,
  logging, MySQL, Redis).
- **Users and sessions**: register, login, Redis-backed sessions, `me`, profile,
  and skills.
- **Resumes**: upload/list, text versions, and deterministic tailoring drafts
  with a fact-check gate.
- **Jobs**: public search/detail/requirements, filters, pagination,
  deterministic `rule_v1` ranking and recommendations, and explainable scores.
- **Sources**: Greenhouse, Lever, Ashby, and nowcoder; manual capture and JSON
  import; freshness and an intake-quality review gate.
- **Applications**: favourites, status state machine, statistics, and a local
  status board.
- **Web UI**: native HTML/CSS/JS, same-origin `/api`/`/health` proxy, nginx or
  the stdlib Python fallback.
- **Optional AI worker**: task-based assistance; the deterministic path does not
  need a provider key.

The schema has **13 tables**; the router has **36 routes** (27 non-internal and 9
internal). `docs/03` and `docs/04` are the contracts.

## Known limits and non-goals

- **China source coverage is narrow**: nowcoder is the only registered China
  campus source; it reads at most 20 rows per query, has no pagination, and
  always reports `complete=false`.
- **Some postings have no apply URL**: Greenhouse and nowcoder aggregated rows do
  not expose one; the UI falls back to the original announcement. Moka is not
  registered or verified.
- **No login-wall bypass, auto-login, CAPTCHA bypass, cookie/UA spoofing, or
  proxy pool**; login-walled pages use manual capture only.
- **No automatic application**: no auto-submit, form filling, notifications,
  general crawler, message queue, gateway, or SaaS RBAC.
- **The Web UI is a local demo**: HTTP and browser `localStorage`, not production
  authentication or TLS.
- **AI provider path is unverified**; the deterministic `rule_v1` path is the
  supported one. TSan covers only its dedicated suites, not the full matrix.
- **Benchmark numbers are host-specific**, not service-level objectives.
- **Demo launcher delay**: when Docker Hub is unreachable, `docker compose
  --profile web up` can wait about two minutes for `nginx:1.27-alpine` before the
  Python fallback starts. A separate small fix should check the local image first
  and bound the pull attempt; the script is unchanged in 0027.

## Verification baseline

- **312 C++ tests**; Debug, ASan, and UBSan each completed `312/312` with zero
  failures and zero sanitizer diagnostics; one review-backfill test is skipped
  on the developer database and passes on the isolated scratch database.
- **TSan**: 52/52 harness tests and 17/17 external-runtime tests, zero
  diagnostics.
- **Python**: 481 source-adapter tests and 190 AI-worker tests pass in the
  pinned environment.
- **Benchmark** (4 vCPU VMware, Release): **13,421.82 req/s** at 2 threads / 32
  connections, p99 **7.17 ms**; this is a dated, host-specific measurement.
- **Latest main CI** (`3b4c60b`): `ci` `39625131378` completed/success (4m22s;
  build 2m40s + suite 1m35s); `image` `39625131650` completed/success (2m11s;
  container gate 2m07s).
- **License**: `LICENSE` is the standard MIT text; the public GitHub license API
  returns `spdx_id=MIT` (V9 closed).

## Working rules

- Do not change `backend/{include,src}/jobpilot/{network,http}/` without an
  explicitly approved correctness plan; P1 network/HTTP is frozen.
- Keep business logic out of the network layer and preserve Controller → Service
  → DAO dependencies.
- Never log or commit passwords, tokens, `.env` values, or local credentials.
- Use parameterized SQL, structured JSON, documented errors, RAII, and explicit
  ownership for descriptors, callbacks, and locks.
- Work on a feature/docs branch; the worker does not push `main`. The owner
  merges the PR.
- Verify at the smallest sufficient level from
  [`../quality/README.md`](../quality/README.md); do not rerun an already-green
  full matrix unless relevant files or the environment changed.
- Update this file when a stage closes; do not reconstruct long stage evidence
  here.

## Reading order

1. [`../../AGENTS.md`](../../AGENTS.md)
2. [`README.md`](README.md) (AI context router)
3. This file
4. [`../exec-plans/README.md`](../exec-plans/README.md) (stage index)
5. [`../architecture/open-source-mvp.md`](../architecture/open-source-mvp.md),
   [`../adr/README.md`](../adr/README.md), and the relevant contract in
   [`../03-database-design.md`](../03-database-design.md) or
   [`../04-api-design.md`](../04-api-design.md)
6. [`../quality/README.md`](../quality/README.md) before running tests
7. [`../guides/local-demo.md`](../guides/local-demo.md) for the local UI

The public documentation map is [`../README.md`](../README.md); the full document
inventory is [`../PROJECT_DOCUMENTATION_INDEX.md`](../PROJECT_DOCUMENTATION_INDEX.md).
