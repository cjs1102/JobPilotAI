# JobPilotAI AI Context Router

Use this file to select the smallest relevant context. Do not read the entire
documentation tree by default.

Repository files and Git history are the cross-agent fact source. A new agent
must not require the user to restate completed project history.

## Handoff path

1. Read `AGENTS.md`.
2. Read this router.
3. Read `project-state.md`.
4. For historical detail, use `../exec-plans/README.md` and Git history.
5. Use diff-first and targeted reads for code.
6. Follow: research → capability check → design → implementation → verification.

There is currently no active plan.

## Task routing

| Task | Load |
|---|---|
| Level 1/2 code change | `AGENTS.md`, relevant code/tests, and only the relevant section of `../06-coding-standard.md` |
| Build/CMake/Conan/CTest failure | `cpp-build-diagnose` |
| C/C++ code review | `cpp-code-review` |
| Network/HTTP lifetime or Reactor change | `AGENTS.md` P1 freeze, `../architecture/open-source-mvp.md`, and the relevant network tests |
| Level 3/4 feature or architecture work | `engineering-research-plan`, `../exec-plans/README.md`, the current architecture/API/DB sections |
| Project status or takeover | `project-state.md` |
| Historical stage detail | `../exec-plans/README.md` plus Git history |
| GitHub/tooling research | `references/github_resources.md` |
| Quality or benchmark evidence | `../quality/README.md` or `../benchmarks/README.md` |
| Pre-ADR-0007 design material | Historical only: `../00`–`../02`, `../05`, `../project-structure.md` |

## Source-reading policy

1. Start with `git diff --stat` and identify the changed targets.
2. Use `git diff -- <specific files>` before opening whole files.
3. Use `rg -n` to locate symbols, functions, routes, error handling,
   authentication, ownership, and call sites.
4. Read a complete file only when surrounding control flow, invariants, or
   lifetime context cannot be established from the targeted diff/search.
5. Do not reread large unchanged files or repeat context already confirmed.
6. For tests, keep the first failure and a concise result summary; do not replay
   complete successful logs.

If new evidence invalidates an earlier conclusion, reopen only the affected files
or sections.

## Authoritative sources

- Current architecture: `../architecture/open-source-mvp.md`
- Current architecture decisions: `../adr/README.md`
- Current project state: `project-state.md`
- Stage index: `../exec-plans/README.md`
- Coding rules: `../06-coding-standard.md`
- API contract: `../04-api-design.md`
- Database contract: `../03-database-design.md`
- Verification levels and evidence: `../quality/README.md`
- Public documentation map: `../README.md`

Historical design material (each carries a Superseded banner and must not drive
new implementation decisions):

- `../00-project-overview.md`
- `../01-requirements.md`
- `../02-system-architecture.md`
- `../05-development-plan.md`
- `../project-structure.md`

Where they conflict with current ADRs or architecture, the current architecture
document and ADR directory take precedence.

## Rule ownership

- Personal cross-project defaults: the active agent's user configuration
- Project-specific rules: repository `AGENTS.md`
- Reusable procedures: the matching skill
- Detailed history: Git history and the stage index
- User-global workflows: `~/.agents/skills/`

Do not duplicate rules across these layers; link to the authoritative file.

- Agent workflow and verification rules: [`agent-workflow.md`](agent-workflow.md)
