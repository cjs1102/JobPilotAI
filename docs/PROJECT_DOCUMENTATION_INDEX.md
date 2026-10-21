# JobPilotAI Project Documentation Index

Maintained for the Claude Code handoff; refreshed 2026-10-21 for the README and
demo tooling. It answers one question per document: **what is this, who should
read it, and is it still true?** When a document disagrees with the code, the
row says so and the disagreement is recorded in §12 instead of being silently
"fixed".

Status vocabulary used below:

- `CURRENT` — describes the code as it is today; safe to rely on.
- `HISTORICAL` — deliberately kept for history; **carries a Superseded banner**
  and must not be used as the basis for new design decisions.
- `REFERENCE` — facts that do not age with the code (research notes, pins).
- `STALE` — partly out of date; read only with the notes in this file.
- `MISSING` — the repository does not have it yet.

## 0. Reading order for a new contributor or agent

1. `README.md` / `README.zh-CN.md` — what the project is, the one-command demo,
   and how to use it.
2. `AGENTS.md` — the project's hard constraints, P1 freeze, task routing.
3. `docs/CLAUDE_CODE_HANDOFF.md` — the current state, gaps and the protocol for
   the first phase (the document you are likely to need next).
4. `docs/ai/README.md` → `docs/ai/project-state.md` — AI context router and the
   current project state.
5. `docs/architecture/open-source-mvp.md` and the accepted ADRs in `docs/adr/`.
6. `docs/04-api-design.md` + `docs/03-database-design.md` before touching an API
   or the schema.
7. `docs/quality/README.md` before running any test suite (verification levels).
8. `docs/exec-plans/completed/` for the history of how it got here.

## 1. Entry points and governance

| Path | Document | Purpose | Audience | Validity | Related code |
|---|---|---|---|---|---|
| `README.md` | Project README (English) | What the project is, two screenshots, one-command Quick Start, demo flow, usage, verification, limits, community links | Everyone, first file to read | `CURRENT` (2026-10-21) | whole repo |
| `README.zh-CN.md` | Chinese README | Chinese mirror of the English README with the same sections, screenshots and commands; no mixed-language paragraphs | Chinese-speaking users and contributors | `CURRENT` (2026-10-21) | whole repo |
| `docs/images/` | README screenshots | `web-detail.png` (explainable match) and `web-status.png` (application board), both 1366×682 from the local UI with synthetic data | Everyone | `CURRENT` (2026-10-21; regenerated with the browser smoke after the temporary originals were removed) | `web/`, `tools/dev/web-smoke.sh` |
| `AGENTS.md` | Project Guide | Hard constraints (dependency direction, P1 freeze, secrets, JSON/SQL/RAII rules), task routing, verification policy | Every agent/contributor | `CURRENT` | whole repo |
| `.agents/skills/jobpilot-reactor-review/SKILL.md` | Reactor review skill | Mandatory checklist before touching `backend/{include,src}/jobpilot/{network,http}/` | Anyone changing the network layer | `CURRENT` | `backend/src/network`, `backend/src/http` |
| `SECURITY.md` | Security policy | Supported versions, private reporting, local-demo boundaries, no-secrets policy, A history disclosure (facts only) | Everyone, release reviewer | `CURRENT` (2026-10-20) | `tools/checks/` |
| `CONTRIBUTING.md` | Contribution guide | Setup, Python lock, branch/PR/review flow, P1 freeze, verification and non-goals | Contributors, agents | `CURRENT` (2026-10-20) | whole repo |
| `THIRD_PARTY_NOTICES.md` | Third-party notices | Licence of every dependency, verified from the pinned package; MariaDB client and libbcrypt provenance | Maintainer, release reviewer | `CURRENT` | `service/conanfile.txt`, `third_party/`, `Dockerfile` |
| `LICENSE` | MIT licence | The licence the repository ships | Everyone | `CURRENT` | — |
| `docs/README.md` | Documentation map | Reading order and the historical/current split | Everyone | `CURRENT` | — |

