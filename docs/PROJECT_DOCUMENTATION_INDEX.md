# JobPilotAI Project Documentation Index

Refreshed 2026-10-23 for the public repository. It answers one question per
document: **what is this, who should read it, and is it still true?** The live
project state is [`ai/project-state.md`](ai/project-state.md).

Status vocabulary:

- `CURRENT` — describes the code as it is today.
- `HISTORICAL` — retained for context; carries a Superseded banner and must not
  drive new design decisions.
- `REFERENCE` — facts that do not age with the code.
- `MISSING` — not present.

## 0. Reading order

1. `README.md` / `README.zh-CN.md` — what the project is and how to run the demo.
2. `AGENTS.md` — repository rules, task routing, and the P1 freeze.
3. `docs/ai/README.md` → `docs/ai/project-state.md` — AI context and live state.
4. `docs/architecture/open-source-mvp.md` and the accepted ADRs.
5. `docs/04-api-design.md` + `docs/03-database-design.md` before an API/schema
   change.
6. `docs/quality/README.md` before running a test suite.
7. `docs/exec-plans/README.md` for the stage index; bodies are in Git history.

## 1. Entry points and governance

| Path | Purpose | Audience | Validity |
|---|---|---|---|
| `README.md` | English README: why, features, architecture, quick start, usage, docs, roadmap | Everyone, first file | `CURRENT` |
| `README.zh-CN.md` | Chinese mirror with the same section order | Chinese-speaking users | `CURRENT` |
| `docs/images/` | Two 1366×682 local-UI screenshots with synthetic data | Everyone | `CURRENT` |
| `AGENTS.md` | Hard constraints, P1 freeze, task routing, verification policy | Contributors and agents | `CURRENT` |
| `SECURITY.md` | Supported versions, private reporting, local-demo boundaries, no-secrets policy | Everyone | `CURRENT` |
| `CONTRIBUTING.md` | Setup, Python lock, branch/PR flow, verification, non-goals | Contributors | `CURRENT` |
| `THIRD_PARTY_NOTICES.md` | Dependency licences and provenance | Maintainer, release reviewer | `CURRENT` |
| `LICENSE` | Standard MIT licence | Everyone | `CURRENT` |
| `docs/README.md` | Short documentation map | Everyone | `CURRENT` |

## 2. AI context

| Path | Purpose | Audience | Validity |
|---|---|---|---|
| `docs/ai/README.md` | Context router and source-reading policy | Agents | `CURRENT` |
| `docs/ai/agent-workflow.md` | Verification levels L1–L5 and execution rules | Agents | `CURRENT` |
| `docs/ai/project-state.md` | Current snapshot, capabilities, limits, verification baseline, open follow-ups | Agents, maintainer | `CURRENT` |
| `docs/ai/references/github_resources.md` | GitHub/tooling research | Maintainer | `REFERENCE` |

## 3. Architecture and decisions

| Path | Purpose | Validity |
|---|---|---|
| `docs/architecture/open-source-mvp.md` | Current module boundaries and data flow | `CURRENT` |
| `docs/adr/README.md` | ADR index and status legend | `CURRENT` |
| `docs/adr/0001`–`0011` | Accepted technical and product decisions | `CURRENT` (ADR-0006 is superseded for the MVP) |
| `docs/02-system-architecture.md` | Original architecture design | `HISTORICAL` |
| `docs/project-structure.md` | Original directory responsibilities | `HISTORICAL` |

## 4. Contracts

| Path | Purpose | Validity |
|---|---|---|
| `docs/04-api-design.md` | HTTP routes, request/response shapes, error codes, internal-token contract | `CURRENT` |
| `docs/03-database-design.md` | 13-table schema, indexes, additive migrations, deadline rule | `CURRENT` |
| `docs/guides/local-demo.md` | Local UI, nginx/Python fallback, demo flow, troubleshooting | `CURRENT` |
| `database/schema/README.md` | Schema file order and apply/verify procedure | `CURRENT` |
| `docs/06-coding-standard.md` | C++ naming, error handling, SQL, RAII, logging | `CURRENT` |
| `docs/00-project-overview.md` | Original overview | `HISTORICAL` |
| `docs/01-requirements.md` | Original requirements | `HISTORICAL` |
| `docs/05-development-plan.md` | Original roadmap | `HISTORICAL` |

## 5. Quality, testing, and operations

| Path | Purpose | Validity |
|---|---|---|
| `docs/quality/README.md` | Verification levels, presets, sanitizers, checkers | `CURRENT` |
| `docs/quality/test-merge-map.json` | Machine-readable test-overlap guard consumed by CI | `CURRENT` |
| `docs/quality/test-overlap-audit.md` | Short summary and counts marker for the guard | `CURRENT` |
| `docs/benchmarks/README.md` | Benchmark baseline and re-run instructions | `REFERENCE` (numbers age) |
| `docs/RELEASE_CHECKLIST.md` | Release gate and owner actions | `CURRENT` (dated evidence blocks) |
| `docs/examples/` | Synthetic import/capture/demo fixtures | `CURRENT` |

## 6. Execution plans

| Path | Purpose | Validity |
|---|---|---|
| `docs/exec-plans/README.md` | Stage index: number, title, result, merge commit, one-line outcome; protocol | `CURRENT` |

Individual plan bodies were removed from the public tree in 0027. They remain in
Git history. There is no active plan.

## 7. Ingestion, sources, and tools

| Path | Purpose | Validity |
|---|---|---|
| `source_adapters/README.md` | Four adapters, robots/compliance framework, source register | `CURRENT` |
| `ai_worker/README.md` | Optional AI worker and provider configuration | `CURRENT` for code; live provider unverified |
| `tools/job_report/README.md` | Offline single-file HTML report | `CURRENT` |
| `third_party/libbcrypt/PROVENANCE.md` | Vendored bcrypt provenance and hashes | `CURRENT` (upstream revision unresolved) |

## 8. Historical documents

- `docs/00-project-overview.md`
- `docs/01-requirements.md`
- `docs/02-system-architecture.md`
- `docs/05-development-plan.md`
- `docs/project-structure.md`
- `docs/research/2026-10-08-open-source-job-search-agents.md` (research input)

## 9. CI and repository metadata

| Path | Purpose | Validity |
|---|---|---|
| `.github/workflows/ci.yml` | Clean build, full test suite, coverage-map guard, Python suites | `CURRENT` |
| `.github/workflows/image.yml` | Container build and end-to-end demo gate | `CURRENT` |
| `Dockerfile`, `docker-compose.yml`, `docker/nginx/` | Image and local stack | `CURRENT` |
| `CMakePresets.json`, `service/CMakePresets.json`, `service/conanfile.txt`, `service/conan.lock` | Build presets and pinned Conan graph | `CURRENT` |

## 10. Missing documents

| Path | Why it is needed | Suggested content |
|---|---|---|
| `CODE_OF_CONDUCT.md` | Conventional for a public project | Standard contributor covenant; deferred until a reporting channel exists |

## 11. Maintenance

- Add a row when a document is added, moved, or superseded.
- Do not mark a `HISTORICAL` document `CURRENT` without rewriting it.
- Current architecture and ADRs take precedence over historical design docs.
- Deleted plan bodies and old conflict records remain in Git history; do not
  recreate them in the public tree.
