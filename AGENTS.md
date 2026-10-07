# JobPilotAI Project Guide

## Project

C++17 Linux backend for an AI-assisted job application platform. `service/` is
the canonical runnable entry; root CMake is the unified developer entry.

Current phase: P2 after the completed user/auth vertical slice. P1
network/HTTP remains behaviorally frozen.

## Task Routing

- Ordinary code change: target code/tests plus relevant `docs/06` sections.
- Architecture or module boundary: relevant `docs/02` sections.
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

- Use `service/CMakeLists.txt`, `service/conanfile.txt`, and
  `docs/08-execution-guide.md`; do not invent flags.
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