## 2. AI context (agents)

| Path | Document | Purpose | Audience | Validity | Related code |
|---|---|---|---|---|---|
| `docs/ai/README.md` | AI context router | Which document to read for which task; diff-first reading policy | Agents | `CURRENT` | — |
| `docs/ai/agent-workflow.md` | Agent workflow and verification rules | Verification levels L1–L5, when a suite may run, harness prerequisites, one review per stage | Agents | `CURRENT` | `build/l4-0017/` (scratch harness) |
| `docs/ai/project-state.md` | Current project state (1011 lines) | Stage-by-stage state, carried-forward items, what is genuinely open | Agents, maintainer | `CURRENT` (updated 2026-10-21) | whole repo |
| `docs/ai/CURRENT_STATE.md` | Handoff verification report (2026-10-19) | Git/CI facts, §23 fact checklist, document-code conflicts, capability-matrix update; 2026-10-20 resolution note | Agents, maintainer, release reviewer | `CURRENT` (dated 2026-10-19; point-in-time report plus dated resolution note) | whole repo |
| `docs/ai/archive/next-phase-plan-2026-10.md` | Archived next-phase plan | An earlier plan sketch; kept for context | Agents | `REFERENCE` (archived) | — |
| `docs/ai/references/github_resources.md` | GitHub resource research | 655-line survey used for the open-source plan | Maintainer | `REFERENCE` | — |
| `docs/ai/references/project-state-full.md` | Historical project-state snapshot | Long-form state at an earlier point | Agents | `HISTORICAL` | — |

## 3. Architecture and decisions

| Path | Document | Purpose | Audience | Validity | Related code |
|---|---|---|---|---|---|
| `docs/architecture/open-source-mvp.md` | Current architecture | Module boundaries, data flow, what the MVP is | Everyone | `CURRENT` | `service/`, `backend/`, `source_adapters/` |
| `docs/adr/0001-tech-stack.md` | ADR-0001 | C++17 backend + MySQL + Redis + Python tooling | Maintainer | `CURRENT` (Accepted) | — |
| `docs/adr/0002-architecture-style.md` | ADR-0002 | Modular monolith + separate service; Controller→Service→DAO | Maintainer | `CURRENT` (Accepted for the C++ modular monolith) | `service/src` |
| `docs/adr/0003-database-choice.md` | ADR-0003 | MySQL choice | Maintainer | `CURRENT` (Accepted) | `database/` |
| `docs/adr/0004-message-queue.md` | ADR-0004 | RabbitMQ as a **future target only** | Maintainer | `CURRENT` — explicitly not built | — |
| `docs/adr/0005-ai-agent-architecture.md` | ADR-0005 | AI as an optional worker, deterministic first | Maintainer | `CURRENT` (target direction) | `ai_worker/` |
| `docs/adr/0006-ai-data-access.md` | ADR-0006 | AI data access | Maintainer | **Superseded for the MVP** | `service/src/ai` |
| `docs/adr/0007-open-source-first-positioning.md` | ADR-0007 | Open-source-first positioning | Maintainer | `CURRENT` | — |
| `docs/adr/0008-source-adapters-and-job-discovery.md` | ADR-0008 | Source adapters instead of a general crawler | Everyone touching ingestion | `CURRENT` | `source_adapters/` |
| `docs/adr/0009-ai-pipeline-and-provider-strategy.md` | ADR-0009 | Deterministic-first, provider-neutral AI | Everyone touching matching/AI | `CURRENT` | `service/src/ai`, `ai_worker/` |
| `docs/adr/0010-job-freshness-and-lifecycle.md` | ADR-0010 | Freshness, disappearance grace, `missing_count`, visibility | Everyone touching ingestion | `CURRENT` | `service/src/job/source_observation.cpp` |
| `docs/adr/0011-china-first-sourcing-and-compliance.md` | ADR-0011 | China-first sourcing, compliance boundary, no auto-submit, campus focus | Everyone touching China sources | `CURRENT` (Accepted 2026-10-08) | `source_adapters/` |
| `docs/adr/README.md` | ADR index | ADR template and status legend | Maintainer | `CURRENT` | — |

