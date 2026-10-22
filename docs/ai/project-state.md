# JobPilotAI Current Project State

> Snapshot: **2026-10-23**. `main = c20795d`; the repository is public and
> `v0.1.0` is tagged. Stages 0001–0028 are complete. **No code plan is active**;
> the current change is the docs-only status update (0029).

This file is the concise live state. Stage bodies and long evidence are in Git
history; the stage index is [`../exec-plans/README.md`](../exec-plans/README.md).
Repository files and Git history are authoritative over chat history.

## Recent stage closeout (2026-10-22/23)

### 0027 — repository hygiene and README

Merged in PR #12 at `dac3490`. Removed 37 files: the obsolete deployment and
execution guides, the handoff/current-state copies, the archived next-phase
plan and full-state copy, 27 completed plan bodies, and four internal AI-skill
files. `docs/` dropped from about 1.9 MB to 760 KB. The English README was
rewritten and `README.zh-CN.md` mirrored its structure; the docs index, map, and
`AGENTS.md` references were updated.

**Deviation from the supervisor instruction (evidence-backed):**
`docs/quality/test-merge-map.json` and `docs/quality/test-overlap-audit.md` were
kept (the report reduced to a short summary). The CI `Coverage map guard` reads
both files; deleting them would fail `full test suite`. The 0028 first CI run
demonstrated exactly that failure. `docs/exec-plans/README.md` was retained as
the stage index.

### 0028 — usability defect fixes

Merged in PR #13 at `c20795d` (head `621437a`). Four defects fixed:

- **New-user recommendations**: no profile/skills now returns `200` with an
  empty page (was `10001`), echoing `page`/`page_size`.
- **Duplicate skill rows**: the job card keeps the `reasons` list and no longer
  renders `matched_skills`/`missing_skills` twice; optional Firefox DOM
  assertion covers it.
- **Non-UTF-8 input**: a strict UTF-8 validator rejects multipart title/filename,
  txt/md bodies, and public JSON writes with `400/10001` before any write.
  Internal import/source-sync/review endpoints were intentionally left out.
- **Field-aware parameter errors**: `https://`-safe additive `data.field` for
  resume `title`/`file`, application `resume_version_id`, and AI `resume_id`;
  `code`/`message`/status unchanged.

Tests moved **312 → 321**. `docs/quality/test-merge-map.json` counts and the
renamed ranking case were updated. Main CI after the merge: `ci`
`39814999026` (11m55s; build 6m37s + suite 1m18s) and `image` `39814999046`
(12m08s; container gate 1m54s), both green. PR CI on `621437a` was also green
(`ci` `39811899272`, `image` `39811899278`).

**Lesson:** any test addition or rename must include
`python3 tools/checks/test_coverage_map.py --test-dir build/presets/<preset>
--map docs/quality/test-merge-map.json`; the first 0028 CI run failed only on
this guard.

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

- **321 C++ tests**; the full Debug suite completed `321/321` with zero
  failures and zero sanitizer diagnostics (one review-backfill test is skipped
  on the developer database and passes on the isolated scratch database). The
  last full ASan/UBSan matrix was `312/312` before the nine 0028 tests; the 14
  affected 0028 tests pass under both sanitizers.
- **TSan**: 52/52 harness tests and 17/17 external-runtime tests, zero
  diagnostics.
- **Python**: 481 source-adapter tests and 190 AI-worker tests pass in the
  pinned environment.
- **Benchmark** (4 vCPU VMware, Release): **13,421.82 req/s** at 2 threads / 32
  connections, p99 **7.17 ms**; this is a dated, host-specific measurement.
- **Latest main CI** (`c20795d`): `ci` `39814999026` completed/success
  (11m55s; build 6m37s + suite 1m18s); `image` `39814999046`
  completed/success (12m08s; container gate 1m54s).
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
- Any test addition/rename must run the coverage-map guard before pushing.

## Carried forward

- UTF-8 boundary validation for internal import/source-sync/review endpoints
  (intentionally out of 0028).
- `data.field` adoption in the remaining parameter-error paths.
- `tools/dev/demo-up.sh`: when Docker Hub is unreachable, check for a local
  nginx image first or bound the pull attempt (currently about a two-minute
  wait before the Python fallback).
- The browser DOM duplicate assertion stays in the optional Firefox smoke and
  is not a CI dependency.

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
