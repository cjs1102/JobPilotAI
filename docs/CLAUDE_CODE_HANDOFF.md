# JobPilotAI Claude Code Handoff

Written 2026-10-11 for the transfer of JobPilotAI to Claude Code (and to any
engineer or agent picking it up). Everything here was checked against the code at
that date; where a fact could not be verified it is marked **UNKNOWN** or
**NEEDS_VERIFICATION** instead of being guessed.

**Read `docs/PROJECT_DOCUMENTATION_INDEX.md` first for the document map, then this
file, then `AGENTS.md`, then the section you are about to touch.**

> **Dated corrections (2026-10-20, 0021).** This handoff was written 2026-10-11.
> The following statements are historical; live facts are in `docs/03` and
> `docs/04`:
>
> - **`apply_url`**: 0020 added `jobs.apply_url VARCHAR(512) NULL` end to end
>   (merged `c07c35a`); see §3 row 5, §5 row 5, §7 item 2, §9 migration debt,
>   §11, §21 and §22.
> - **ATS live smoke**: 0019 recorded 2026-10-19 Greenhouse **232**, Lever
>   **10**, Ashby **167**, all `board_complete=True`; see §5 row 2 and §20 V5.
> - **China source**: 0018 registered `nowcoder` (login-free
>   `/jobs/school/jobs`, `complete=false`, max 20 rows, no pagination); the
>   manual-only conclusion in §10 is historical. See `docs/04` §5.5.1.
> - **H-1**: the applications list already returns `company`; only
>   `source_url` is missing; see §5 row 16.
> - **Route count**: the live router has **36** registered routes = 27
>   non-internal (25 business + `/health` + dev-only `/hello`) + 9 internal;
>   see §12.
> - **Table name**: `skills`, not `user_skills` (§11; deferred to a later docs
>   cleanup).
>
> Original text is kept below; these dated corrections are authoritative.

---

## 1. 项目定位

A **local-first, self-hostable job-search assistant**, not a SaaS:

- Import or collect job postings (public boards via adapters; login-walled pages
  via a bookmarklet + a local publisher).
- Search, filter and read job details; every posting keeps its **original
  announcement URL** (`source_url`).
- Explainable matching against the user's profile/skills/resume (deterministic
  `rule_v1`, with reasons and deal-breakers).
- Save a job and track an application's state (favourite → applied → interview →
  offer/rejected).
- Jump to the original posting and apply **manually in the browser**. There is no
  submission path anywhere in the code, by design.

Stack: C++17 backend (epoll/Reactor, Controller→Service→DAO), MySQL 8, Redis
(sessions only), Python `source_adapters` (collect/publish) and an optional
`ai_worker`. Docker Compose for local deployment. No gateway, no message queue, no
Kubernetes.

## 2. 明确非目标

These are decisions, not gaps. Do not "complete" them:

- Multi-tenant SaaS, billing, RBAC, notifications (email/SMS/push).
- **Auto-apply / auto-submit / form filling** — enforced by
  `tools/checks/no_submission_path.py`; the check must keep exiting 0.
- Logging into job platforms, CAPTCHA solving, cookie/UA spoofing, proxy pools,
  TLS bypass. Login-walled content is **manual capture only** (ADR-0011).
- A general-purpose crawler, or reverse-engineering a site's request signing to
  reach its private API.
- Gateway / RabbitMQ / SaaS infrastructure (ADR-0004 keeps the MQ as a *future
  target only*).
- LLM wording polish of resumes (D6, deferred by the owner; `tailor_rule_v1` is
  the only generator).
- TSan/MSan in the CI matrix today (presets exist; see §13).

## 3. 当前 Git 基线

| Fact | Value |
|---|---|
| Branch | `main` |
| Handoff parent commit | `e9d75c2` (`docs(release): refresh the history scan count at the close-out commit`) |
| HEAD at handoff | the commit that adds this file (verify with `git log -1 --format=%H`; a document cannot contain its own sha) |
| `origin` | `git@github.com:cjs1102/JobPilotAI.git` (SSH) |
| Working tree before the handoff commit | clean, no untracked, no staged, no unpushed commit |
| Tracked files | 423 before this handoff; +2 with the handoff documents |
| Remote visibility | **private** (owner decision; do not change) |

The 20 most recent commits and every stage from 0001 to 0017A are in
`docs/exec-plans/completed/`. There is **no active plan** (`docs/exec-plans/active/`
is empty — verified).

## 4. 当前架构

```
source_adapters/ (Python)          ai_worker/ (Python, optional)
  providers: greenhouse/lever/ashby   claims tasks, calls the internal AI surface
  capture: bookmarklet + publisher
        │  POST /internal/jobs/source-sync         │ POST /internal/ai/tasks/claim
        │  POST /internal/jobs/import (manual)     │ POST /internal/ai/tasks/{id}/complete
        ▼                                          ▼
service/ (C++17)  Router → Controller → Service → DAO ──► MySQL 8
  backend/ (core: config, logger, epoll/Reactor, HTTP, MySQL pool, Redis client)
  sessions live in Redis (`session:<token>`), everything else in MySQL
tools/ (stdlib Python): job_report (offline HTML), checks (preflight, coverage
map, history scanner, no-submission-path), dev (bootstrap, demo gate, capture
acceptance, CI status)
```

- **P1 freeze**: `backend/{include,src}/jobpilot/{network,http}/` must not change
  without an approved plan and the `jobpilot-reactor-review` skill.