## 4. Contracts

| Path | Document | Purpose | Audience | Validity | Related code |
|---|---|---|---|---|---|
| `docs/04-api-design.md` | API contract (1428 lines) | Every route, request/response, error codes, internal token contract, public visibility rule (updated 2026-10-11), source-registration contract, reserved-route markers and route counts (2026-10-20) | Anyone changing an API | `CURRENT` | `service/src/*/*_controller.cpp`, `service/src/router.cpp` |
| `docs/03-database-design.md` | Database contract (834 lines) | Tables, columns, indexes, F13 deadline rule, 0015A quality status, current 13-table DDL, migration notes (§9.4) | Anyone changing the schema | `CURRENT` | `database/schema/0*.sql` |
| `docs/guides/local-demo.md` | Local Web UI demo guide | Dependencies, one-command launcher, nginx/Python-fallback startup, demo flow, resume prerequisite, screenshots, troubleshooting, security boundary | Users, reviewers, maintainers | `CURRENT` (2026-10-21) | `web/`, `docker/nginx/`, `tools/dev/demo-up.sh`, `tools/dev/web-smoke.sh` |
| `database/schema/README.md` | Schema guide | How the schema files are ordered and applied | Anyone touching the DB | `CURRENT` | `database/schema/` |
| `docs/06-coding-standard.md` | Coding standard | Naming, error handling, SQL, RAII, logging rules | Contributors | `CURRENT` | whole repo |
| `docs/project-structure.md` | Project structure | Directory/module responsibilities | Contributors | `HISTORICAL` (Superseded banner) | — |

## 5. Quality, testing and operations

| Path | Document | Purpose | Audience | Validity | Related code |
|---|---|---|---|---|---|
| `docs/quality/README.md` | Quality baseline | Verification levels L1–L5, the test/verification toolchain (preflight, coverage map, capture acceptance, demo gate, history scanner, CI status) | Everyone running tests | `CURRENT` | `tools/checks/`, `tools/dev/` |
| `docs/quality/test-overlap-audit.md` | Test overlap audit (0017-B) | Proof that the suite has no mergeable duplicate case; the 66 evaluated pairs; counts block read by the guard | Maintainer | `CURRENT` | `service/tests/`, `tools/checks/test_coverage_map.py` |
| `docs/benchmarks/README.md` | Benchmark baseline | Recorded performance numbers and how to re-run them | Maintainer | `REFERENCE` (numbers age) | `build/` presets |
| `docs/RELEASE_CHECKLIST.md` | Release checklist (333 lines) | The gate: automated checks, licence, secrets, publish-safety review, owner actions, dated evidence blocks | Release owner | `CURRENT` (dated blocks, refreshed 2026-10-11) | whole repo |
| `docs/examples/` | Example payloads | Synthetic fixtures used in docs, including `demo-jobs.json` (12 synthetic jobs imported by the one-command demo) | Contributors | `CURRENT` (2026-10-21) | — |

## 6. Execution plans

