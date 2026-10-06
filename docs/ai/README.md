# JobPilotAI AI Knowledge Map

This directory contains AI-facing project documentation. It does not replace
the architecture, API, database, or execution documents.

## Documents

| Document | Purpose |
|---|---|
| `github_resources.md` | GitHub resource research and classification. Reference only; do not copy third-party code. |
| `project-state.md` | Current disk, Git, build, test, architecture, and technical-debt snapshot for AI takeover. |
| `next-phase-plan.md` | Dependency-based plan for P0 completion, P1 correctness, P2 integration, and quality work. |
| `../quality/README.md` | Repeatable ASan, UBSan, and TSan commands and recorded results. |
| `../benchmarks/README.md` | Microbenchmark and HTTP throughput/latency baseline. |
| `README.md` | Navigation and boundaries for AI-facing documentation. |
| `../../.agents/skills/jobpilot-reactor-review/SKILL.md` | Project workflow for reviewing Reactor, epoll, fd, callback, threading, shutdown, and network-test changes. |

## Authoritative Sources

- Execution order and stop lines: `../08-execution-guide.md`
- Architecture: `../02-system-architecture.md`
- Coding rules: `../06-coding-standard.md`
- API contract: `../04-api-design.md`
- Database contract: `../03-database-design.md`

## Layer Boundary

- User-global workflows live in `~/.agents/skills/`.
- Workspace-common AI architecture lives in `~/projects/docs/ai/`.
- JobPilotAI-specific Skills live in `.agents/skills/`.
- JobPilotAI-specific rules live in the root `AGENTS.md` and this directory.

## Documentation Rules

- Keep one source of truth; link instead of duplicating.
- Put durable project facts in `AGENTS.md`.
- Put reusable procedures in Skills.
- Put detailed design or evidence in focused documents.
- Create an ExecPlan only for Level 3 or Level 4 work.
