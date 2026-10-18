# Documentation map

A short reading order. The project [README](../README.md) has the Quick Start;
the documents below are the contracts and the evidence behind it.

## To run it

1. [`../README.md`](../README.md) — Quick Start (English) plus the list of what
   actually works today, and what deliberately does not.
2. [`../database/schema/README.md`](../database/schema/README.md) — create a
   database, upgrade an existing one, verify it, and the isolation rule for demo
   and test data.
3. [`07-deployment.md`](07-deployment.md) and
   [`08-execution-guide.md`](08-execution-guide.md) — **historical**, superseded
   designs; each carries a dated correction listing the parts that no longer
   work. Do not follow them.

## To understand the design

4. [`00-project-overview.md`](00-project-overview.md) — what the project is.
5. [`01-requirements.md`](01-requirements.md) — requirements and non-goals.
6. [`02-system-architecture.md`](02-system-architecture.md) and
   [`architecture/open-source-mvp.md`](architecture/open-source-mvp.md) — the
   architecture direction and the MVP scope classification.
7. [`03-database-design.md`](03-database-design.md) — schema, indexes and the
   additive migration contracts.
8. [`04-api-design.md`](04-api-design.md) — the HTTP contract.
9. [`adr/`](adr/) — the accepted architecture decisions.

## To change the code

10. [`../AGENTS.md`](../AGENTS.md) — how this repository is worked on.
11. [`ai/agent-workflow.md`](ai/agent-workflow.md) — verification levels L0-L5,
    execution budget, stop conditions.
12. [`quality/README.md`](quality/README.md) — the sanitizer presets, their
    dependency boundary and the recorded baselines.
13. [`ai/project-state.md`](ai/project-state.md) — the current snapshot: which
    stages are closed, which are open, and what is carried forward.
14. [`exec-plans/`](exec-plans/) — `active/` holds the plan being executed,
    `completed/` is the evidence archive of every finished stage.
15. [`06-coding-standard.md`](06-coding-standard.md) — the code style.
16. [`RELEASE_CHECKLIST.md`](RELEASE_CHECKLIST.md) — the release gate: what must be
    true before an outside reader can trust a tag, which parts are automated, and
    which parts only the repository owner can confirm.

## To take over the project

17. [`CLAUDE_CODE_HANDOFF.md`](CLAUDE_CODE_HANDOFF.md) — the handoff written on
    2026-10-11: the git baseline, the 38-row capability matrix, completed work and
    its evidence, open work, known bugs, technical debt, the China-source register,
    verification items, release blockers, and the protocol for the first phase
    (verify the facts → `CURRENT_STATE.md` → `EXEC_PLAN.md` → wait for approval).
18. [`PROJECT_DOCUMENTATION_INDEX.md`](PROJECT_DOCUMENTATION_INDEX.md) — every
    tracked document with purpose, audience, validity (`CURRENT` / `HISTORICAL` /
    `STALE` / `MISSING`), related code, and the `DOCUMENT_CONFLICT` records.