| Path | Document | Purpose | Audience | Validity | Related code |
|---|---|---|---|---|---|
| `docs/exec-plans/README.md` | ExecPlan protocol | Required sections, when a plan is needed | Agents | `CURRENT` | — |
| `docs/exec-plans/completed/0018-nowcoder-campus-source.md` | Completed plan 0018 | First China source: nowcoder campus SSR list, four-place registration, fixtures, controlled live smoke; merged at `cc9b333` after VERDICT PASS | Agents, maintainer | `COMPLETED` (2026-10-19; ASan 311/312 accepted with conditions) | `source_adapters/jobpilot_sources/providers/nowcoder.py` |
| `docs/exec-plans/completed/0019-ats-live-smoke-and-asan-timeout.md` | Completed plan 0019 | H-2 ATS live-smoke evidence + sanitizer-aware HTTP test timeout + ASan re-run; merged at `0c24c45` after VERDICT PASS | Agents, maintainer | `COMPLETED` (2026-10-19) | `source_adapters/`, `service/tests/`, `backend/tests/` |
| `docs/exec-plans/completed/0020-apply-url.md` | Completed plan 0020 | Nullable `jobs.apply_url`, import/source-sync/API plumbing, explicit-source-only extraction, additive upgrade + CI forward/backward test; merged at `c07c35a` after VERDICT PASS with conditions | Agents, maintainer | `COMPLETED` (2026-10-20) | `database/`, `service/`, `source_adapters/`, `tools/checks/` |
| `docs/exec-plans/completed/0021-docs-reconciliation.md` | Completed plan 0021 | `docs/03`–`docs/04` reconciliation of DB-1/2/3 and API-1/2/3/4; dated handoff corrections; docs-only L0; merged at `d75b14e` after VERDICT-0021-closeout PASS | Agents, maintainer | `COMPLETED` (2026-10-20) | `docs/03`, `docs/04`, `docs/CLAUDE_CODE_HANDOFF.md` |
| `docs/exec-plans/completed/0022-web-ui.md` | Completed plan 0022 | Minimal native Web UI: nginx static + same-origin proxy, auth, jobs/filter/match, detail/apply links, resume prerequisite, favourite and status board; Python fallback + Firefox smoke; merged at `38bd484` | Agents, maintainer | `COMPLETED` (2026-10-20) | `web/`, `docker/nginx/`, `tools/dev/web-smoke.sh` |
| `docs/exec-plans/completed/0023-release-readiness.md` | Completed plan 0023 | Release readiness: security/contributing docs, Python locks, MariaDB client image, TSan first run, libbcrypt provenance, benchmark refresh, release-checklist state; merged at `539202f` after the exclusive benchmark rework | Agents, maintainer, release reviewer | `COMPLETED` (2026-10-21) | `SECURITY.md`, `CONTRIBUTING.md`, `Dockerfile`, `backend/benchmarks/` |
| `docs/exec-plans/active/0025-readme-and-usage.md` | Active plan 0025 | External-facing English README plus a separate Chinese mirror, two real UI screenshots, one-command demo launcher/seed, 12-job fixture, docs index/state; L0 + shell smoke, no L4 | Agents, maintainer, release reviewer | `ACTIVE` (implementation on `docs/readme-demo`; closeout pending) | `README.md`, `README.zh-CN.md`, `tools/dev/demo-up.sh`, `tools/dev/demo-seed.sh` |
| `docs/exec-plans/completed/0001…0017A` | 20 archived plans | The history of every stage, each with its evidence, decisions and L4 results. Notable: `0014` (China sources + manual capture, 1264 lines), `0015`/`0015A` (tailoring, explainable matching, quality gate), `0016` (packaging, demo gate), `0017` (ops hardening, L4), `0017A` (the `published_at` contract gap) | Agents needing the "why" | `HISTORICAL` by definition, but each is the **authority for its stage's decisions** | the module named in each plan |

## 7. Ingestion, sources and tools

| Path | Document | Purpose | Audience | Validity | Related code |
|---|---|---|---|---|---|
| `source_adapters/README.md` | Source adapters (638 lines) | The four adapters, the crawl/compliance framework, **the source register with per-candidate verdicts**, and the Step 5 re-verification | Anyone touching sources | `CURRENT` | `source_adapters/jobpilot_sources/` |
| `docs/exec-plans/completed/0014-china-campus-sources-and-manual-capture.md` | 0014 plan | The China-source probe evidence, the bookmarklet, F13/F14 contracts | Anyone touching China/manual capture | `CURRENT` for its stage | `source_adapters/`, `tools/dev/capture-acceptance.sh` |
| `ai_worker/README.md` | AI worker (197 lines) | The optional Python worker: task claim, provider configuration, degradation without a key | Anyone touching AI tasks | `CURRENT` for the code; the live-provider path is unverified (see handoff §20) | `ai_worker/jobpilot_ai/` |
| `tools/job_report/README.md` | Read-only job report | The offline single-file HTML renderer and its deliberate limits | Users who want a report without a UI | `CURRENT` | `tools/job_report/` |
| `third_party/libbcrypt/PROVENANCE.md` | libbcrypt provenance | Upstream sources and licence statements of the vendored bcrypt | Maintainer | `CURRENT` (no upstream revision recorded — a known evidence gap) | `third_party/libbcrypt/` |