- API surface: 22 public routes + 8 internal token-protected routes
  (`service/src/router.cpp`). Details in `docs/04-api-design.md`.
- Internal routes are gated by `X-Internal-Token` (constant time); when
  `internalApi.token` is empty they are not registered at all.

## 5. 当前完整能力矩阵

Status vocabulary: `DONE` (works, verified) · `PARTIAL` · `FRAMEWORK_ONLY` ·
`NOT_IMPLEMENTED` · `BLOCKED` · `NEEDS_VERIFICATION` · `EXPLICIT_NON_GOAL`.

| # | Capability | Status | Code | API | Tests | Real verification | Gap / risk | Next |
|---|---|---|---|---|---|---|---|---|
| 1 | Job import (manual, idempotent) | DONE | `service/src/job/job_ingestion_*.cpp` | `POST /internal/jobs/import` | `test_job_ingestion_*` | demo gate 3 jobs; real capture 1 job | none known | — |
| 2 | Source adapters greenhouse/lever/ashby | **PARTIAL** | `source_adapters/jobpilot_sources/providers/*.py` | `POST /internal/jobs/source-sync` | Python suites + C++ `test_source_sync_*` | **fixture-driven only**; no live board run is recorded in the repo | live network behaviour unproven | optional live smoke run (see §20) |
| 3 | China sources | **BLOCKED / none registered** | register table in `source_adapters/README.md` | — | allowlist consistency tests | probes only (see §10) | compliance/environment | §10 + plan |
| 4 | `source_url` (announcement link) | DONE | `service/src/job/job_ingestion_dao.cpp` (`uk_source_url` upsert) | list + detail return it | ingestion/HTTP tests | demo gate + acceptance | — | — |
| 5 | `apply_url` (actual apply link) | **NOT_IMPLEMENTED** | — (`database/schema/03_job.sql` has no such column; `providers/public_page.py` only lists JSON-LD candidate keys) | — | — | — | product goal 3 is half-met | add column + payload + API + UI (`migrations` risk, see §11) |
| 6 | Job search / filters / sort | DONE | `service/src/job/job_controller.cpp`, `job_dao.cpp` | `GET /api/v1/jobs` | C++ suite | demo gate search leg | — | — |
| 7 | Job detail + requirements | DONE | `job_service.cpp`, `job_dao.cpp` | `GET /api/v1/jobs/{id}` | C++ suite | acceptance read-back | — | — |
| 8 | `needs_review` gate (intake quality) | DONE | `service/src/job/job_quality.cpp`, `job_visibility.h` | all public reads | `test_job_dao/http/quality` | real capture routed to review; 404 by id | — | — |
| 9 | Internal review surface | DONE | `internal_job_review_controller.cpp`, wired in `service/src/main.cpp` | `GET /internal/jobs/review-queue`, `POST /internal/jobs/review/{id}`, `POST .../review/backfill` | `test_job_review_http` (approve/fix/reject/queue/backfill) | demo gate review leg on the runner | real capture still `resolution=open` (human decision) | review the queued sample when desired |
| 10 | Resumes (upload/list/versions/text) | DONE | `service/src/resume/*` | `/api/v1/resumes*` | C++ suite | demo gate uses versions | — | — |
| 11 | Profile + skills | DONE | `service/src/user/*` | `/users/profile`, `/users/skills` | C++ suite | demo gate profile leg | — | — |
| 12 | Explainable matching (`rule_v1`) | DONE | `service/src/job/job_ranking*.cpp`, `match_rule.cpp` | `?with_match=true`, `/jobs/recommended`, `POST /api/v1/ai/match` | C++ suite | demo gate match leg (score + reasons) | — | — |
| 13 | AI task pipeline (optional worker) | DONE (deterministic path); **NEEDS_VERIFICATION** (live provider) | `service/src/ai/*`, `ai_worker/` | `/api/v1/ai/tasks/{id}` + internal task routes | C++ + Python suites (mocks) | no real LLM provider was ever exercised here | provider path unproven; degradation without key expected | verify with a local OpenAI-compatible stub if needed |
| 14 | Resume tailoring (rule draft + fact gate) | DONE | `service/src/resume/tailor_rule.cpp`, `resume_artifact_*.cpp` | `/api/v1/resumes/artifacts*` | C++ suite | demo gate tailor leg | D6 (LLM wording) is `EXPLICIT_NON_GOAL` for now | — |
| 15 | Favourite | DONE (as `applications.status = 0`) | `application_service.cpp` | `POST /api/v1/applications` with status 0 | C++ suite | — | no dedicated toggle endpoint; a UI must use the applications API | document it for the UI |
| 16 | Application tracking | DONE | `service/src/application/*` | `/api/v1/applications`, `/{id}/status`, `/statistics` | C++ suite | demo gate track leg | list lacks `company`/`source_url` | consider adding for the UI |
| 17 | Notifications | `EXPLICIT_NON_GOAL` | none | none | — | — | — | — |
| 18 | Redis sessions | DONE | `backend/src/cache/session_manager.cpp` (`session:<token>`), `service/src/auth/session_authenticator.cpp` | `Authorization` header | C++ suite | demo gate login | single-node Redis, no cluster | — |
| 19 | MySQL persistence | DONE | `database/schema/0*.sql`, DAOs | — | C++ suite | acceptance DB assertions | see §11 (no migration tool) | — |
| 20 | C++ HTTP service (epoll/Reactor) | DONE, **P1 frozen** | `backend/src/network/`, `backend/src/http/` | all routes | C++ suite incl. network tests | demo gate / acceptance | no static file serving (**NEEDS_VERIFICATION** for the UI phase) | confirm before the web UI |
| 21 | Python source framework (robots, SSRF guard, budgets) | DONE | `source_adapters/jobpilot_sources/{robots,fetch,normalizer,...}.py` | — | Python suites | fixtures | no live source registered | — |
| 22 | Bookmarklet manual capture | DONE | `source_adapters/jobpilot_sources/capture/`, `cli.py` | `publish-capture` → `/internal/jobs/import` | `test_capture_*` | real capture on a login-walled page (review path) | extraction is heuristic; data quality needs the review queue | — |
| 23 | HTML report | DONE | `tools/job_report/render_report.py` | consumes `GET /api/v1/jobs?with_match=true` | `tools/job_report/tests` | used on captured responses | read-only, no interaction | supersede with the UI later, keep as export |
| 24 | Web UI | **NOT_IMPLEMENTED** | none (no HTML templates/assets; only a test fixture `.html`) | — | — | — | the biggest product gap | plan §12 of the handoff |
| 25 | Docker / compose local deployment | DONE | `Dockerfile`, `docker-compose.yml`, `docker/mysql/init/` | — | image workflow | image job green on ubuntu:24.04 runner | base images must be pullable; this dev host cannot pull Docker Hub | — |
| 26 | CI (build + integration + image) | DONE | `.github/workflows/ci.yml`, `image.yml` | — | itself | green at `e9d75c2` (ci 4m59s, image 3m06s) | Actions minutes are owner-only | keep green |
| 27 | ASan | DONE historically (309/309, 0017 L4); **NEEDS_VERIFICATION** after `7ba62c8` | preset `asan` | — | ctest matrix | last full run predates the review-gate change | §20 | re-run |
| 28 | UBSan | same as ASan | preset `ubsan` | — | ctest matrix | same | §20 | re-run |
| 29 | TSan | presets `tsan`/`tsan-external` exist; **NEEDS_VERIFICATION** (not in the 0017 L4 matrix; last run unknown) | presets | — | — | no recorded run | racy-code regressions would go unnoticed | run once and record |
| 30 | Python tests | DONE | `source_adapters/jobpilot_sources/tests`, `ai_worker/jobpilot_ai/tests` | — | unittest | CI runs both | — | — |
| 31 | Release artifact (CI) | DONE (audited + trimmed) | `ci.yml` packaging step | — | — | full 28.8 MB artifact audited: 0 secrets/PII; now excludes build intermediates | 24 h retention; becomes public if the repo does | keep the trim |
| 32 | Licence (repo) | DONE (MIT) | `LICENSE`, `THIRD_PARTY_NOTICES.md` | — | preflight items 1–2 | each dependency's own licence file checked | MySQL client question (below) | owner decision |
| 33 | MySQL client licence | **PARTIAL** | `Dockerfile` (Oracle `libmysqlclient21`) | — | — | **MariaDB Connector/C alternative fully evidenced**: LGPL-2.1 for the client library (`Files: libmariadb/* → License: LGPL-2+`), unchanged build (`mysqlclient 3.3.19`), `NEEDED libmariadb.so.3`, and **312/312 C++ tests passed with MariaDB-linked binaries against MySQL 8.0** | the switch is **not applied**; the Oracle client remains in the shipped image | apply the 3-line switch + let the image job verify ubuntu:24.04 |
| 34 | SECURITY.md | **NOT_IMPLEMENTED** | — | — | — | — | needed before public | add |
| 35 | CONTRIBUTING.md | **NOT_IMPLEMENTED** | — | — | — | — | needed before public | add |
| 36 | GitHub metadata (About/Topics) | **BLOCKED (owner)** | — | — | — | `PATCH /repos/...` → 403 with the current token; verified copy in `RELEASE_CHECKLIST.md` §5 | needs account permission | owner applies 13-line copy |
| 37 | Branch protection | **NOT_IMPLEMENTED** (owner) | — | — | — | `protected: false` | no guard against force-push to `main` | owner enables |
| 38 | Secret / history disclosure | **PARTIAL** | — | — | preflight + `tools/checks/history_secret_scan.sh` | tree: 8 pass/0 fail; history: 162 commits, 0 credential shape | history still contains the old personal email, 7 home-path strings and 14 RFC1918 strings | owner accepts or decides on a rewrite (history rewrite is forbidden to agents) |

