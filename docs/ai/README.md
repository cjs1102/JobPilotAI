# JobPilotAI AI Context Router

Use this file to select the smallest relevant context. Do not read the entire
AI directory by default.

## Task Routing

| Task | Load |
|---|---|
| Level 1/2 code change | Project `AGENTS.md`, relevant code/tests, and only the relevant section of `docs/06-coding-standard.md` |
| Build/CMake/Conan/CTest failure | `cpp-build-diagnose` |
| C/C++ code review | `cpp-code-review` |
| Network/HTTP lifetime or Reactor change | `jobpilot-reactor-review` and only its relevant reference |
| Level 3/4 feature or architecture work | `engineering-research-plan`, `docs/exec-plans/README.md`, the active ExecPlan, and only the relevant architecture/API/DB sections |
| Project takeover or status | `project-state.md` |
| Historical project detail | `references/project-state-full.md` |
| GitHub/tooling research | `references/github_resources.md` |
| Old phase planning | `archive/next-phase-plan-2026-10.md` |
| Quality or benchmark evidence | `../quality/README.md` or `../benchmarks/README.md` |

## Authoritative Sources

- Execution order and stop lines: `../08-execution-guide.md`
- Architecture: `../02-system-architecture.md`
- Coding rules: `../06-coding-standard.md`
- API contract: `../04-api-design.md`
- Database contract: `../03-database-design.md`

Read only the sections needed for the current task.

## Rule Ownership

- Personal cross-project defaults: the active agent's user configuration
- Project-specific rules: repository `AGENTS.md`
- Reusable procedures: matching Skill
- Detailed evidence and history: `references/` or completed ExecPlans
- User-global workflows: `~/.agents/skills/`
- Workspace-common AI architecture: `~/projects/docs/ai/`
- JobPilotAI-specific Skills: `.agents/skills/`

Do not duplicate rules across these layers; link to the authoritative file.
