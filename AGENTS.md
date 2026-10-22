# JobPilotAI Project Guide

## Project

C++17 Linux backend for a job-search and application-tracking product. The
repository is a modular monolith: `service/` is the business layer and the
runnable entry; `backend/` is `jobpilot_core` (network/HTTP, config, logging,
MySQL, Redis); `source_adapters/` and `ai_worker/` are optional Python tools;
`web/` is the native local UI.

Current phase: stages 0001–0026 are complete, `v0.1.0` is tagged, and the
repository is public. There is **no active code plan**. A docs-only repository
hygiene/README change may be in progress; check
`docs/ai/project-state.md` before starting work.

Product direction: China-first public sources plus manual capture, campus focus,
company career pages, and **never auto-submit**. P1 network/HTTP remains
behaviorally frozen.

## Agent takeover

Repository files and Git history are authoritative. Do not rely on chat history.

Read in this order:

1. This `AGENTS.md`.
2. `docs/ai/README.md`.
3. `docs/ai/project-state.md`.
4. `docs/exec-plans/README.md` (stage index; plan bodies are in Git history).
5. `docs/architecture/open-source-mvp.md`, the accepted ADRs, and the relevant
   API/database contract.
6. Only the code, tests, or quality sections needed for the task.

Do not require the user to restate project history. If the repository is
ambiguous, inspect Git history and the referenced contracts before asking.

## Task routing

- Ordinary code change: target code/tests plus the relevant `docs/06` section.
- Architecture or module boundary: `docs/architecture/open-source-mvp.md` and
  the accepted ADRs.
- API or database contract change: `docs/04` or `docs/03`, including impact.
- Project status: `docs/ai/project-state.md`.
- Level 3/4 work: `engineering-research-plan`, `docs/exec-plans/README.md`, and
  the relevant architecture/API/DB sections.
- Build/test failure: `cpp-build-diagnose`.
- C/C++ review: `cpp-code-review`.
- Network/HTTP Reactor change: stop and obtain an explicitly approved
  correctness plan first; read `docs/architecture/open-source-mvp.md` and the
  network tests.
- Verification scope: `docs/quality/README.md`.

Read only the sections needed for the current task.

## Historical documents

The following are Historical / Superseded. Read them only for context; they must
not drive new design decisions.

- `docs/00-project-overview.md`
- `docs/01-requirements.md`
- `docs/02-system-architecture.md`
- `docs/05-development-plan.md`
- `docs/project-structure.md`

Current architecture: `docs/architecture/open-source-mvp.md`. Current decisions:
`docs/adr/README.md` (ADR-0007 through ADR-0011).

## Hard constraints

- Preserve the Controller → Service → DAO dependency direction.
- Do not change `docs/03` schema or `docs/04` API contracts without updating the
  contract and obtaining review.
- Keep business logic out of the network layer.
- Never log or commit passwords, tokens, secrets, `.env` values, or local
  credentials.
- Use structured JSON, parameterized SQL, documented errors, RAII, and explicit
  ownership for descriptors, callbacks, and locks.
- Do not weaken warnings, tests, sanitizers, or error handling; do not revert
  unrelated user changes.
- Work on a feature/docs branch. The worker does not push `main`; the owner
  merges the PR.

## P1 freeze

Do not change `backend/{include,src}/jobpilot/{network,http}/` unless an
explicitly approved correctness plan permits it. Network work must include a
focused lifetime, backpressure, and ownership review before implementation or
final review.

## Build and verify

- Use `service/CMakeLists.txt`, `service/conanfile.txt`, `CMakePresets.json`, and
  `docs/quality/README.md`.
- Prefer documented CMake presets and `ctest --test-dir <build>`.
- Select the smallest sufficient verification level from
  `docs/quality/README.md`; L1–L5 are verification levels, not task-size levels.
- Follow the diff-first source-reading policy in `docs/ai/README.md`.
- Do not rerun a full test or sanitizer matrix that already passed unless
  relevant files changed, the environment was invalidated, or a stage/commit
  gate explicitly requires it.
- A change is complete only after the relevant tests run or a verification gap
  is explicitly reported.
