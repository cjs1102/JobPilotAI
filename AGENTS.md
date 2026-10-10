# JobPilotAI Project Guide

## Project

C++17 Linux backend for an AI-assisted job application platform. `service/` is
the canonical runnable entry; root CMake is the unified developer entry.

Current phase: **0017 ops hardening and acceptance closure is complete and
archived** (`docs/exec-plans/completed/0017-ops-hardening-and-acceptance-closure.md`)
— CI now compiles the tree once (the `build` job uploads the release build tree
and `integration` consumes it; the container job moved to
`.github/workflows/image.yml` with path/branch filtering), the 309-case suite was
audited by assertion target and proved to contain **no removable case** (k = 0,
machine-checked by `tools/checks/test_coverage_map.py` and enforced in CI),
`tools/dev/capture-acceptance.sh` runs the bookmarklet publish/read-back
acceptance in one command and prints the ExecPlan 0014 §19.5 evidence block, and
`docs/RELEASE_CHECKLIST.md` + `tools/checks/release_preflight.py` make the release
gate checkable. **Its single L4 passed** on a second attempt, against a
stage-owned isolated stack (`jobpilot-l4-0017`, MySQL `:13399` / Redis `:16399`,
own volumes): Debug / ASan / UBSan each
`100% tests passed, 0 tests failed out of 309`, sanitizer diagnostics 0, both
Python suites green, and review-backfill isolation `[ SKIPPED ]` on the dev
database with `[ PASSED ] 7 tests` on the isolated scratch one (plan §10.2 keeps
the first, environment-limited attempt as history — the host ran 3.7× slower and
nine 2-second-timeout HTTP cases went red; the harness now refuses to run when
its `test_bcrypt` probe exceeds 14 s). CI after the change is green on every push
and the `integration` job dropped from 5m21s to 1m16s–1m49s
(`tools/dev/ci-status.sh`, plan §10.3).
**No plan is active** and the recorded MVP roadmap is complete through
packaging — 0013-0017 and the 0015A hardening are all archived under
`docs/exec-plans/completed/`. Carried forward, not done:
- 0014's Step 5 (first real China source) is **environment-limited**: no
  compliant, reusable China list contract could be verified, so no China source
  adapter is implemented and no China `source` identifier is registered (only
  `greenhouse`/`lever`/`ashby`).
- The bookmarklet's **publish/read-back acceptance** now has a one-command
  scaffold (`tools/dev/capture-acceptance.sh`, green end to end against the
  synthetic sample); only the extraction step has been run on a real login-walled
  posting so far (0014 §19.6, 【用户报告】), so the owner still has to run the
  scaffold once against a real capture JSON.
- 0015's D6 (`ai_worker` `resume_optimize` wording pass) remains **deferred by
  owner decision** (0015 plan §17).
- 0017 found one real contract gap, **fixed by 0017A (2026-10-10)**: `publish-capture`
  sends `published_at`, but the manual import neither documented nor parsed it, so a
  manual row's public `posted_at` stayed null. `published_at` is now an optional
  documented field of `POST /internal/jobs/import` (`docs/04` §5.2) written with
  `COALESCE(VALUES(published_at), published_at)`; see
  `docs/exec-plans/completed/0017A-manual-import-published-at.md`.
- 0016's CI **`image` job is confirmed green**, and the first real CI run exposed
  and fixed four things it was meant to expose: the workflow ran for the first
  time on 2026-10-10 (`clean build` ✓ and `container image and the end-to-end
  gate` ✓ — the committed `ubuntu:24.04` image builds on a runner and the demo
  gate passes against it, which closes CE-5), while the `integration` job needed
  an `.env` for compose, the ctest path corrected to the root entry's build
  directory, and the registry default moved to the official ConanCenter. Run
  `38057642118` (`9238b97`) is then green on all three jobs, including the suite
  job's first genuine execution. The repository being **private** is still the one
  thing that keeps the packaging work from being open source in practice:
  making it public, filling About/description and rotating the old PAT are owner
  actions collected in `docs/RELEASE_CHECKLIST.md`.