## 8. Historical documents (do not use for new decisions)

| Path | Why historical |
|---|---|
| `docs/00-project-overview.md` | Superseded overview |
| `docs/01-requirements.md` | Superseded requirements (V1.0) |
| `docs/02-system-architecture.md` | Superseded architecture (V3.0); describes modules that were never built (gateway, MQ) |
| `docs/05-development-plan.md` | Superseded plan; the roadmap it describes is complete through 0017 |
| `docs/07-deployment.md` | Superseded deployment guide (V1.0) |
| `docs/08-execution-guide.md` | Superseded 1596-line execution manual |
| `docs/project-structure.md` | Superseded structure doc |
| `docs/ai/references/project-state-full.md` | Historical snapshot |
| `docs/research/2026-10-08-open-source-job-search-agents.md` | Research note; inputs only |

## 9. CI and repository metadata

| Path | Document | Purpose | Validity | Related code |
|---|---|---|---|---|
| `.github/workflows/ci.yml` | CI workflow | `build` (release tree upload, trimmed) + `integration` (schema load, 312-case suite, coverage guard, Python suites) | `CURRENT` | — |
| `.github/workflows/image.yml` | Image workflow | Builds the committed `Dockerfile`, runs the demo gate end to end | `CURRENT` | `Dockerfile`, `tools/dev/demo-gate.sh` |
| `Dockerfile`, `docker-compose.yml`, `docker/mysql/init/` | Container definitions | The shipped image and the local stack | `CURRENT` | — |
| `CMakePresets.json`, `service/CMakePresets.json`, `service/conanfile.txt`, `service/conan.lock` | Build definitions | Presets (`debug`/`release`/`asan`/`ubsan`/`tsan`/`tsan-external`) and pinned Conan graph | `CURRENT` | build only |

## 10. Missing documents (MISSING)

| Path | Why it is needed | Suggested content |
|---|---|---|
| `CODE_OF_CONDUCT.md` | Optional for a small project; conventional for public repos | Standard contributor covenant; deferred until a reporting channel exists |

`SECURITY.md`, `CONTRIBUTING.md` and `docs/guides/local-demo.md` were added after
this section was first written; they are tracked in §1 and §4 and are no longer
missing.

## 11. Handoff documents added on 2026-10-11

| Path | Purpose |
|---|---|
| `docs/CLAUDE_CODE_HANDOFF.md` | The complete state, capability matrix, open problems, release blockers and the first-phase protocol for the next AI engineer |
| `docs/PROJECT_DOCUMENTATION_INDEX.md` | This index |

## 12. DOCUMENT_CONFLICT records

Recorded, not silently fixed. Each entry names what the document says, what the
code does, and what was done.