> **Correction 2026-10-20 (0021), row 5:** the `apply_url` row is historical.
> 0020 added `jobs.apply_url VARCHAR(512) NULL` end to end (schema,
> import/source-sync, list/recommended/detail; no index/backfill), merged at
> `c07c35a`. The row is preserved as the 2026-10-11 snapshot. See `docs/03` §3
> and `docs/04` §5.
>
> **Correction 2026-10-20 (0021), H-1 (row 16):** the applications list already
> returns `company`; only `source_url` is missing. This half of the row is
> historical; `docs/04` §6.2 is the live contract.
>
> **Correction 2026-10-20 (0021), row 2 (H-3):** the “no live board run” sentence
> is historical. 0019 recorded the 2026-10-19 dry-run: Greenhouse **232**,
> Lever **10**, Ashby **167**, all `board_complete=True`; raw log
> `build/0019/ats-live-smoke.log`.
>
> **Correction 2026-10-21 (0025), row 9:** the “real capture still
> `resolution=open`” sentence is historical. The 2026-10-21 check of the dev
> `jobpilot-mysql` database found **0** `job_review_items` (0 open), **0** jobs
> with `review_status <> 0`, and four jobs that are all DAO test fixtures
> (`source_url` `job-dao-*`), not the real capture. No real capture row exists
> in the dev database, so no review-row reject/delete action applied. A raw
> real-capture file at `$HOME/capture-acceptance.json` (6846 bytes, SHA-256
> `1a625a3be8a42008c8def2b31740551c6d12b3bf57bdb001400d6499162f58d0`) was
> found outside the repository and deleted on 2026-10-21; no remaining copy was
> found under the user home or `/tmp`. See `docs/ai/project-state.md`
> `## Completed Plan (0025)` and `build/0025/capture-retention-check.txt` (ignored).