Product direction is China-first per
`docs/adr/0011-china-first-sourcing-and-compliance.md` (public sources plus
manual capture import, campus focus, company career pages, never auto-submit).
P1 network/HTTP remains behaviorally frozen.

Verification and agent workflow rules: `docs/ai/agent-workflow.md`. Read it
before running any test suite — it defines the levels, when a suite may run, and
the harness prerequisites (credentials probe, full ctest capture, no re-runs of
already-green suites).
An L4 run is exclusive: while one is in progress, no other agent may build the
same targets or run an E2E against the same database.

## Agent Takeover

Repository files are authoritative. Do not rely on chat history.

Read in this order:

1. This `AGENTS.md`.
2. `docs/ai/README.md`.
3. `docs/ai/project-state.md`.
4. The relevant active/completed ExecPlan.
5. Only the code, tests, API, DB, architecture, or quality sections needed for
   the task.

Do not require the user to restate project history. If the repository is
ambiguous, inspect Git history and the referenced ExecPlans before asking.

## Task Routing

- Ordinary code change: target code/tests plus relevant `docs/06` sections.
- Architecture or module boundary: `docs/architecture/open-source-mvp.md` and
  relevant accepted ADRs; historical architecture only when its history is
  relevant (see Historical Documents).
- API or database contract change: `docs/04` or `docs/03`, including impact.
- Project takeover/status: `docs/ai/project-state.md`.
- Level 3/4 work: `engineering-research-plan`, `docs/exec-plans/README.md`,
  active ExecPlan, and relevant contract.
- Build/test failure: `cpp-build-diagnose`.
- C/C++ review: `cpp-code-review`.
- Network/HTTP Reactor change: `jobpilot-reactor-review` and only its relevant
  reference.
- AI documentation routing: `docs/ai/README.md`.
- Verification scope and test evidence: `docs/quality/README.md`.

Read only the sections needed for the current task.

## Historical Documents

The following are Historical / Superseded for the current MVP. They must not be
used as the basis for new design decisions; read them only for history. This
list matches `docs/ai/README.md`.

- `docs/00-project-overview.md`
- `docs/01-requirements.md`
- `docs/02-system-architecture.md`
- `docs/05-development-plan.md`
- `docs/07-deployment.md`
- `docs/08-execution-guide.md`
- `docs/project-structure.md`

Current architecture: `docs/architecture/open-source-mvp.md`. Current decisions:
`docs/adr/README.md` (ADR-0007 through ADR-0010).

## Hard Constraints

- Preserve Controller -> Service -> DAO dependency direction.
- Do not change `docs/03` schema or `docs/04` API contracts without updating
  the contract and obtaining review.
- Keep business logic out of the network layer.
- Never log or commit passwords, tokens, secrets, `.env` values, or local
  credentials.
- Use structured JSON, parameterized SQL, documented errors, RAII, and explicit
  ownership for fds, callbacks, and locks.
- Do not weaken warnings, tests, Sanitizers, or error handling; do not revert
  unrelated user changes.

## P1 Freeze

Do not change `backend/{include,src}/jobpilot/{network,http}/` unless an
explicitly approved correctness plan permits it. Network work must load
`jobpilot-reactor-review` before implementation or final review.

## Build and Verify

- Use `service/CMakeLists.txt`, `service/conanfile.txt`, active ExecPlan, and
  `docs/quality/README.md`; `docs/08-execution-guide.md` is historical.
- Prefer documented CMake presets and `ctest --test-dir <build>`.
- Select the smallest sufficient L1-L5 verification level from
  `docs/quality/README.md`.
- The L1-L5 labels here are verification levels, not personal task-size levels.
- Follow the diff-first source-reading policy in `docs/ai/README.md`.
- Do not rerun a full test or sanitizer matrix that already passed unless
  relevant files changed, the environment was invalidated, or a stage/commit
  gate explicitly requires it.
- A change is complete only after relevant tests run or a verification gap is
  explicitly reported.