| ID | Document | What it says | What the code does | Status |
|---|---|---|---|---|
| DC-1 | `docs/ai/project-state.md` (early §) | At one point it described the bookmarklet publish/read-back acceptance as "still needs one real-machine JSON from the owner" | The real capture has been run end-to-end through the review path (`tools/dev/capture-acceptance.sh --expect-review`, 11 legs, `CAPTURE_ACCEPTANCE_OK`) | **Fixed 2026-10-11** in the same file (top banner). If older text in the body repeats the old claim, treat the dated banner as authoritative |
| DC-2 | `docs/04-api-design.md` visibility bullet | Said only the list/recommended surfaces were clean-only and that `needs_review` was reachable "only through the internal endpoints" | The by-id detail, AI match creation, resume tailoring and the application check also went through a status-only lookup and **did** return queued rows (HTTP 200) until `7ba62c8` | **Code fixed; contract clarified 2026-10-11** (`7ba62c8` + the docs/04 bullet now names every public read). No open conflict |
| DC-3 | `docs/RELEASE_CHECKLIST.md` §7 history row | Quoted `149 commits` / `absolute home path x6` when written | At the close-out commit the scanner reports `162 commits` / `x7` (`x14` RFC1918) | **Refreshed 2026-10-11** (`e9d75c2` and this handoff). Counts move with every commit; the row is dated and says so |
| DC-4 | `docs/02-system-architecture.md` (historical) | Presents gateway, RabbitMQ, notifications as part of the system | None of them exist; ADR-0004 marks the MQ as a future target and ADR-0007/0011 scope the MVP without them | **Known historical document** (Superseded banner). Do not "fix" it; use `docs/architecture/open-source-mvp.md` |
| DC-5 | `docs/05-development-plan.md` (historical) | Describes phases up to an open-ended roadmap | Every phase through 0020 is complete and archived; 0021 (docs/03–04 reconciliation) is active on `feat/0021-docs-reconciliation` | **Known historical document**. The live statement is `docs/ai/project-state.md` + the handoff |
| DC-6 | `docs/07-deployment.md` (historical) | Deployment instructions for the earlier shape of the project | The current run path is `README` → `tools/dev/bootstrap.sh` → `docker compose up`, and the image is built by `image.yml` | **Known historical document**; README is authoritative |
| DC-7 | `docs/quality/README.md` verification levels vs `AGENTS.md` | Both describe verification levels and the L4 rule | They agree in substance; `AGENTS.md` adds the P1 freeze and the L4 exclusivity rule | No conflict; read both |
| DC-8 | `source_adapters/README.md` source register | Marks candidates as "unverified" / "OUT" | No code registers any of those identifiers — `SUPPORTED_PROVIDERS` and the C++ allowlist still contain only greenhouse/lever/ashby | No conflict; **this is the correct state** (see handoff §10) |
| DC-9 | `docs/ai/project-state.md` §Verified Snapshot | Records "Opt-in live smoke: Greenhouse 217, Lever 10, Ashby 160" | No raw command/date/response evidence was found in the repository; handoff §5 row 2 and §20 V5 say no live board run is recorded | **Resolved by re-run 2026-10-19** (0019): dry-run produced Greenhouse 232, Lever 10, Ashby 167, all `board_complete=True`; raw output in `build/0019/ats-live-smoke.log`. The old 217/10/160 counts are historical/unverified, not constants |
| DC-10 | `docs/CLAUDE_CODE_HANDOFF.md` §12, `docs/03`, `docs/04` | Handoff counted 22 public + 8 internal; `docs/03` said 11 tables and its §3 was a stale snapshot; `docs/04` listed 2/9 internal routes, omitted `/hello`, used `{task_id}` and did not mark the 2 reserved AI routes | The live router has **36** registered routes = 27 non-internal (25 business + `/health` + dev-only `/hello`) + 9 internal; the schema has **13** tables; the two AI routes are not registered | **Fixed 2026-10-20** by 0021 (merged `d75b14e`, PR #4): `docs/03` §1/§2/§3, `docs/04` §7.2/§7.3/§9, dated corrections in the handoff, and a resolution note in `CURRENT_STATE.md`. Original handoff text is preserved |

## 13. How to keep this index true

- Add a row whenever a document is added, superseded or moved; the "Validity"
  column is the point of the file.
- When a document and the code disagree, add a `DC-n` row **before** changing
  either side, then fix the code or the document in a separate commit.
- Never mark a `HISTORICAL` document `CURRENT` without rewriting its content;
  never delete history to make the tree look tidy.
