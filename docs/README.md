# Documentation map

The project [README](../README.md) has the Quick Start. This page is the short
reading order for the contracts and evidence behind it.

## Run it

1. [`../README.md`](../README.md) / [`../README.zh-CN.md`](../README.zh-CN.md) —
   the one-command demo, usage, capabilities, and non-goals.
2. [`../database/schema/README.md`](../database/schema/README.md) — create or
   upgrade the database and isolate demo/test data.
3. [`guides/local-demo.md`](guides/local-demo.md) — the local Web UI and its
   Python fallback.

## Understand the design

4. [`00-project-overview.md`](00-project-overview.md) — project overview.
5. [`01-requirements.md`](01-requirements.md) — requirements and non-goals.
6. [`02-system-architecture.md`](02-system-architecture.md) and
   [`architecture/open-source-mvp.md`](architecture/open-source-mvp.md) — the
   original design and the current MVP architecture.
7. [`03-database-design.md`](03-database-design.md) — schema, indexes, and
   additive migration contracts.
8. [`04-api-design.md`](04-api-design.md) — the HTTP contract.
9. [`adr/`](adr/) — accepted architecture decisions.
10. [`05-development-plan.md`](05-development-plan.md) — the original roadmap,
    kept for context.

## Change and verify the code

11. [`../AGENTS.md`](../AGENTS.md) — repository rules, task routing, and the P1
    freeze.
12. [`06-coding-standard.md`](06-coding-standard.md) — C++ style and review
    rules.
13. [`ai/agent-workflow.md`](ai/agent-workflow.md) — verification levels and
    execution rules.
14. [`quality/README.md`](quality/README.md) — presets, tests, sanitizers, and
    benchmark evidence.
15. [`exec-plans/README.md`](exec-plans/README.md) — the stage index (plan
    bodies are in Git history).
16. [`ai/project-state.md`](ai/project-state.md) — the current snapshot and
    known gaps.
17. [`PROJECT_DOCUMENTATION_INDEX.md`](PROJECT_DOCUMENTATION_INDEX.md) — the
    document inventory and validity notes.
18. [`RELEASE_CHECKLIST.md`](RELEASE_CHECKLIST.md) — the release gate.
