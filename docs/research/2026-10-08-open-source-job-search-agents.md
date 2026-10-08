# Research note: two open-source AI job-search projects (2026-10-08)

Task level: L3 research. Read-only study; no product code changed by this note.

Sources (fetched 2026-10-08 from the remote host; `web_fetch` cannot resolve
GitHub in this environment, so the raw `api.github.com` + Contents API were
used):

- [career-ops-hq/career-ops](https://github.com/career-ops-hq/career-ops) —
  README.md, ARCHITECTURE.md, DATA_CONTRACT.md, `.agents/skills/career-ops/SKILL.md`,
  repository metadata and the full file tree.
- [MadsLorentzen/ai-job-search](https://github.com/MadsLorentzen/ai-job-search) —
  README.md, repository metadata and the full file tree.

Star counts and adoption claims below are what the projects report; they are not
independently verified.

## 1. What each project is

| | career-ops | ai-job-search |
|---|---|---|
| Purpose | "The open-source AI job search agent" — evaluate, tailor, track, locally | "The job search that runs on your machine" — Claude Code application framework |
| License / language | MIT / JavaScript (Node) + Go TUI | MIT / Python orchestration + Bun/TypeScript portal CLIs |
| Reported scale | ~73.8k stars, ~13.9k forks, created 2026-04-04 | ~45.3k stars, ~15.7k forks, created 2026-03-18 |
| Runtime model | Prompt modes (`modes/*.md`) executed by any AI coding CLI, plus ~200 standalone Node scripts | Claude Code slash commands + per-portal CLI skills |
| Storage | Markdown files canonical (`data/applications.md`), SQLite as a **derived** index | CSV/Markdown tracker + LaTeX sources |
| Explicit stance | Local-first, AI-agnostic, human-in-the-loop, never auto-submits | Same: draft-only, "you press Submit" |

Both are **local prompt-pack/CLI tools**, not services: there is no server, no
multi-user auth, and no database authority. JobPilotAI is a different genre
(C++ service + MySQL + Python workers), so the transferable material is
*design discipline*, not architecture.

## 2. Genuinely better ideas worth adopting

Ranked by value divided by effort for JobPilotAI. "Where" names the stage that
should own it.

### A. Per-requirement weight with recorded provenance — `career-ops`

career-ops scores each requirement's importance for a specific posting *and
labels where that weight came from* (the JD's own wording, the JD's structure,
or an estimate), and an estimated weight can never reach the top band.

Our `rule_v1` / `match_rule_v1` use **global** constants (skill 55%, title 15%,
salary 15%, location 10%). That is simpler, but it cannot express "this posting
says Kubernetes is mandatory and Redis is a nice-to-have". Adopting
per-requirement weights (still computed deterministically in C++) with a
`weight_source` field would materially improve explainability and honesty.
Effort: **S/M**. Where: 0013 result schema (amendment), reused by 0011 ranking.

### B. Model-output evaluation harness — `career-ops`

career-ops ships `evals/` plus `eval-golden.mjs`, `gemini-eval.mjs`,
`ollama-eval.mjs`, `openai-eval.mjs`, `openrouter-runner.mjs`: they test *AI
output quality* across providers, not just code paths.

We currently test plumbing (fixtures, schemas, degradation) and never the
prompt/output quality of the AI stage. A small golden set with recorded
provider responses and asserted properties (schema validity, no fabricated
facts, degradation on failure) closes a real gap. Effort: **M**. Where: 0013
Step 6.

### C. Untrusted-input discipline for postings — both projects

career-ops: postings are untrusted input, no embedded instructions are
followed, no links from the body are fetched, plus an SSRF/private-address
guard and `validate-untrusted-content-coverage.mjs`. ai-job-search: same
"follows no instructions embedded in postings" rule and a `robots_check.py`
gate.

Our worker will send job descriptions to an LLM. We need: (1) JD content passed
as data with an explicit never-follow rule, (2) provider `base_url` validation
that rejects loopback/private/metadata addresses unless explicitly opted in,
(3) tests that assert both. Effort: **S/M**. Where: 0013 Step 6.

### D. Fact-provenance verification instead of "please don't hallucinate" — both

career-ops blocks a generated CV whose numbers or facts appear in neither `cv.md`
nor the article digest (`--skip-fact-check` exists and is the escape hatch);
ai-job-search verifies every claim against the profile and enforces "reformulate,
never fabricate", plus an ATS text-layer check on the compiled PDF and
relevance-weighted CV cutting when the CV overflows.

This is the single most important 0014 design input: tailoring needs a
*mechanical* provenance gate (every generated sentence maps to a source fact)
and an ATS extraction check, not a prompt request. Effort: **M/L**. Where:
0014 (must-have, recorded here).

### E. Closed-loop outcome calibration — both

ai-job-search: `/outcome` archives the submitted CV, cover letter and posting
into `documents/applications/<company>_<role>/`, runs a follow-up cadence
(default 10 days, at most twice, drafts only, only claims from materials already
submitted), and points back to re-calibrate the fit framework from what actually
got interviews. career-ops: rejection-pattern analysis, per-ATS-channel advance
rates, repost detection.

We track applications but never learn from outcomes. Archiving the exact
submitted artifacts plus outcome statistics makes our ranking weights
improvable with evidence. Effort: **M**. Where: 0014 (and a small 0009
extension for artifact archival).

### F. Source-adapter contract and scaffolding — `ai-job-search`

Every portal is a self-contained skill folder with the same contract
(`search`/`detail`, `--format json|table|plain`, an `enabled:` flag, its own
offline tests, zero runtime deps where possible); `/add-portal` investigates a
board, scaffolds from that structure, live-tests it before registering, declines
auth-walled portals and warns about ToS.

Our `source_adapters/` already has fixture-driven tests; formalizing the adapter
contract, adding a machine-readable format mode, a "decline login-walled
sources" policy and an add-source scaffolder would make 0012 extensible.
Effort: **S/M**. Where: 0012 follow-up.

### G. Single-artifact offline report — `ai-job-search`

`/html-report` renders a self-contained HTML dashboard (inline SVG, no external
dependencies) from the tracker. Cheap to build over our MySQL data and high
demo value. Effort: **S/M**. Where: 0015.

### H. Open-source packaging discipline — `career-ops`

`SECURITY.md`, `CONTRIBUTING.md`, `CODEOWNERS`, SBOM workflow, dependency
review, plugin registry with schema validation, an explicit **system vs user
file contract** (`DATA_CONTRACT.md`) enforced by a test, a "no telemetry, no
phone home" guarantee, and a documented zero-cost/local-model path.

For 0015 these are the difference between "a repo" and "a project someone can
adopt safely". Effort: **M**. Where: 0015.

### I. Deadline and deal-breaker awareness — `ai-job-search`

`/rank` vetoes on deal-breakers, flags deadlines as urgency, marks dead postings
expired, and reports honest strengths and gaps. We have hard filters and
freshness but no application deadline or structured deal-breaker concept.
Effort: **M**. Where: backlog / 0014.

### J. Bulk, idempotent profile onboarding — `ai-job-search`

`/setup` Path A reads a populated `documents/` folder (CV, LinkedIn export,
diplomas, references, past applications) and is safe to re-run. Our profile and
resume flows are one-item-at-a-time. Effort: **M/L** (needs document text
extraction). Where: backlog.

## 3. Anti-patterns we should not copy

| Pattern | Why not for JobPilotAI |
|---|---|
| Files as the canonical store, DB as a derived index | Correct for a single-user local CLI; wrong for a service with concurrent users. MySQL stays the single authority; caches/indexes must remain disposable. |
| Business logic living in prompt/markdown files | Works because the runtime *is* the LLM CLI. Our scoring, validation and state transitions must stay typed C++ with tests. |
| Flat ~200-script repository root | They justify it with path stability for forks/plugins; it would destroy our module boundaries and review ability. |
| Playwright/Chromium and a Go TUI in the core | Heavy runtime dependencies. PDF/visual work, if any, belongs in an optional documented toolchain, not the core. |
| Auto-updater that rewrites the user's checkout | Upgrades should be `git pull` + additive migrations with a reviewed contract, as 0012/0013 already do. |
| Personal data written into repo-tracked files by setup | They warn loudly about public forks. We keep secrets in environment variables only and user data in MySQL. |
| Community scaffolding (manifesto, hired wall, 20 README translations, sponsorship) | Not a fit for this project; adds surface without engineering value. Keep `SECURITY.md`/`CONTRIBUTING.md` from H only. |
| In-process test discovery that imports suites and shares counters | They had to defend it repeatedly. Our per-binary CTest isolation is safer. |
| Auto-submitting applications | Both projects refuse; we already list it as a non-goal. |

## 4. Concrete amendments

### 0013 (active plan) — proposed, pending review

1. **Match result schema**: add per-requirement entries carrying a deterministic
   `weight` plus `weight_source` (`jd_explicit` | `structure` | `estimate`), and
   document that `estimate` alone cannot produce a top-band score. (Section 5.5,
   Step 2 extension.)
2. **Worker security step**: the job description is untrusted data; the prompt
   must state that embedded instructions are never followed and that links from
   the posting body are never fetched; provider `base_url` is validated to
   reject loopback/private/metadata addresses unless explicitly opted in; both
   have tests. (New in Step 6.)
3. **Golden eval set**: recorded provider responses plus asserted properties
   (schema validity, no fabricated facts, degradation path), runnable offline.
   (New in Step 6.)
4. **Worker may only cite input evidence**: suggestions/explanation must
   reference facts present in the input bundle; unreferenced claims are dropped.

### 0014 (resume tailoring) — requirements recorded now

- Mechanical fact-provenance gate over every generated claim.
- ATS text-layer extraction check on the rendered document.
- Relevance-weighted cutting with a documented scoring rule instead of
  truncating the oldest section.
- Archive the exact submitted CV/cover letter/posting per application.
- Follow-up cadence with caps and draft-only output.
- Outcome-driven calibration of matching weights.

### 0015 (open-source packaging) — requirements recorded now

- `SECURITY.md` + `CONTRIBUTING.md` + dependency review.
- Explicit shipped-file vs user-data boundary with an upgrade path that never
  writes user data.
- No-telemetry / no-phone-home guarantee, stated and testable.
- Adapter contract validation (registry-style) for third-party sources.
- Zero-cost local-model path documented.
- Optional single-file offline HTML report.

## 5. Caveats

- Both projects are AI-CLI prompt packs; their feature breadth comes from
  cheap markdown orchestration plus Node/Python glue, which is not available to
  a C++ service in the same way.
- Their star counts and "hired" claims are self-reported.
- Several of their behaviours (Playwright liveness, Gmail/Notion integrations,
  LaTeX PDF pipelines) depend on dependencies we deliberately exclude from the
  core.