## 6. 已完成的重要工作（有证据的）

- 0001–0017A: 20 archived stages, each with evidence in its ExecPlan.
- The 309→**312-case** C++ suite (gtest/CTest), reviewed by an assertion-target
  audit (`docs/quality/test-overlap-audit.md`, machine-guarded by
  `tools/checks/test_coverage_map.py`; the map was refreshed for the three cases
  added by the review-gate fix).
- `tools/dev/demo-gate.sh`: 9-leg end-to-end gate (register → profile → import →
  review surface → search → match → tailor → track) on an isolated compose
  project; run by CI's image job on every push.
- `tools/dev/capture-acceptance.sh`: the bookmarklet publish/read-back acceptance
  with **two documented paths** — a clean capture must become publicly visible, a
  capture the 0015A pre-flight routes to review must stay hidden from
  list/search/**and the by-id detail** (11 legs, `CAPTURE_ACCEPTANCE_OK`).
- `tools/dev/ci-status.sh`: read-only CI inspection (runs, per-job durations) via
  a fine-grained token kept outside the repo.
- `tools/checks/release_preflight.py`: 8 automated release checks + 5 manual owner
  items, including a `local-paths` check and a `secrets` check.
- `tools/checks/history_secret_scan.sh`: real history scan (the documented
  one-liners had a `\|` escaping bug that made them powerless).
- The 2026-10-11 review-gate fix (`7ba62c8`): `needs_review` rows are invisible to
  every public read (list, detail, AI match, tailoring, application check) and
  still readable by the review queue and the AI worker; verified by a negative
  control (the pre-fix image fails the new assertion) and by the real capture.
- The 0017 L4: debug/asan/ubsan each `100% tests passed, 0 failed out of 309`
  (before the review-gate change), sanitizer diagnostics 0, both Python suites
  green, review-backfill isolation `[ SKIPPED ]` on dev + `[ PASSED ] 7` on the
  isolated scratch DB.

## 7. 当前未完成工作

1. **No web UI** — the most visible product gap. All six user steps work at the
   API level only.
2. **`apply_url`** — the "actual apply link" half of product goal 3 does not exist.

   > **Correction 2026-10-20 (0021):** 0020 implemented this end to end (merged
   > `c07c35a`); this item is historical. See `docs/03` §3 and `docs/04` §5.

3. **China source** — none registered; see §10.
4. **MariaDB client switch** — evidence complete, change unapplied (§5 row 33).
5. **ASan/UBSan re-run after the review-gate change; TSan never recorded** (§5
   rows 27–29).
6. **SECURITY.md / CONTRIBUTING.md / docs/guides/** — missing.
7. **GitHub metadata, branch protection, PAT rotation, visibility** — owner side.
8. **The real capture sample is still `resolution=open`** in the review queue: a
   human should decide whether it becomes public (its `company` is the platform
   name and its `description` contains page chrome).

   > **Resolved 2026-10-21 (0025):** the current dev database contains no real
   > capture row and no open review item; the four remaining jobs are DAO test
   > fixtures. The raw real-capture JSON at
   > `$HOME/capture-acceptance.json` was deleted and no copy remains under the
   > user home or `/tmp`; no review-row reject/delete applied because no row
   > existed. Redacted acceptance logs remain as evidence. This item is closed
   > for the current environment; if a future real capture is imported, the
   > no-retention rule still requires an explicit review decision (reject keeps
   > the row for audit; deleting is a separate explicit action).

## 8. 已知 Bug

No confirmed, open functional bug is known at this baseline. For the record, the
defects found and fixed recently (each has a test or a gate that would catch a
regression):

| Bug | Found by | Fixed in | Guard |
|---|---|---|---|
| The review surface was never wired into the shipped binary (`/internal/jobs/review-queue` → 404) | the capture acceptance against the real binary | `8af793b` | new demo-gate leg (queue + read-only backfill) |
| `needs_review` rows were readable by id, matched, tailored and applicable | the capture acceptance + code trace | `7ba62c8` | DAO/HTTP/application tests + real capture asserting 404 |
| `published_at` was neither documented nor parsed by the manual import, so a manual row's `posted_at` stayed null | 0017 | `35563da` (0017A) | ingestion + HTTP round-trip tests |
| The documented history-scan one-liners used `\|` inside ERE (literal pipe) and could never match a key | a review of the checklist | `9258603` | scanner + synthetic positive/negative controls |
| A CI workflow `options: >-` scalar contained comment lines (docker read them as the image name → `invalid reference format`) | CI run `38131771058` | `1ec48e1` | CI itself |
| Service credentials via optional-secret expressions left the mysql service unhealthy on the runner | CI run `38132106588` | `63d33ae` | CI itself (literals restored) |

Known **limitations** (documented, not bugs): `deadline` is display-only (no
auto-close); the applications list has no company/`source_url`; no notification of
any kind; the bookmarklet's extraction is heuristic.

## 9. 技术债

| Debt | Impact | Suggested direction |
|---|---|---|
| **No migration tool** — `database/schema/0*.sql` are applied by hand; column additions are documented as `ALTER` snippets in `docs/03` §9.4 | a schema change is a manual, error-prone step for every environment | introduce a tiny versioned `migrations/` runner before the first real schema change (`apply_url`), or keep the documented ALTER practice and enforce it in CI |
| **The L4 harness lives in gitignored `build/l4-0017/`** | the stage gate cannot be reproduced from a fresh clone | move it into `tools/dev/l4-matrix.sh` when it is next touched |
| **No Python lock file** — `PyYAML>=6.0` only | builds are not bit-reproducible on the Python side | add `requirements.lock`/pins next to the two `pyproject.toml` |
| **`ai_worker` live provider never exercised** | the AI path is only proven against mocks | verify with a local OpenAI-compatible stub; record the result |
| **Source adapters never ran against a live board here** | unknown robustness of real HTML/JSON drift | one opt-in live smoke run, recorded |
| **TSan presets exist but are not in any matrix** | concurrency regressions escape | add TSan to the L4 (or a nightly) and record the first run |
| **Docs debt**: `docs/08-execution-guide.md` (1596 lines) and `docs/02` remain Superseded; `docs/04` is one 1348-line file | readers can mistake history for current state | the documentation index (`docs/PROJECT_DOCUMENTATION_INDEX.md`) is the mitigation; split `docs/04` only if it becomes a real editing burden |
| **No static-file serving in the C++ HTTP server** | the web UI needs a hosting decision | **NEEDS_VERIFICATION**: confirm the router has no static handler, then serve the UI from a separate static host (do not touch the frozen P1 layer) |
| **`libbcrypt` vendored without an upstream revision** | licence/provenance evidence gap | record the upstream commit on the next touch |
| **Single-host development**: this machine cannot pull Docker Hub base images | image work runs against cached bases | do image verification on the CI runner |

## 10. 国内来源现状

**No Chinese source is supported for automated collection.** Only greenhouse /
lever / ashby are registered (`SUPPORTED_PROVIDERS`, `providers.PROVIDERS`,
`service/src/job/source_observation.cpp:309-311`, `docs/04` §5.5 — a four-place
contract; a new id must be added in all four or every publish fails with 400).

From `source_adapters/README.md`'s register (probes run 2026-10-09, robots-first,
read-only, low volume, honest UA):

| Source | Entry | Login-free | Public API | robots | Auto-collect | Manual only | Detail URL | apply URL | Field completeness | Stability | Risk | Verdict |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 国聘 `www.iguopin.com` | SPA + `POST /api/jobs/v3/list` | yes (pages) | **no** — requires client signature (`Sign`/`T`/`Nonce` from a bundled secret) | all paths allowed | **no** | via bookmarklet | yes | not verified | unknown | unknown | signature = anti-automation control; bypassing it is forbidden | **OUT for automation** |
| Moka `app.mokahr.com` (S1 ATS tenants) | tenant roots | pages yes | unknown | tenant roots allowed (two specific paths disallowed) | **no (unverified)** | yes | tenant redirect loop unresolved | unknown | unknown | unknown | tenant list URL/API shape unverified (10-hop self-redirect) | **candidate, shape unverified** |
| 清华就业中心 `career.tsinghua.edu.cn` | announcements host | yes | no | `404` → allowed (no rules published) | **no** | bookmarklet | announcements only | no | n/a | n/a | the 招聘信息 list lives on `career.cic.tsinghua.edu.cn`, robots `503` → fail-closed | **not usable** |
| 南大 `job.nju.edu.cn` | JS SPA (management portal) | yes | unknown | SPA HTML → unparseable → allowed | **no** | bookmarklet | unknown | unknown | unknown | unknown | needs the school's public API | **unverified** |
| 牛客 `www.nowcoder.com` | `/search` disallowed; `/` logged-out shell | no (login markers) | no proven list | `Disallow: /search` | **no** | **yes (only channel)** | yes (as captured) | not verified | company often the platform name; description contains chrome (observed on the real sample) | unknown | robots + login wall | **manual only** |
| 北森 `www.zhiye.com` | — | — | — | TLS self-signed → fail-closed | no | — | — | — | — | — | certificate not bypassed | **OUT** |
| 北大 `job.pku.edu.cn`, 浙大 `career.zju.edu.cn` | — | — | — | DNS failure → fail-closed | no | — | — | — | — | — | unreachable from the probe host | **unverified** |
| Boss/智联/51job/猎聘/脉脉/拉勾/实习僧 | — | no | — | not probed | no | bookmarklet only | — | — | — | — | login-walled by design, ADR-0011 | **EXPLICIT OUT** |
| WeChat / APP referrals | — | — | — | — | no | no | — | — | — | — | no public web API | **EXPLICIT OUT** |
| RSS/Atom feeds | framework + fixtures | — | — | — | no | — | — | — | — | — | **FRAMEWORK_ONLY**, no live source | no source registered |

Machine-readable probe evidence:
`source_adapters/jobpilot_sources/tests/fixtures/cn_source_probe_2026-10-09.json`.

**Rule for the next stage**: pick **one** candidate, re-run the robots/terms check
from a current network, get written evidence of a login-free list, and only then
register the id in the four places. Do not claim support before that.

## 11. 数据库现状

13 base tables in `database/schema/0*.sql` (`users`, `profiles`, `user_skills`,
`resumes`, `resume_versions`, `jobs`, `job_requirements`, `applications`,
`ai_tasks`, `job_review_items`, …). Details: `docs/03-database-design.md`.

- `jobs` key columns: `source`, `source_url` (**`uk_source_url` idempotency
  key**), `status`, freshness (`last_seen_at`, `last_verified_at`,
  `source_status`, `closed_at`, `missing_count`), quality (`review_status`,
  `reviewed_at`, `review_note`), F13 deadline (`deadline`, `deadline_date_only`),
  `published_at`.
- **No `apply_url` column.**

  > **Correction 2026-10-20 (0021):** 0020 added
  > `jobs.apply_url VARCHAR(512) DEFAULT NULL` (no index, no backfill;
  > `source_url` remains the `uk_source_url` idempotency key), merged at
  > `c07c35a`. The statement above is the 2026-10-11 snapshot. The upgrade is
  > `database/upgrades/0020_jobs_apply_url.up.sql`; the CI test is
  > `tools/checks/test_schema_upgrade.sh`.

- `applications.status`: `0 favourite · 1 pending · 2 applied · 3/4/5 interview ·
  6 offer · 7 rejected`, with a transition machine in
  `service/src/application/application_service.cpp`.
- Migrations: there is **no migration tool** — the schema files are the source of
  truth and column additions have been applied as documented `ALTER` statements
  (`docs/03` §9.4). Adding `apply_url` must follow the same pattern (nullable, no
  backfill, no index) unless a migration tool is introduced first.
- The dev database `jobpilot` is used by the tests (they create and clean their own
  fixtures); the isolated compose projects use their own MySQL containers and
  volumes. Never point a test run at production data.

## 12. API 现状

`docs/04-api-design.md` is the contract. 22 public routes + 8 internal routes
(`service/src/router.cpp`):

> **Correction 2026-10-20 (0021):** the 22+8 count is historical and incorrect.
> The live router has **36 registered routes** = **27 non-internal** (25
> business public + `GET /health` + dev-only `POST /hello`) + **9 internal**.
> The two AI routes `resume/optimize` and `interview/generate` are reserved
> contract-only and are **not** registered (404). See `docs/04` §9.

- Public: users (register/login/me/profile/skills), resumes (+ versions, upload,
  artifacts), jobs (search/detail/recommended), applications (+ status,
  statistics), AI match/tasks.
- Internal (`X-Internal-Token`): `POST /internal/jobs/import`,
  `POST /internal/jobs/source-sync`, `GET /internal/jobs/review-queue`,
  `POST /internal/jobs/review/{id}`, `POST /internal/jobs/review/backfill`,
  `/internal/ai/tasks/{claim,complete,fail,input}`.
- Public visibility rule (2026-10-11): every public read returns clean rows only;
  a queued row is indistinguishable from a missing one (404 / code 40001). The
  rollback switch `JOB_PUBLIC_INCLUDE_NEEDS_REVIEW=true` restores the old
  visibility for public reads only.
- Error envelope: `{"code": <int>, "message": <string>}`; `40001` is
  job-not-found. Internal token failures are `401`.

## 13. 测试状态

| Suite | Where | Count | Last known result |
|---|---|---|---|
| C++ (gtest via CTest) | `service/tests`, `backend/tests` | **312** | CI `full test suite` green at `e9d75c2`; the same 312 passed locally with **MariaDB-linked binaries** against MySQL 8.0 (46.9 s) |
| Coverage-map guard | `tools/checks/test_coverage_map.py` | 312 ↔ map 312 | green |
| Python `source_adapters` | `source_adapters/jobpilot_sources/tests` | 446 tests (recorded in 0017) | green in CI |
| Python `ai_worker` | `ai_worker/jobpilot_ai/tests` | 190 tests (recorded in 0017) | green in CI |
| Demo gate (real binary) | `tools/dev/demo-gate.sh` | 9 legs | green (CI image job, runner) |
| Capture acceptance | `tools/dev/capture-acceptance.sh` | 11 legs | green on the review path; a clean sample path also green |
| Release preflight | `tools/checks/release_preflight.py` | 8 automated + 5 manual | 8 pass / 0 fail / 0 skip |
| History scan | `tools/checks/history_secret_scan.sh` | 162 commits | 0 credential-shape findings; info counters: 7 home paths, 14 RFC1918 |
| No-submission-path | `tools/checks/no_submission_path.py` | whole tree | exit 0 |
| ASan / UBSan | CMake presets | — | **NEEDS_VERIFICATION** after `7ba62c8` (last matrix: 309/309 pre-change) |
| TSan | preset `tsan` | — | **NEEDS_VERIFICATION** (no recorded run) |

Verification levels and when a suite may run: `docs/ai/agent-workflow.md` +
`docs/quality/README.md`. An L4 run is exclusive: no other build or E2E at the
same time.

## 14. CI 状态

- `.github/workflows/ci.yml`: `build` (configure/build the release preset, package
  a **trimmed** `release-build.tar.gz`, upload) → `integration` (download, extract,
  load the schema into a job-local MySQL, 312-case ctest, coverage guard, both
  Python suites).
- `.github/workflows/image.yml`: builds the committed `Dockerfile` on
  `ubuntu:24.04` and runs `tools/dev/demo-gate.sh` against it (this is what proves
  the shipped entry point wires the review surface).
- Latest verified runs at handoff time: `e9d75c2` → `ci` **success 4m59s**,
  `image` **success 3m06s**. `1ec48e1` and `63d33ae` also green.
- Read runs with `tools/dev/ci-status.sh` (needs `$GITHUB_TOKEN`/`$GH_TOKEN` or
  `~/.config/jobpilot/github-token`, 0600, never printed).
- The Actions billing endpoint is **not readable** with the current fine-grained
  token (403) — monthly usage remains an owner task.

## 15. Release 状态

Private repository, no GitHub Releases/tags. `docs/RELEASE_CHECKLIST.md` is the
gate and carries two dated evidence blocks (2026-10-11).

- **Passed with evidence**: preflight (8 pass/0 fail/0 skip/5 manual), actionlint
  1.7.7 real PASS, history scan, artifact audit (full download, sha256 matched,
  no secrets/PII; whitelist trimmed), CI logs disclosure check, licence table,
  review-gate fix, real-capture acceptance.
- **Executed but failed / then fixed**: the two CI workflow regressions recorded
  in §8.
- **Cannot be verified from here**: Actions minutes/storage, the private-repo
  billing page.
- **Owner decisions**: history disclosure acceptance, PAT rotation, About/Topics,
  visibility switch, branch protection.

## 16. License 状态

- The repository ships **MIT** (`LICENSE`, holder `JobPilotAI`).
- `THIRD_PARTY_NOTICES.md` lists every dependency with the licence read from the
  package itself: spdlog/fmt/yaml-cpp/nlohmann **MIT**, GoogleTest/hiredis
  **BSD-3-Clause**, PyYAML **MIT** (floor `>=6.0`, no lock), vendored `libbcrypt`
  **CC0/public domain** (no upstream revision recorded — evidence gap).
- **Open decision (not a legal conclusion)**: the committed `Dockerfile` links
  Oracle's `libmysqlclient21` (GPLv2 + Oracle FOSS License Exception). The
  alternative — MariaDB Connector/C, `libmariadb3`, whose *client library* is
  **LGPL-2.1-or-later** (`Files: libmariadb/* → License: LGPL-2+` in the package
  copyright) — has complete compatibility evidence (unchanged build with
  `mysqlclient 3.3.19`, `NEEDED libmariadb.so.3`, 312/312 tests against MySQL 8.0,
  11-leg acceptance + 9-leg gate on images that contain only `libmariadb3`) but the
  switch is **not applied**. Applying it is a 3-line change + CI verification.

## 17. Security 状态

- Working tree: **clean** (preflight `secrets` + `local-paths` pass over 423
  tracked files; the only secret-looking tracked names are `.env.example` with
  placeholders and the scanner's own name).
- History: **no credential shape** in 162 commits. It *does* retain: the earlier
  commit-author email, 7 `/home/<user>` strings, 14 RFC1918 addresses (already
  disclosed and accepted as a risk to decide on), and the intentional SSRF
  fixtures. History rewriting is **forbidden** to agents/CI.
- Secrets: never committed; tokens live outside the repo (`~/.config/jobpilot/`
  0600) and are never printed. CI uses an ephemeral job-scoped MySQL password
  (literals in the workflow by design; the job's containers die with the run;
  absent from the artifact).
- The shell history on the dev host had one line with a literal token value; it
  was redacted in place on 2026-10-11.
- No `SECURITY.md` yet (**missing**); no vulnerability disclosure channel.

## 18. GitHub 状态

| Item | State |
|---|---|
| Visibility | **private** (owner only; do not change) |
| Default branch | `main`, `protected: false` (owner should enable) |
| About / description / Topics | **empty / not applied** — copy verified and stored in `RELEASE_CHECKLIST.md` §5; the current token cannot PATCH repo metadata (403) |
| Actions | `ci` + `image` green at HEAD; artifact retention 24 h |
| Releases / tags | none |
| License detection on the repo page | reported as `Other / NOASSERTION`; the file itself is standard MIT — **UNKNOWN** why the private repo does not classify it (verify after publishing) |
| Collaborators / external contributions | none; there is no `CONTRIBUTING.md` or `SECURITY.md` yet |

## 19. 文档索引

See **`docs/PROJECT_DOCUMENTATION_INDEX.md`** — it lists every tracked document
with purpose, audience, validity (`CURRENT`/`HISTORICAL`/`STALE`/`MISSING`),
related code, and a `DOCUMENT_CONFLICT` table (8 entries, all resolved or
explicitly historical).

## 20. 待验证事项

| # | Item | Why it matters | How to verify |
|---|---|---|---|
| V1 | Does the C++ HTTP server serve static files? | decides how the web UI is hosted without touching the P1-frozen layer | inspect `service/src/router.cpp` + `backend/src/http/` (no static handler was found; confirm) |
| V2 | Moka tenant public list shape (and whether an apply URL is exposed) | the most promising compliant China candidate | robots-first, read-only probe with honest UA; record in the source register |
| V3 | Any login-free, robots-allowed 牛客 job-list path | 牛客 is where the real capture came from | robots-first probe; if none, 牛客 stays manual-only |
| V4 | `ai_worker` against a real OpenAI-compatible provider | the AI path is only mock-tested | run against a local stub (no key needed) and record the task lifecycle |
| V5 | Source adapters against a live board | fixture-only today | opt-in live smoke run (budgeted, honest UA) |
| V6 | TSan last run / first TSan run | concurrency safety is unproven at this commit | build the `tsan` preset and run the suite |
| V7 | ASan/UBSan after `7ba62c8` | the last matrix predates the review-gate change | run the L4 (debug/asan/ubsan) once, exclusively |
| V8 | Ubuntu 24.04 behaviour of the MariaDB switch | the switch has not been applied | apply it and let the `image` job build/verify on the runner |
| V9 | Why GitHub's licence detection says `Other` | cosmetic/legal clarity for the public page | check after the repository becomes public |

## 21. 发布阻塞事项

**Engineering blockers (can be closed here):**

1. ASan/UBSan re-run after the review-gate change (V7) — record the result.
2. MariaDB client switch application + CI verification (V8, §16) — closes the last
   licence uncertainty of the shipped artifact.
3. `SECURITY.md` + `CONTRIBUTING.md` (missing; conventional for a public repo).
4. TSan first run recorded (V6) — not strictly blocking, but an unmeasured
   concurrency claim is a risk for a Reactor backend.

**Owner-only blockers (an agent must not do these):**

5. Accept (or decide about) the history disclosure: old author email, 7 home
   paths, 14 RFC1918 strings (§17). History rewriting is forbidden to agents.
6. Revoke/rotate the previously used personal token.
7. Apply About/description/Topics (13-line copy ready) — the current credential
   gets 403.
8. Enable branch protection on `main` (no force-push, CI required).
9. Switch visibility to public **only after** 5–8 are settled.

**Not blockers**: the `needs_review` sample awaiting a human decision; missing web
UI; missing China source; Python lock file; TSan; the `apply_url` gap.

> **Correction 2026-10-20 (0021):** the `apply_url` gap is closed by 0020
> (merged `c07c35a`); the other items remain as written.

## 22. 建议开发顺序

1. **V7 + V8** (verification/one small change): re-run the L4 matrix; apply the
   MariaDB switch and let CI verify it. Closes two blockers cheaply and proves the
   pipeline still works from a fresh clone.
2. **Handoff verification protocol** (§23) and `CURRENT_STATE.md` — no code.
3. **`apply_url` end to end** (small schema change + payload + API + rendering):
   the only product goal that is completely absent; do it before the UI so the UI
   has it to show.

   > **Correction 2026-10-20 (0021):** completed by 0020 (merged `c07c35a`).
   > The next product step is the minimal Web UI; it remains owner-specified.

4. **Minimal local web UI** (list + filters + explainable match + detail with
   "open original page" + favourite + application status board):
   - host it independently (V1 decides), consume the existing API, no new server
     business logic, no submission endpoint;
   - this is the natural moment for `docs/guides/local-demo.md`.
5. **One China source evaluation** (§10 rule): choose Moka or an RSS/sitemap
   source, verify robots/terms, then register in all four places.
6. **Publication hygiene**: `SECURITY.md`, `CONTRIBUTING.md`, branch protection,
   About/Topics, visibility — owner-driven.

## 23. Claude Code 接手后的第一阶段要求

**Do not write code first.** The first phase is a verification phase with a
written output, and it exists because this repository's history already contains
several cases of documents drifting from the code (see the index's
`DOCUMENT_CONFLICT` rows).

Required order:

1. Read `README.md`.
2. Read `AGENTS.md` (hard constraints, P1 freeze, verification policy).
3. Read `docs/CLAUDE_CODE_HANDOFF.md` (this file).
4. Read `docs/PROJECT_DOCUMENTATION_INDEX.md`.
5. Read `docs/ai/README.md`, `docs/ai/agent-workflow.md`, `docs/ai/project-state.md`.
6. Read `docs/exec-plans/README.md` and skim the completed plans (at least 0014,
   0015/0015A, 0016, 0017/0017A).
7. Check Git: `git status -sb`, `git log --oneline -10`,
   `git rev-parse HEAD origin/main` — the working tree must be clean and
   `HEAD == origin/main`.
8. Check CI with `tools/dev/ci-status.sh` — the newest `ci` and `image` runs on
   `main` must be green; record run ids and per-job durations.
9. Check code against documents: at minimum `docs/04` vs `service/src/router.cpp`,
   `docs/03` vs `database/schema/*.sql`, README's capability list vs the code.
10. Re-verify the key facts below (each must be checked, not assumed):
    - `git ls-files | wc -l` and `git status --porcelain` (clean),
    - `ctest --test-dir build/presets/debug -N | tail -1` → **312**,
    - `python3 tools/checks/test_coverage_map.py --test-dir build/presets/debug --map docs/quality/test-merge-map.json` → PASS,
    - `python3 tools/checks/release_preflight.py` → 8 pass / 0 fail,
    - `tools/checks/history_secret_scan.sh` → 0 credential-shape findings,
    - `python3 tools/checks/no_submission_path.py` → exit 0,
    - `grep -rn 'apply_url' database/ service/` → no column/key,
    - the source allowlists (Python `SUPPORTED_PROVIDERS`/`PROVIDERS` and C++
      `IsSupportedObservationSource`) contain exactly greenhouse/lever/ashby.

Then produce, **before any implementation**:

- **`CURRENT_STATE.md`** — what is actually true now: git/CI facts, verified
  capability status per §5, conflicts found between docs and code, items it could
  not verify (with the exact command that failed or was unavailable), and the
  delta against this handoff.
- **`EXEC_PLAN.md`** — the proposed next task: goal, non-goals, files to touch,
  dependencies and risks, acceptance criteria with verification commands, what the
  user can do afterwards, and whether a schema migration is involved and its risk.

**Wait for explicit user approval of `EXEC_PLAN.md` before changing code.** While
working: never rewrite history, never force-push, never change repository
visibility, never commit a secret, never introduce a submission path, and keep the
P1 network layer frozen unless an approved plan says otherwise.
