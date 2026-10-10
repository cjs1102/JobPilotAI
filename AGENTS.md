# JobPilotAI Project Guide

## Project

C++17 Linux backend for an AI-assisted job application platform. `service/` is
the canonical runnable entry; root CMake is the unified developer entry.

Current phase: **0016 open-source packaging is complete and archived with its L4
green** (`docs/exec-plans/completed/0016-open-source-packaging.md`: HEAD
`0a6e59e`, frozen and clean tree; Debug / ASan / UBSan `100% tests passed, 0
tests failed out of 309`, sanitizer diagnostics 0). **No plan is active** and the
recorded MVP roadmap is complete through packaging — 0013-0016 and the 0015A
hardening are all archived under `docs/exec-plans/completed/`. Carried forward,
not done:
- 0014's Step 5 (first real China source) is **environment-limited**: no
  compliant, reusable China list contract could be verified, so no China source
  adapter is implemented and no China `source` identifier is registered (only
  `greenhouse`/`lever`/`ashby`).
- The bookmarklet's **publish/read-back acceptance is still open**: only the
  extraction step was run by the product owner on a real login-walled posting
  (0014 §19.6, 【用户报告】), while `publish-capture` and backend persistence were
  not executed (§19.5).
- 0015's D6 (`ai_worker` `resume_optimize` wording pass) remains **deferred by
  owner decision** (0015 plan §17).
- 0016's CI **`image` job is confirmed green**, and the first real CI run exposed
  and fixed four things it was meant to expose: the workflow ran for the first
  time on 2026-10-10 (`clean build` ✓ and `container image and the end-to-end
  gate` ✓ — the committed `ubuntu:24.04` image builds on a runner and the demo
  gate passes against it, which closes CE-5), while the `integration` job needed
  an `.env` for compose, the ctest path corrected to the root entry's build
  directory, and the registry default moved to the official ConanCenter. Run
  `38057642118` (`9238b97`) is then green on all three jobs, including the suite
  job's first genuine execution. The repository being **private** is still the one
  thing that keeps the packaging work from being open source in practice.

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
