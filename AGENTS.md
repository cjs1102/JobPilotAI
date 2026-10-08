# JobPilotAI Project Guide

## Project

C++17 Linux backend for an AI-assisted job application platform. `service/` is
the canonical runnable entry; root CMake is the unified developer entry.

Current phase: 0013 AI Matching is the active plan (`job_match` end to end);
Steps 1-3 (schema migration, `match_rule_v1`, AI Task DAO) are implemented and
verified, and Step 4 (AI Task Service) is next. 0012 is complete and archived
under `docs/exec-plans/completed/`. Product direction is China-first per
`docs/adr/0011-china-first-sourcing-and-compliance.md` (public sources plus
manual capture import, campus focus, company career pages, never auto-submit).
P1 network/HTTP remains behaviorally frozen.

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
