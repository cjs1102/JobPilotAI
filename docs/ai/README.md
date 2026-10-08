# JobPilotAI AI Context Router

Use this file to select the smallest relevant context. Do not read the entire
AI directory by default.

Repository files and Git history are the cross-agent fact source. A new Agent
must not require the user to restate completed project history.

## Agent Handoff Path

1. Read `AGENTS.md`.
2. Read this router.
3. Read `project-state.md`.
4. Identify the active/completed ExecPlan for the task.
5. Use diff-first and targeted reads for code.
6. Follow:

```text
Research -> Capability Check -> Design -> Plan -> Implement -> Verify
```

Do not load `references/project-state-full.md` unless historical detail is
needed. It is a point-in-time archive, not current state.

## Task Routing

| Task | Load |
|---|---|
| Level 1/2 code change | Project `AGENTS.md`, relevant code/tests, and only the relevant section of `docs/06-coding-standard.md` |
| Build/CMake/Conan/CTest failure | `cpp-build-diagnose` |
| C/C++ code review | `cpp-code-review` |
| Network/HTTP lifetime or Reactor change | `jobpilot-reactor-review` and only its relevant reference |
| Level 3/4 feature or architecture work | `engineering-research-plan`, `docs/exec-plans/README.md`, the active ExecPlan, and only the relevant architecture/API/DB sections |
| Project takeover or status | `project-state.md` |
| Active implementation work | The file named in `project-state.md` and `../exec-plans/active/` |
| Historical project detail | `references/project-state-full.md` |
| GitHub/tooling research | `references/github_resources.md` |
| Old phase planning | `archive/next-phase-plan-2026-10.md` |
| Pre-ADR-0007 design material | Historical only: `../00`–`../02`, `../05`, `../07`, `../08`, `../project-structure.md` |
| Quality or benchmark evidence | `../quality/README.md` or `../benchmarks/README.md` |

## Source Reading Policy

1. Start with `git diff --stat` and identify the changed targets.
2. Use `git diff -- <specific files>` before opening whole files.
3. Use `rg -n` to locate symbols, functions, routes, error handling,
   authentication, ownership, and call sites.
4. Read a complete file only when surrounding control flow, invariants, or
   lifetime context cannot be established from the targeted diff/search.
5. Do not reread large unchanged files or repeat context already confirmed.
6. For tests, keep the first failure and a concise result summary; do not
   replay complete successful logs.

If new evidence invalidates an earlier conclusion, reopen only the affected
files or sections.

## Authoritative Sources

- Current architecture: `../architecture/open-source-mvp.md`
- Current architecture decisions: `../adr/README.md`
- Current project state: `project-state.md`
- Active and completed stage contracts: `../exec-plans/`
- Coding rules: `../06-coding-standard.md`
- API contract: `../04-api-design.md`
- Database contract: `../03-database-design.md`
- Verification levels and current evidence: `../quality/README.md`

Historical design material (each carries a Superseded banner and must not
drive new implementation decisions):

- `../00-project-overview.md`
- `../01-requirements.md`
- `../02-system-architecture.md`
- `../05-development-plan.md`
- `../07-deployment.md`
- `../08-execution-guide.md`
- `../project-structure.md`

Where they conflict with current ADRs or architecture, the current architecture
document and ADR directory take precedence.

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

- Agent workflow and verification rules: [`agent-workflow.md`](agent-workflow.md)
