# JobPilotAI Current Project State

> Snapshot: 2026-10-10 (0017 closed)
>
> Baseline HEAD: `b2a55be docs(quality): redact the development host address in
> the overlap audit`; the 0017 close-out documentation commit follows it and
> carries the stage's L4 evidence (`## Completed Plan (0017)`).
>
> 0010 Job Ingestion, the Architecture Re-Research, 0011 Job Search / Ranking,
> 0012 Steps 1-8 (freshness schema, source observation domain, Source Sync
> DAO/Service/API, freshness search, Python ATS adapters, end-to-end
> integration), 0013 AI Matching, 0014 China Campus Sources + Manual Capture, the
> **0015A** job-list/intake hardening, **0015** resume tailoring and explainable
> matching, and **0016** open-source packaging are complete and archived;
> **0017 (ops hardening and acceptance closure)** is complete and archived too,
> with its single L4 green, so **no plan is active** and the recorded MVP roadmap
> is complete through packaging. Carried forward: 0014's Step 5 stays
> environment-limited, and the bookmarklet's publish/read-back acceptance has now
> been run **on a real capture sample** (`tools/dev/capture-acceptance.sh
> --expect-review`: 11 assertions green on an isolated stack, the sample routed to
> the review queue exactly as the 0015A pre-flight demands and never exposed on the
> public detail/list/search), so no owner-run is outstanding for that step; 0015's
> D6 stays deferred by owner decision, 0017 found one real contract gap
> (`publish-capture` sends `published_at`, but `/internal/jobs/import` neither
> documented nor parsed it) which **0017A fixed on 2026-10-10** — see
> `## Completed Plan (0017)` and `completed/0017A-manual-import-published-at.md` —
> and the repository is
> still **private**: making it public, filling About/description and rotating the
> old personal access token are owner actions listed in
> [`RELEASE_CHECKLIST.md`](../RELEASE_CHECKLIST.md).

> **2026-10-21 update — 0019–0023 merged.** The Claude Code takeover
> verification is recorded in [`CURRENT_STATE.md`](CURRENT_STATE.md). 0018
> (`nowcoder`) merged at `cc9b333` (PR #1); 0019 (ATS live smoke + sanitizer
> timeout) merged at `0c24c45` (PR #2) after VERDICT-0019 PASS with conditions;
> 0020 (`apply_url` end to end) merged at `c07c35a` (PR #3, merge commit) after
> VERDICT-0020-closeout PASS with conditions; 0021 (docs/03–04 reconciliation)
> merged at `d75b14e` (PR #4, merge commit) after VERDICT-0021-closeout PASS
> with conditions; 0022 (minimal local Web UI) merged at `38bd484` (PR #5,
> merge commit) after VERDICT-0022-closeout PASS with conditions; 0023 (release
> readiness) merged at `539202f` (PR #6, merge commit) after
> VERDICT-0023-closeout PASS with the benchmark-rework completion. See
> `## Completed Plan (0020)`, `## Completed Plan (0021)`,
> `## Completed Plan (0022)` and `## Completed Plan (0023)`.
> Main CI after the 0023 merge: `ci` `39462589981` (11m02s; build 4m02s +
> suite 1m50s) and `image` `39462589965` (2m44s; container 2m39s), both
> **completed/success**; the PR image job `39461251212` also passed
> `Verify the MariaDB client linkage` (`MARIA_DB_LINK_OK`) and the demo gate.
> The 0022 main CI was `ci` `39446877179` (5m58s; build 4m26s + suite 1m24s)
> and `image` `39446876743` (2m58s; container 2m53s). The 0019 ASan
> re-run is **312/312** with 0 sanitizer diagnostics (one isolated
> review-backfill SKIP); L4 Debug/ASan/UBSan each **312/312**. The 2026-10-19
> ATS snapshot is **232 / 10 / 167**; the old 217/10/160 is
> historical/unverified and is **not** a constant or a coverage promise.
> TSan is not covered. See `## Completed Plan (0018)` and
> `## Completed Plan (0019)`.
> Next stage: **0024 owner publication** (owner-only; no worker implementation).
> The release-readiness work is merged; A history disclosure remains
> accepted/no-rewrite. Wait for the owner’s 0024 instruction; do not start it.
>
> **2026-10-21 update — 0025 implementation in progress.** The plan-only PR #7
> merged at `7acc58f`; the docs/tooling implementation is on branch
> `docs/readme-demo` and is **not merged yet**. The README rewrite, screenshots,
> one-command demo and the capture no-retention check are recorded in
> `## Active Plan (0025)` below. 0024 remains owner-only and has not started.

Repository files and Git history are the cross-agent fact source. This concise
state plus the active or most recent ExecPlan should be enough for a new Agent to
continue without asking the user to replay project history.

This is the concise takeover entry. Detailed evidence and historical sections
are preserved in
[references/project-state-full.md](references/project-state-full.md).

## Completed Plan (0018)

[`completed/0018-nowcoder-campus-source.md`](../exec-plans/completed/0018-nowcoder-campus-source.md)
— approved 2026-10-19, **merged at `cc9b333` via PR #1 (merge commit) after
VERDICT-0018 PASS with conditions**. Summary:

- Added the `nowcoder` provider: login-free SSR list at
  `/jobs/school/jobs`, recursive `jobListData` lookup (no hardcoded module
  number), native + aggregator record mapping, row-level diagnostics,
  in-batch dedupe, 48 KiB cap, and **always `complete=false`** (max 20 rows,
  no pagination, order jitter).
- Registered `nowcoder` in the four places: Python `SUPPORTED_PROVIDERS`,
  `PROVIDERS`, CLI choices (derived), and C++ `IsSupportedObservationSource`;
  `docs/04` §5.5.1 records the contract.
- Evidence: Python `Ran 479 tests ... OK`; C++ Debug/UBSan `312/312`;
  live dry-run `20 job(s), complete=False`; isolated publish 19 rows (single
  board) and 12 rows (second board, 7 s for two boards); review queue 0.
- L4 (project `jobpilot-l4-0018`, 13418/16418, scratch
  `jobpilot_l4_0018_scratch`): builds all rc=0; Debug 312/312, UBSan 312/312,
  **ASan 311/312** with one `registration failed` timeout and 0 sanitizer
  diagnostics. Targeted ASan re-runs of both affected tests pass alone; a full
  ASan re-run fails a different registration-heavy test. Classified as a
  harness/3 s-client-timeout issue, not a 0018 product regression. **VERDICT-0018
  accepted 311/312 as harness/environment-limited** with conditions: never write
  "ASan all green"; 0019 must add a sanitizer-aware HTTP test-client timeout and
  re-run ASan; keep the residual uncertainty (server-side bcrypt duration at the
  failure instant was not instrumented).
- Main CI after the merge: `ci` `39200537331` completed/success 5m36s
  (`clean build` 4m08s + `full test suite` 1m21s); `image` `39200537315`
  completed/success 2m51s (`container image and the end-to-end gate` 2m47s).
- History disclosure: the history scanner now reports **home path x10**
  (was x7) because the 0018 docs commits added strings; the current tree is
  clean and there are still 0 credential-shape findings / 14 RFC1918 strings.
  No history rewrite was performed; disposition is an owner decision, recorded
  here for 0019.
- The pre-protocol docs commit `a351984` is part of the merge history; no
  history rewrite, no force-push.

## Completed Plan (0019)

[`completed/0019-ats-live-smoke-and-asan-timeout.md`](../exec-plans/completed/0019-ats-live-smoke-and-asan-timeout.md)
— **merged at `0c24c45` via PR #2 (merge commit) after VERDICT-0019 PASS with
conditions**. Scope: H-2 ATS live-smoke evidence + sanitizer-aware HTTP test
timeout + ASan re-run.

- ATS live smoke 2026-10-19 (dry-run, no writes): Greenhouse **232**, Lever
  **10**, Ashby **167**, all `board_complete=True`; log
  `build/0019/ats-live-smoke.log`. The old 217/10/160 is historical/unverified,
  not a constant or coverage promise.
- Timeout helper applied to all 10 HTTP test clients; no assertion lines
  changed. ASan/TSan get 5x; GCC has no UBSan macro, so UBSan keeps 3 s and
  passed. TSan is not covered.
- ASan re-run **312/312** in 108.02 s, 0 sanitizer diagnostics (one isolated
  review-backfill SKIP); L4 Debug/ASan/UBSan each **312/312** (91.69 / 101.06 /
  63.76 s), builds all rc=0, Python 479 + 190 OK, review isolation dev SKIP /
  scratch 7, stack `jobpilot-l4-0019` torn down.
- Guard evidence: preflight 8/0/0 (actionlint 1.7.7); coverage 312→312 PASS;
  no-submission rc=0; history 172 commits, 0 credential-shape (home path ×10,
  RFC1918 ×14); `git diff --check` clean.
- PR #2 `ci` run `39203694811` completed/success 5m21s (build 3m57s + suite
  1m18s); `image` not triggered on the PR (no image-path change). Main CI after
  the merge: `ci` `39205581874` completed/success 5m32s (build 4m02s + suite
  1m23s); `image` `39205581917` completed/success 3m06s (container 3m01s).
- Next stage: **owner-specified**; the worker must not start one.

## Completed Plan (0020)

[`completed/0020-apply-url.md`](../exec-plans/completed/0020-apply-url.md) —
**merged at `c07c35a` via PR #3 (merge commit) after VERDICT-0020-closeout PASS
with conditions**. Scope:
nullable `jobs.apply_url`, import/source-sync plumbing (COALESCE), public
list/recommended/detail field, explicit-source-only extraction, and the
documented additive upgrade + CI forward/backward test (Option B; no migration
runner). Source coverage: Lever/Ashby `applyUrl`; nowcoder native
`redirectExternalUrl` only when explicit; Greenhouse and nowcoder aggregator
NULL; manual import accepts the field but the bookmarklet does not extract it.
Implementation is complete on `feat/0020-apply-url`: the upgrade pair passes
forward/backward/re-forward/idempotent on a scratch DB; targeted C++ tests are
72/72; full Python is 481 OK; CTest remains 312. The stage L4 (project
`jobpilot-l4-0020`, MySQL 14018 / Redis 17018, scratch
`jobpilot_l4_0020_scratch`, 14:12–14:28) is **green**: Debug/ASan/UBSan each
**312/312**, 0 sanitizer diagnostics, builds all rc=0, Python 481 + 190 OK,
review isolation dev SKIP / scratch 7, teardown complete. The dev database has
the upgrade applied. Final head `e01e283`.
- PR CI `39374045453` completed/success 3m56s (build 2m23s + suite 1m26s);
  the `Schema upgrade forward/backward test` step completed/success; `image`
  was not triggered on the PR (no image-path change), which is expected.
- Main CI after the merge: `ci` `39375032387` completed/success 4m09s
  (build 2m38s + suite 1m24s); `image` `39375032398` completed/success 2m48s
  (container 2m44s). Both green.
- Known non-blocking test-environment behavior (VERDICT-0020-closeout
  condition 3): DB tests run without `MYSQL_PASSWORD` fail as a SetUp
  exception/SEGFAULT instead of a readable SKIP. This is recorded for later
  maintenance and is not a 0020 defect.
- Next stage: **0022 minimal Web UI** (owner route B), approved by
  `VERDICT-0022-plan`; see `## Active Plan (0022)`. Implementation is
  UI/compose/docs only; no P1 or backend change.

## Completed Plan (0021)

[`completed/0021-docs-reconciliation.md`](../exec-plans/completed/0021-docs-reconciliation.md)
— **merged at `d75b14e` via PR #4 (merge commit) after VERDICT-0021-closeout
PASS with conditions**. Final head `940ad4c`. Scope: reconcile `docs/03`
§1/§2/§3 (13 tables, 0015A columns/indexes, full `job_review_items` /
`resume_artifacts` DDL) and `docs/04` §7.2/§7.3/§9 (2 reserved AI routes,
9 internal routes, dev-only `/hello`, `{task_id}`→`{id}`, 36 = 27 + 9 count);
add dated handoff corrections and a `CURRENT_STATE` resolution note. Evidence:
schema 13/13 and all columns/indexes match, router 36 = 27 + 9 with only the 2
reserved routes extra, preflight **7 pass / 0 fail / 1 skip** (actionlint
unavailable), `git diff --check` rc=0. **Docs-only L0**: no code, schema,
migration, API behavior or CI change; no L4 and no reuse of 0020 test evidence.
Conditions: DB-4/H-2 (`user_skills`) deferred; permanent docs checker is a
follow-up candidate; next stage is owner-specified.

## Completed Plan (0022)

[`completed/0022-web-ui.md`](../exec-plans/completed/0022-web-ui.md) —
**merged at `38bd484` via PR #5 (merge commit) after VERDICT-0022-closeout PASS
with conditions**. Final head `bab3ece`; code head `2abbaa1`. Scope: a native
HTML/CSS/JS UI served by nginx
(`nginx:1.27-alpine`) with a same-origin `/api`/`/health` reverse proxy; login/
register, job list/filters, `rule_v1` match explanations, recommended view,
detail with `apply_url`/`source_url` links, minimal resume setup (required by
`POST /applications`), favourite and application status board. No P1, backend,
schema, source or submission-path change. The `web` compose profile is opt-in;
the Python stdlib static+proxy runner is the fallback for hosts without the
nginx image. Verification: isolated compose smoke through the real nginx
config (or Python fallback), `no_submission_path.py`, `innerHTML`/CDN/link
checks, preflight, and one L4 at stage close.

Implementation is committed at `2abbaa1`: `web/` + `docker/nginx/default.conf`
+ opt-in compose profile + `tools/dev/web-smoke.sh` / Python fallback +
optional Firefox/geckodriver UI smoke + README/guide updates. Evidence:
`web-smoke.sh` passed through both the Python fallback and a real nginx binary,
including `BROWSER_SMOKE_OK` (login → resume → search/detail → favourite →
status board) and isolated teardown (`WEB_SMOKE_TEARDOWN_OK`); L4 Debug/ASan/
UBSan each **312/312** (one known review-backfill SKIP), Python 481 + 190 OK,
backfill scratch 7 passed, 0 sanitizer diagnostics; preflight 7/0/1 and
`git diff --check` rc=0. PR CI `ci` `39445774298` (6m19s) included the new
`Web UI smoke (Python fallback)` step as success; `image` `39445774360` was
green (2m40s). Main CI after the merge: `ci` `39446877179` (5m58s; build 4m26s
+ suite 1m24s) and `image` `39446876743` (2m58s; container 2m53s), both green.
Conditions: nginx container path remains environment-limited (digest recorded;
1.24.0 binary + Firefox UI smoke used); next stage is owner-specified 0023.

## Completed Plan (0023)

[`completed/0023-release-readiness.md`](../exec-plans/completed/0023-release-readiness.md)
— **merged at `539202f` via PR #6 (merge commit) after VERDICT-0023-closeout
PASS with the benchmark-rework condition completed**. Final head `b6eaaa6`;
code head `933968a`. Scope: SECURITY/CONTRIBUTING (CoC deferred), hash-pinned
Python locks, MariaDB Connector/C image switch, TSan first run, libbcrypt
provenance, benchmark refresh, release-checklist/docs updates. No P1 or
business API change. Evidence: Python lock install `--require-hashes` + 481/190
suites; TSan 52/52 + 17/17, 0 diagnostics (test-harness CMake launcher fix
`3ea3bc5`); benchmark matrix and microbench **exclusively re-run 2026-10-21
00:38:57–00:48:41** (health 13.4–16.1k req/s, jobs 180–194 req/s; 2026-10-06
32.7k health historical, ~2.4× difference and environment/load caveats
documented in `docs/benchmarks/README.md`; raw
`build/benchmarks-0023-exclusive/`); L4 Debug/ASan/UBSan each **312/312** (one
known review-backfill SKIP), scratch backfill 7 passed, 0 sanitizer diagnostics;
libbcrypt upstream unresolved with SHA-256 manifest. PR image `39461251212`
passed `Verify the MariaDB client linkage` (`MARIA_DB_LINK_OK`) + demo gate;
main CI after merge is green (`ci` `39462589981`, `image` `39462589965`).

## Active Plan (0025)

[`active/0025-readme-and-usage.md`](../exec-plans/active/0025-readme-and-usage.md)
— plan-only PR #7 merged at `7acc58f`; implementation is in progress on branch
`docs/readme-demo`, **not yet reviewed or merged**. Scope: external-facing
English README + separate `README.zh-CN.md`, two UI screenshots, one-command
`tools/dev/demo-up.sh`/`demo-seed.sh`, 12-job fixture, docs index/state. This is
docs + developer tooling only: no core/API/schema/CI/P1 change; L0 + shell smoke,
no L4.

Evidence collected on the branch (2026-10-21):

- **Screenshots**: `docs/images/web-detail.png` and `docs/images/web-status.png`,
  both 1366×682 PNG. The planned `/tmp/0022-browser-final` originals were no
  longer present, so they were regenerated with
  `tools/dev/web-smoke.sh --browser firefox` (Python proxy fallback); the exact
  original images are not recoverable. Both READMEs reference the new files.
- **README**: `README.md` is English and `README.zh-CN.md` is a separate Chinese
  mirror; both have the language switch, two screenshots, one-command Quick
  Start, demo flow, usage, verification, limits and community links. Stage
  numbers are absent from both bodies.
- **Demo smoke** (isolated `jobpilot-demo-0025`; env
  `/tmp/jobpilot-demo-0025.env`, later removed; ports 18180/18181/13326/16399;
  backend image `jobpilot-backend:verify`, `DEMO_BUILD_BACKEND=0`):
  `demo-up.sh` produced `DEMO_READY url=http://127.0.0.1:18181`, used the
  Python proxy fallback because `nginx:1.27-alpine` was unavailable, and seeded
  `total=12 created=12`. A second run preserved the `.env` byte-for-byte
  (SHA-256 unchanged) and seeded `total=12 created=0 updated=12`; the page and
  `GET /api/v1/jobs?page=1&page_size=20` remained reachable with `total=12`.
  A `C++` keyword request returned 5 jobs. The generated `.env` was mode `0600`;
  none of its generated secret values appeared in the launcher log.
  `docker compose ... down -v` removed all containers, volumes and the network;
  the temporary env file was deleted. Raw logs: `build/0025/` (ignored).
- **Capture sample / no retention**: the 2026-10-21 query against the dev
  `jobpilot-mysql` returned `jobs_total=4`, `jobs_review_status_nonzero=0`,
  `review_items_total=0`, `review_items_open=0`. All four jobs are DAO test
  fixtures (`source_url` `job-dao-*`, source `manual`, `review_status=0`), not
  a real capture. No real capture row exists in the dev database, so no
  review-row reject/delete action applied. A raw real-capture file at
  `$HOME/capture-acceptance.json` (6846 bytes, SHA-256
  `1a625a3be8a42008c8def2b31740551c6d12b3bf57bdb001400d6499162f58d0`) was
  found outside the repository and was **deleted on 2026-10-21**; a search of
  the user home and `/tmp` found no remaining copy. Redacted acceptance logs under
  `build/0017a/` and `build/capture-0017/` remain as evidence and contain no raw
  payload. The old handoff statement that the real sample is still
  `resolution=open` now carries dated corrections in §5 row 9 and §7 item 8.
  Raw DB output: `build/0025/capture-retention-check.txt` (ignored).

Remaining for this branch: closeout request, owner-created PR/merge, and the
post-merge main CI verification. Do not archive the plan or mark this stage
completed until the owner merges it.

## Current Stage

- Architecture direction is now open-source first and self-hosted, with a
  future path to SaaS only through separately approved decisions.
- P0 config, logging, and schema are complete.
- P1 network/HTTP remains behaviorally frozen after the minimum correctness
  gate, with one approved exception for `PUT`/`DELETE` request
  method parsing and routing.
- Quality infrastructure is complete: ASan, UBSan, TSan, presets, and baseline
  benchmarks.
- The first P2 user vertical slice is complete:
  `register -> login -> Redis session -> Authorization -> me`.
- The 0006 profile/skill vertical slice is implemented and verified in the
  committed baseline: authenticated profile read/update and skill
  list/create/upsert/delete.
- The 0007A infrastructure slice is implemented and verified in the committed
  baseline: bounded HTTP bodies, multipart parsing, MySQL
  transactions, local file storage, storage configuration, and shared session
  authentication.
- The Resume Vertical Slice is implemented, committed, and pushed:
  upload, paginated list, text version creation, and version listing.
- The Job Read Vertical Slice is committed and pushed: public active-job
  search, pagination, detail, and requirements.
- The Application Vertical Slice is implemented, committed, and pushed:
  authenticated create/list/status/statistics with Job and ResumeVersion
  validation, duplicate protection, and a strict state machine.
- The Job Ingestion Vertical Slice is implemented, committed, and pushed:
  internal token-protected batch import, `source_url` idempotent upsert,
  transactional requirements replacement, and Import -> Job Read ->
  Application integration coverage.
- The Job Search / Ranking Vertical Slice is implemented, committed, and
  pushed: salary/skill hard filters, authenticated `rule_v1`
  recommendations, deterministic score breakdown, stable pagination, and a
  bounded ranking candidate set.
- The 0012 Source Adapter + Freshness slice is implemented through Step 8 and
  pushed: additive freshness columns, Python Greenhouse/Lever/Ashby adapters,
  internal `POST /internal/jobs/source-sync` observations with completeness
  semantics, disappearance grace and reopen, and freshness-aware public search.
- The 0013 AI Matching slice is implemented, committed, and archived: additive
  lease/rule-baseline columns on `ai_tasks`, deterministic `match_rule_v1` in
  C++, public `POST /api/v1/ai/match` plus task polling, token-protected
  `/internal/ai/tasks/*`, and an optional Python `ai_worker/`.
- The 0014 China campus sources + manual capture slice is implemented through
  Step 8 with **Step 5 environment-limited**: F13 Beijing-time deadline
  semantics, F14 manual-record priority, the additive `jobs` migration (6
  nullable columns, 0 indexes), the Python capture framework, and the manual
  capture channel (bookmarklet + local publisher through
  `/internal/jobs/import`). No real China source adapter exists yet and no China
  `source` identifier is registered.
- **0016 (open-source packaging) is complete and archived with its L4 green**,
  and **0015 (resume tailoring and explainable matching)** with **0015A
  (job-list usability and intake quality)** are complete and archived too, so
  **no plan is active** and the recorded MVP roadmap is complete through
  packaging. Details: `## Completed Plan (0016)`, `## Completed Plan (0015)` and
  `## Completed Plan (0015A)` below.
- **0017 (ops hardening and acceptance closure) is complete and archived**: CI
  compiles the tree once (`build` uploads the release tree as a single artifact,
  `integration` consumes it, and the container job moved to
  `.github/workflows/image.yml` with path/branch filtering); the 309-case suite
  was audited by assertion target and proved to contain **no removable case**
  (k = 0, machine-checked by `tools/checks/test_coverage_map.py` and enforced in
  CI); `tools/dev/capture-acceptance.sh` runs the bookmarklet publish/read-back
  acceptance in one command and prints the 0014 §19.5 evidence block; and
  `docs/RELEASE_CHECKLIST.md` plus `tools/checks/release_preflight.py` make the
  release gate checkable. Details: `## Completed Plan (0017)` below.

The revised architecture direction is:
[../architecture/open-source-mvp.md](../architecture/open-source-mvp.md).

Key decisions:

- Job Discovery, Search, and Crawler are separate concerns.
- Source Adapter -> Normalize/Deduplicate -> Job Ingestion replaces a general
  crawler.
- AI uses `Hard Filter -> Rule Score -> Embedding -> LLM`.
- AI Providers are BYOK and provider-neutral.
- RabbitMQ, Gateway, general Crawler, and AutoApply are not MVP requirements.
- Freshness lifecycle is introduced with 0012 Source Adapter, not 0011.
- A single missing fetch never closes a Job.

## Completed Plan (0017)

[`completed/0017-ops-hardening-and-acceptance-closure.md`](../exec-plans/completed/0017-ops-hardening-and-acceptance-closure.md)
— closed and archived 2026-10-10; four independent units landed, each committed
after its own targeted verification.

**The stage's single L4 passed on a second attempt** (raw log
`build/l4-0017/run2.log`; the isolated harness `build/l4-0017/run.sh` uses its
own compose project `jobpilot-l4-0017` with MySQL `:13399` / Redis `:16399` and
its own volumes, so neither the development database nor
`jobpilot_review_scratch` was touched): debug / asan / ubsan each
`100% tests passed, 0 tests failed out of 309` (114.83 s / 130.21 s / 74.41 s,
sanitizer diagnostics 0), both Release builds rc=0, Python `Ran 446 tests` + `OK`
and `Ran 190 tests` + `OK`, and the review-backfill isolation legs green (dev
`REVIEW_DB_ISOLATION connected=jobpilot scratch_env=<unset>` + `[ SKIPPED ]`,
scratch `connected=jobpilot_l4_scratch scratch_env=jobpilot_l4_scratch` +
`[ PASSED ] 7 tests`). **The first attempt is kept as history**: the host was
3.7× slower than at 0016 (pure-CPU `test_bcrypt` 7.13 s → 26.58 s), which turned
nine 2-second-timeout HTTP cases red; the harness now refuses to run when the
probe exceeds 14 s
(`/usr/bin/time ./build/presets/debug/backend/test_bcrypt`, healthy ≈ 7.1 s).
CI after the change is `success` on every push (`ci` + `image`); the
`integration` job dropped from 5m21s to 1m16s–1m49s, and the new
`Coverage map guard` step passed in all four runs.

Delivered:

- **0017-A CI cost** (`e6cae75`): `build` packages `build/presets/release` into
  one tar.gz and uploads it (`actions/upload-artifact@v4`, `compression-level: 0`,
  `retention-days: 1`); `integration` (`needs: build`) downloads and extracts it
  and no longer provisions Conan or compiles the tree. The container job moved to
  `.github/workflows/image.yml`: always on `main`, and on a pull request only when
  `Dockerfile`, `docker-compose.yml`, `docker/**` or `tools/dev/**` changed. Local
  replay of the workflow's own commands: actionlint rc=0; 56/56 test binaries
  carry no Conan `NEEDED` entry; the extracted tree runs
  `100% tests passed, 0 tests failed out of 309` plus both Python suites; dev-host
  wall clock 466.61 s → 260.23 s per run (the duplicated
  bootstrap+configure+compile is replaced by a 4.88 s tar round trip). Runner wall
  clock and the three-run comparison stay owner-confirmed: this host has no GitHub
  token and the anonymous API answers 404 for a private repository.
- **0017-B test overlap audit** (`4a7995e`): all 309 CTest cases (57 binaries)
  classified by assertion target in `docs/quality/test-overlap-audit.md`; 66
  candidates (1,533 within-binary pairs through four mechanical filters plus one
  manual loop sweep) each fail at least one clause of the strict merge rule, so
  **k = 0**: no case was removed and no test source was edited. 47 RECORD-ONLY
  overlaps are documented with `file:line` and the verbatim assertion.
  `docs/quality/test-merge-map.json` plus `tools/checks/test_coverage_map.py` make
  "coverage did not degrade" executable (positive control PASS 309 → 309; five
  negative controls each exit 1), and the CI `integration` job runs the guard.
- **0017-C bookmarklet acceptance scaffold** (`38f29fd`):
  `tools/dev/capture-acceptance.sh --capture <capture.json>` drives
  publish-capture → `/internal/jobs/import` → public search visibility → detail
  read-back → the F14 collision check inside an isolated compose project and
  prints the sanitized ExecPlan 0014 §19.5 evidence block. The synthetic sample
  (`docs/examples/capture-acceptance-sample.json`) is green end to end (9 legs,
  `CAPTURE_ACCEPTANCE_OK`); the owner only has to run it once against a real
  capture JSON to close 0014's acceptance item.
- **0017-D release checklist** (`38f29fd`, redaction fix `b2a55be`):
  `docs/RELEASE_CHECKLIST.md` plus `tools/checks/release_preflight.py` (licence,
  the Conan dependency list against `THIRD_PARTY_NOTICES.md`, secret and
  private-address scans, the README-for-outsiders checks, relative links,
  actionlint, repository hygiene). The preflight passes `7 pass, 0 fail, 0 skip`
  on a clean clone, and its secret scan immediately caught a real problem in the
  tree pushed minutes earlier: the private IPv4 of the development host in
  `docs/quality/test-overlap-audit.md`, redacted in `b2a55be`.

**Defect found by 0017 and fixed by 0017A (2026-10-10):** `publish-capture`
normalizes the capture's `published_at` into the manual-import payload
(`source_adapters/jobpilot_sources/capture/publisher.py:220-224`), but
`POST /internal/jobs/import` neither documented it (`docs/04-api-design.md:599-625`)
nor parsed it (`service/src/job/internal_job_import_controller.cpp:391-477`), so
the value was dropped silently and the public `posted_at` stayed `null`; the
source-sync path already accepted `published_at`
(`service/src/job/internal_source_sync_controller.cpp:447`). The 0017-C scaffold
observed it. Impact: a manual row never carried a published date, while the
"published within N days" filter (`docs/04:488`,
`service/src/job/job_dao.cpp:217-218`) requires `published_at IS NOT NULL`.
**0017A fixed it** (`docs/exec-plans/completed/0017A-manual-import-published-at.md`):
`published_at` is now an optional, documented field of the manual import
(`docs/04` §5.2), parsed into `JobImportData::publishedAt` and written by
`JobIngestionDao::UpsertJob` with
`COALESCE(VALUES(published_at), published_at)` so an upsert that omits it never
clears a stored value; the public detail exposes it as `posted_at`. Verified by
the extended `JobIngestionDaoTest` / `JobIngestionHttpTest` cases and by
`tools/dev/capture-acceptance.sh`, which now asserts the `posted_at` round-trip
instead of reporting the gap.

## Completed Plan (0015)

[`completed/0015-resume-tailoring-and-explainable-matching.md`](../exec-plans/completed/0015-resume-tailoring-and-explainable-matching.md)
— archived 2026-10-10; Phases A-C complete, Phase D implemented through D5 (D6
deferred by owner decision, plan §17); D5 passed its L2 verification (commit
`ae85868`) and the stage's single L4 passed at HEAD `26e2069` (production code
frozen at `576f436`).

Delivered:

- Phase A: `profiles.education_level` / `graduation_year` / `campus_status` are
  nullable columns with no backfill (NULL = unknown); `test_schema` covers the
  live, fresh and pre-0015 migrated databases. Resume PDF text status is
  **derived** (`ok` / `text_empty` / `pdf_no_text` / `text_unavailable`) — no new
  column.
- Phase B: `rule_v1` weights unchanged; `match_explanation` adds deadline
  urgency, deal-breakers as data and reasons; 7 additive keys on `/recommended`
  and on the job list with `with_match=true`; the default payload is
  byte-identical; `strict=true` paginates in the shared ORDER BY window, `total`
  is the surviving count and a capped window is flagged `truncated`.
- Phase C: `tools/job_report/render_report.py` — stdlib-only offline
  self-contained HTML report with 11 deterministic tests.
- Phase D: D0/D1 contracts (`docs/03` §10.6, `docs/04` §9); D2 `tailor_rule_v1`
  deterministic draft; D3 `resume_artifacts` table + DAO + schema tests; D4
  mechanical fact-provenance gate (`fact_check_status=2` blocks confirmation);
  D5 four authenticated routes — `POST`/`GET /api/v1/resumes/artifacts`,
  `GET /api/v1/resumes/artifacts/{id}`,
  `POST /api/v1/resumes/artifacts/{id}/confirm` — with draft
  (`confirmed_at IS NULL`) vs confirmed distinguishable, **409** on a failed fact
  gate or an already-confirmed artifact, `/submit` / `/apply` / `/send` probes
  → 404, and **no submission endpoint anywhere**
  (`tools/checks/no_submission_path.py`). `IResumeDao` also
  gained the `FindOwnedVersion` seam used by draft generation.
  D5's L2 evidence (commit `ae85868`, pushed): release preset build rc=0;
  selected ctest 30/30 (`test_resume_http` 5/5, `test_resume_artifact_dao` 4/4,
  `test_resume_dao` 2/2, `test_resume_service` 6/6, `test_ai_task_service` 13/13);
  `tools/checks/no_submission_path.py` rc=0; `git diff --check` rc=0.

The stage's single L4 passed at HEAD `26e2069` (production code frozen at
`576f436`, tree clean): builds `debug` / `asan` / `ubsan` / `release` /
`root-release` all rc=0; CTest `100% tests passed, 0 tests failed out of 309` in
Debug, ASan and UBSan (rc=0, sanitizer diagnostics 0); Python `source_adapters`
`Ran 446 tests` + `OK` and `ai_worker` `Ran 190 tests` + `OK`; backfill isolation
dev `[  SKIPPED ]` / scratch `[  PASSED  ] 7 tests`. The first full-matrix run at
`576f436` caught one test-only defect, fixed in `26e2069` with production code
unchanged (raw lines below).

## Completed Plan (0016)

[`completed/0016-open-source-packaging.md`](../exec-plans/completed/0016-open-source-packaging.md)
— archived 2026-10-10; P1-P5 landed and the stage's single L4 passed at HEAD
`0a6e59e` (tree frozen and clean).

Delivered:

- P1 build/run contract: pinned `conan.lock`, `tools/dev/bootstrap.sh`, the TSan
  target-order fix, the multi-stage `Dockerfile`, the compose `app` profile and
  the `resume_storage` mount, so a clean machine can configure, build and run;
  the MySQL healthcheck fix is recorded in the plan §17.
- P2 database bootstrap/upgrade documentation: `database/schema/README.md`
  (file→table map, both from-scratch paths, the numbered additive-`ALTER` upgrade
  table, verification queries, isolation rule), with `docs/03` §6 pointing at it.
- P3 demo gate: `tools/dev/demo-gate.sh` runs configure → start → import → search
  → match → tailor → track against an isolated compose project, with a negative
  control; it found and fixed the `main.cpp` artifact wiring (plan §17).
- P4 CI: `.github/workflows/ci.yml` with `build`, `integration` (MySQL 8.0 +
  Redis 7, schema from the committed files, the whole CTest suite, both Python
  suites) and `image` jobs. The first real runs then did their job: they exposed
  the compose environment the image job needed, and — the reason the suite job had
  never executed a single test — a ctest path pointing at the service entry's
  build directory while the job had configured the root entry. `clean build` and
  `container image and the end-to-end gate` are confirmed green on a runner
  (closing CE-5: the committed image builds there and the gate passes against
  it), and run `38057642118` at `9238b97` is green on all three jobs, the suite
  job included - its first genuine execution of the 309 tests on a runner.
- P5 open-source hygiene: English Quick Start in `README.md`,
  `THIRD_PARTY_NOTICES.md`, dated corrections in `docs/07`/`docs/08`, the
  `docs/README.md` reading order, and a secret/private-detail scan (two private
  proxy addresses redacted; link check 55 links, 0 missing).

L4 (HEAD `0a6e59e`, frozen tree, working tree clean): builds `debug build rc=0`,
`asan build rc=0`, `ubsan build rc=0`, `release rc=0`, `root-release rc=0`; the
three CTest phases were re-run after the presets fix and report `100% tests
passed, 0 tests failed out of 309` in Debug (96.12 s), ASan (88.86 s) and UBSan
(59.93 s), sanitizer diagnostics 0; Python `source_adapters` `Ran 446 tests` +
`OK` and `ai_worker` `Ran 190 tests` + `OK`; backfill isolation dev
`[  SKIPPED ]` (rc=0) and scratch `[  PASSED  ] 7 tests` (rc=0). The
debug/asan/ubsan build logs each also carry a `configure rc=1` from a generated
`service/CMakeUserPresets.json` (scratch include, since fixed in
`tools/dev/bootstrap.sh`); the binaries built rc=0. No defect surfaced after P3;
the two the stage found (MySQL healthcheck, `main.cpp` wiring) are recorded in the
plan §17.

## Completed Plan (0015A)

[`completed/0015A-job-list-usability-and-intake-quality.md`](../exec-plans/completed/0015A-job-list-usability-and-intake-quality.md)
— **closed and archived 2026-10-10**; its §13 step boxes were never maintained
during the stage, so the closure evidence is the recorded L4 gate below, and
Step 5's real-database backfill was never applied (apply/revert are proven only
against the scratch database).

Progress:

```text
Steps 0-8 complete; the stage's single L4 passed
L4: Debug / ASan / UBSan 286/286 each; both Release builds ok; Python 446 and 190;
    sanitizers clean; backfill isolation: dev database explicitly SKIP, scratch 7/7
```

- Public job search defaults to `clean` only (`AND j.review_status = 0`);
  `needs_review` rows are reachable only through the internal token-protected
  review surface (`GET /internal/jobs/review-queue`,
  `POST /internal/jobs/review/{id}`, `POST /internal/jobs/review/backfill`).
- The list payload carries `source_url`; `education_max` / `job_nature` /
  `deadline_before` / `deadline_after` filters and `sort` (`created_desc` /
  `deadline_asc` / `education_asc`) are live.
- Description length is capped at 48 KiB of UTF-8 bytes; over-long input is
  rejected or routed to review with diagnostics, never silently truncated.

Recorded database state (observation, not a defect, no cause asserted): `jobs`
has 4 rows, all `review_status=0`, and `job_review_items` has 0 rows; a
historical dry run once saw 6 active jobs with no baseline snapshot, and the
cause is unproven.

## Completed Plan (0014)

[`completed/0014-china-campus-sources-and-manual-capture.md`](../exec-plans/completed/0014-china-campus-sources-and-manual-capture.md)
— archived 2026-10-09; the plan is frozen as historical evidence.

Progress:

```text
Steps 1-8 executed (Step 2/3 delivered except source registration; Step 5
environment-limited and CARRIED FORWARD)
Step 5: environment-limited (no compliant list contract verified) — accepted
Step 8: L4 passed once (2026-10-09): Debug / ASan / UBSan 254/254 each,
        service + root Release builds ok, Python 337 and 190
```

Delivered:

- Step 1/4: `jobs` additive fields + contracts (`docs/03` §9, `docs/04` §5.1/§5.2/
  §5.4/§5.5), both JSON parsers, both validators, both upserts, read-back and
  response mapping; existing-database ALTER executed against a pre-0014 database
  and re-verified with the schema suite.
- Step 2/3: source register + offline fixtures, and the Python China framework
  (robots per-path fail-closed, retry/backoff, charset precedence, HTML/JSON-LD/
  RSS/sitemap parsers, SSRF guard, size/request/page budgets, salary rules, F13).
- Step 6: manual capture (credential-free bookmarklet + local publisher reusing
  `POST /internal/jobs/import` with `source="manual"`; F14 proven end-to-end).
- Step 7: documentation sync (root README, this file, the plan, agent-workflow §8).
- Step 8: the stage's single L4 — Debug/ASan/UBSan 254/254 each, service and root
  Release builds ok, Python `source_adapters` 337/337 and `ai_worker` 190/190 (plan
  §21.2).

**Carried forward (incomplete): Step 5.** The first real China source could not be
verified compliantly — 国聘's job list requires its client-side request signature
(anti-automation risk control) and 清华's job-list host
`career.cic.tsinghua.edu.cn` returns a persistent `robots.txt` HTTP 503
(fail-closed). No China adapter is implemented and the four-place allowlist is
unchanged; the registered sources remain `greenhouse` / `lever` / `ashby`. This
stays visible in `## Next Entry` items 6/7. Evidence:
[`../../source_adapters/README.md`](../../source_adapters/README.md) source
register and
[`../../source_adapters/jobpilot_sources/tests/fixtures/cn_source_probe_2026-10-09.json`](../../source_adapters/jobpilot_sources/tests/fixtures/cn_source_probe_2026-10-09.json).

Remaining work / next steps:

1. Step 5 re-entry: only with a newly verifiable candidate (written permission or
   a documented public API for 国聘; a re-probe once
   `career.cic.tsinghua.edu.cn/robots.txt` stops returning 503; or a different
   server-rendered, unsigned school list — the last needs the product owner's
   authorization to widen the candidate set).
2. The real-browser bookmarklet acceptance remains **UNVERIFIED** (plan §19.5
   template); it is owned by the product owner and is not a stage blocker.
   *[Clarified 2026-10-10, original wording kept: the owner has since run the
   **extraction** step on a real login-walled posting (Edge; JSON produced; one
   Network observation saw no request issued by the bookmarklet — 0014 §19.6,
   【用户报告】). `publish-capture` and backend persistence were not executed in
   that run, so the item as a whole is still not accepted.]*
3. Stage **0015** (resume tailoring) is next, then **0016** (open-source packaging).

Note: `## Next Entry` items 1-5 and the stage-numbering table below were written on
2026-10-08 as the recorded stage-numbering decision and are preserved verbatim
(items 6/7 are the 0014 carry-forward added when 0014 closed; items 8/9 record
the 0015 close); the current status is this section.

## Stage Handoff Map

| Stage | ExecPlan evidence | Primary implementation/tests | Primary commits |
|---|---|---|---|
| P0 Config / Logging / Schema | `completed/0002-p0-config-logging-schema.md` | `backend/src/core`, `database/schema`, backend config/logger/schema tests | `1d3bcbd`, `2c2f344`, `682e26d` |
| Quality / Benchmark | `completed/0003-quality-and-performance-baseline.md` | CMake presets, backend benchmarks | `baefe55` |
| P1 Correctness Gate | `completed/0004-p1-correctness-gate.md` | `backend/src/network`, `backend/src/http`, P1 tests | `2f54cb6`, `2655e05` and preceding P1 fixes |
| User / Auth | `completed/0005-p2-user-vertical-slice.md` | `service/{include,src}/jobpilot/service/user`, `service/tests/test_user_*` | `8e501ce`, `6ed94cb`, `70051c7`, `889e1e7` |
| Profile / Skill | `completed/0006-p2-profile-skill-vertical-slice.md` | User profile/skill DAO/Service/Controller and tests | `73b1172` |
| Resume Infrastructure | `completed/0007A-resume-infrastructure.md` | HTTP body/multipart, Transaction, FileStorage, SessionAuthenticator | `5d145d7`, `516837f`, `dd7cc78`, `638d73f`, `ecf9b68`, `90f52c9` |
| Resume | `completed/0007-p2-resume-vertical-slice.md` | `service/{include,src}/jobpilot/service/resume`, resume tests | `644c7b0`, `1b95602` |
| Job Read | `completed/0008-p2-job-read-vertical-slice.md` | `service/{include,src}/jobpilot/service/job`, job read tests | `3c027b1`, `5f49551` |
| Application | `completed/0009-p2-application-vertical-slice.md` | `service/{include,src}/jobpilot/service/application`, application tests | `2bebcfe`, `a1074d5` |
| Job Ingestion | `completed/0010-p2-job-ingestion-vertical-slice.md` | Job ingestion DAO/Service/Controller, `/internal/jobs/import`, ingestion tests | `9e87686` |
| Architecture Re-Research | `../architecture/open-source-mvp.md`, `../adr/README.md` | ADR-0007 through ADR-0010 | `635fc31` |
| Job Search / Ranking | `completed/0011-job-search-ranking.md` | Job ranking/search modules and tests | `214dc9e` |

## Completed Plan (0013)

[0013-ai-matching.md](../exec-plans/completed/0013-ai-matching.md)

Progress:

```text
Steps 1-10 complete
L4: Debug / ASan / UBSan / Release 230/230 each; Python 85 and 190
```

0013 scope: `job_match` end to end — additive owner/lease/rule-baseline columns
on `ai_tasks`, deterministic `match_rule_v1` scoring in C++, public
`POST /api/v1/ai/match` plus `GET /api/v1/ai/tasks/{id}`, token-protected
`/internal/ai/tasks/{claim,complete,fail,input}`, and an optional Python
`ai_worker/` that adds embedding/LLM explanation without ever replacing the
deterministic score. `resume_optimize` / `interview_gen` stay reserved for 0015 (the
stage-numbering decision in `## Next Entry` renumbered resume tailoring from 0014
to 0015).

## Completed Plan (0012)

[../exec-plans/completed/0012-job-source-adapter-freshness.md](../exec-plans/completed/0012-job-source-adapter-freshness.md)

Progress:

```text
Steps 1-10 complete
L4 verified: Debug / ASan / UBSan 172/172 each, Release builds passed
```

0012 scope:

- Manual Greenhouse / Lever / Ashby source configuration.
- Python source adapters publishing through C++ internal APIs.
- Source Adapter observations use `POST /internal/jobs/source-sync`
  (`complete=true` accepts `0..200`, `complete=false` accepts `1..200`); manual
  and dataset loads keep using `POST /internal/jobs/import` (`1..100`).
- Complete observation and source-sync semantics.
- Additive freshness fields and safe disappearance handling.
- Fresh / aging / stale / closed search behavior.

0012 non-goals include canonical multi-source Job identity, RabbitMQ, browser
crawler, AI freshness authority, AutoApply, and schema changes outside the
additive freshness fields.

Stop and return to design if complete board coverage cannot be proven, the
provider requires authentication, adapters need direct MySQL access, or a
larger schema redesign becomes necessary.

## Architecture Summary

```text
HTTP request
  -> HttpServer / HttpRouter
  -> UserController
  -> UserService
  -> UserDao / SessionManager
  -> MySQL ConnectionPool / RedisClient
```

- `service/` is the canonical runnable build entry; root is the unified
  developer entry.
- `jobpilot_core` owns network/HTTP, config/logging, database, and cache
  primitives.
- `jobpilot_service_core` owns routing plus User, Resume, Job, Application,
  and Job Ingestion modules.
- P1 ownership rules remain mandatory for changes under
  `backend/{include,src}/jobpilot/{network,http}/`.

## Verified Snapshot

- Pre-0006 clean `service/` and root CTest: 54/54.
- Full Debug CTest after 0011: 115/115.
- ASan full CTest after 0011: 115/115.
- UBSan full CTest after 0011: 115/115.
- TSan default gate after 0010: 42/42.
- TSan external-service subset: 6/6.
- Focused Release regression after the `PUT`/`DELETE` method extension: 9/9.
- Both `service/` and root Release builds completed successfully.
- 0012 L4: full Debug, ASan, and UBSan CTest 172/172 each (0 failures, no
  sanitizer diagnostics); `service/` and root Release builds passed; Python
  adapter suite 85/85; `ctest -L job` 84/84.
- 0014 L4 (2026-10-09, one run, service presets, sequential phases): full Debug,
  ASan and UBSan CTest **254/254 each** (0 failures, no sanitizer diagnostics, no
  `LastTestsFailed.log` in any preset); `service/` Release built and
  `jobpilot_service` links cleanly; root Release built; Python `source_adapters`
  **337/337** and `ai_worker` **190/190**. Raw logs and per-phase timestamps are
  recorded in the 0014 plan §21.
- 0015A L4 gate: full Debug, ASan and UBSan CTest **286/286 each** (sanitizers
  clean); both `service/` and root Release builds passed; Python
  `source_adapters` **446** and `ai_worker` **190**; the review-backfill
  isolation ran with the dev database explicitly SKIPped and the scratch database
  7/7.
- 0015 stage L4 (HEAD `26e2069`; production code frozen at `576f436`, tree clean;
  run after the test-only fix): builds `debug rc=0`, `asan rc=0`, `ubsan rc=0`,
  `release rc=0`, `root-release rc=0`; CTest **`100% tests passed, 0 tests failed
  out of 309`** in Debug (118.25 s), ASan (131.29 s) and UBSan (76.48 s), all rc=0,
  sanitizer diagnostics 0 (`AddressSanitizer` / `LeakSanitizer` / `runtime error:`
  / `UndefinedBehaviorSanitizer`); Python `source_adapters` `Ran 446 tests` + `OK`
  and `ai_worker` `Ran 190 tests` + `OK`; backfill isolation dev
  `REVIEW_DB_ISOLATION connected=jobpilot scratch_env=<unset>` + `[  SKIPPED ]`
  (rc=0) and scratch `REVIEW_DB_ISOLATION connected=jobpilot_review_scratch
  scratch_env=jobpilot_review_scratch` + `[  PASSED  ] 7 tests` (rc=0). The first
  full-matrix run at `576f436` failed debug + asan on one **test-only** defect
  (`MatchExplanationTest.ClassifiesDeadlineUrgencyWithDateOnlyHandling` compared
  `const char *` with `EXPECT_EQ`); fixed in test-only commit `26e2069` and the
  three CTest phases re-ran green. Raw logs (gitignored): `build/l4-0015/`.
- 0016 stage L4 (HEAD `0a6e59e`, frozen tree, working tree clean): builds
  `debug build rc=0`, `asan build rc=0`, `ubsan build rc=0`, `release rc=0`,
  `root-release rc=0`; the three CTest phases were re-run after the presets fix
  (`build/l4-0016/test2-*.log`, `test2-summary.txt` rc=0) and report
  **`100% tests passed, 0 tests failed out of 309`** in Debug (96.12 s), ASan
  (88.86 s) and UBSan (59.93 s), sanitizer diagnostics 0
  (`AddressSanitizer` / `LeakSanitizer` / `runtime error:`); Python
  `source_adapters` `Ran 446 tests` + `OK` and `ai_worker` `Ran 190 tests` + `OK`;
  backfill isolation dev `REVIEW_DB_ISOLATION connected=jobpilot scratch_env=<unset>`
  + `[  SKIPPED ]` (rc=0) and scratch
  `REVIEW_DB_ISOLATION connected=jobpilot_review_scratch scratch_env=jobpilot_review_scratch`
  + `[  PASSED  ] 7 tests` (rc=0). The debug/asan/ubsan build logs each also
  carry a `configure rc=1` from a generated `service/CMakeUserPresets.json`
  (scratch include, since fixed in `tools/dev/bootstrap.sh`); the binaries built
  rc=0. No defect surfaced after P3; the two the stage found (MySQL healthcheck,
  `main.cpp` wiring) are in the plan §17. Raw logs (gitignored): `build/l4-0016/`.
- 0017 stage L4 (code frozen at `38f29fd`; the CI/docs commits after it do not
  invalidate code tests): **green on the second attempt.** With the stage's own
  isolated compose stack (`jobpilot-l4-0017`, MySQL `:13399` / Redis `:16399`,
  own volumes): builds `debug rc=0`, `asan rc=0`, `ubsan rc=0`, `release rc=0`,
  `root-release rc=0`; Debug / ASan / UBSan each
  `100% tests passed, 0 tests failed out of 309` (114.83 s / 130.21 s / 74.41 s,
  sanitizer diagnostics 0); Python `source_adapters` `Ran 446 tests` + `OK` and
  `ai_worker` `Ran 190 tests` + `OK`; review-backfill isolation dev
  `REVIEW_DB_ISOLATION connected=jobpilot scratch_env=<unset>` + `[ SKIPPED ]`
  and scratch `connected=jobpilot_l4_scratch scratch_env=jobpilot_l4_scratch` +
  `[ PASSED ] 7 tests`. The first attempt was environment-limited (the host ran
  3.7× slower than at 0016: pure-CPU `test_bcrypt` 7.13 s → 26.58 s, turning nine
  2-second-timeout HTTP cases red, `97%` in Debug); the harness now refuses to
  run above a 14 s probe. Raw logs (gitignored): `build/l4-0017/` (`run2.log`,
  `test-summary.txt`, `probe-bcrypt*.log`).
- CI after 0017-A (run ids verified with `tools/dev/ci-status.sh`): the four
  0017 pushes are all `completed/success` — `38f29fd` ci 4m00s (build 2m44s +
  suite 1m16s) + image 3m00s, `4a7995e` ci 5m40s + image 2m29s, `b2a55be` ci
  4m38s + image 2m51s, `b1d1b89` ci 5m45s + image 2m49s. The same three jobs cost
  11m26s per push at the pre-0017 baseline `a8dd333` (`38058123275`: build 3m17s
  + suite 5m21s + image 2m48s), so the `integration` job alone went from 5m21s to
  1m16s–1m49s and the new `Coverage map guard` step passed in every run.
- Opt-in live smoke (not part of CI), **re-run 2026-10-19** by 0019:
  `INTERNAL_API_TOKEN=dummy python3 -m jobpilot_sources sync --config
  /tmp/0019-ats-live.yaml --dry-run` with boards `gitlab` (Greenhouse),
  `leverdemo` (Lever), `ramp` (Ashby); raw output in
  `build/0019/ats-live-smoke.log`:
  - Greenhouse `gitlab`: **232** jobs, `board_complete=True`, 2 batches,
    `publish_complete=False` (the 200-job batch limit), diagnostics 0;
  - Lever `leverdemo`: **10** jobs, `board_complete=True`, 1 batch,
    `publish_complete=True`, diagnostics 0;
  - Ashby `ramp`: **167** jobs, `board_complete=True`, 1 batch,
    `publish_complete=True`, diagnostics 0.
  `--dry-run` means no database writes. The historical
  217/10/160 counts were recorded without raw evidence; they remain
  historical/unverified and are not constants. Counts drift with the hiring
  season; this is a 2026-10-19 snapshot.
- Real HTTP register/login/me and profile/skill ownership flows passed; test
  data was cleaned afterward.

Quality commands and recorded results:
[../quality/README.md](../quality/README.md).

Performance baseline:
[../benchmarks/README.md](../benchmarks/README.md).

## Current Risks

| Priority | Risk | Boundary |
|---|---|---|
| High | No HTTP request-header limit | Body is capped at 12 MiB; header exhaustion remains open |
| High | Raw `Channel *` in epoll active list | Requires strict channel lifetime discipline |
| Medium | No graceful shutdown/signal handling | Service cannot stop cleanly on signals |
| Medium | Expired vs invalid Redis token cannot be distinguished | Both currently return `20001`; `20002` is reserved |
| Medium | P1 HTTP methods remain a minimal subset | Further methods require a new reviewed change |
| Medium | Live MySQL/Redis required by integration tests | CTest is not self-contained |
| Medium | Internal import token is application-layer only | Gateway network isolation is still pending |
| Medium | Source adapters publish no salary | Lever/Ashby units do not map to the monthly-thousands column contract |
| Medium | Adapters need a running C++ service | The CLI publishes over HTTP; there is no queue or scheduler yet |
| Medium | Lever/Ashby company comes from configuration | Those APIs return no company name, so `company` falls back to config or the board token |
| Medium | AI provider abstraction is not implemented yet | Core must remain functional without AI |
| Medium | Ranking candidate set is capped at 2000 | Results expose `truncated=true` |
| Low | Minimal Web UI is local-demo only | HTTP/localStorage, no production auth; no mobile/framework/build; see `docs/guides/local-demo.md` |

## Next Entry

1. Review the archived 0013 plan and its L4 evidence
   (`completed/0013-ai-matching.md`); 0013 is complete and archived.
2. The China-first source layer decided in
   `adr/0011-china-first-sourcing-and-compliance.md` (public sources plus
   manual capture import, campus focus, company career pages) is planned as
   stage **0014**: its active ExecPlan is authored and implementation has not
   started. The three overseas adapters from 0012 become optional sources.
3. `resume_optimize` and `interview_gen` remain contract-only; the Chinese
   resume/ATS tailoring work is stage **0015**.
4. Follow stage **0016** (open-source packaging) after that.
5. Keep AutoApply, RabbitMQ, general Crawler, Gateway, and SaaS RBAC out of
   the MVP unless a separate ADR changes the decision.
6. **0014 carry-forward — Step 5 (first real China source) is environment-limited
   and NOT done.** It can only complete with a compliant, verifiable candidate:
   written permission or a documented public API for 国聘; a re-probe once
   `career.cic.tsinghua.edu.cn/robots.txt` stops returning 503; or a different
   server-rendered, unsigned school job list that needs the product owner's
   authorization. Until then **no China source adapter is registered** (only
   `greenhouse`/`lever`/`ashby`).
7. **0014 carry-forward — the real-browser bookmarklet acceptance is partly
   verified and still not accepted.** The owner ran the extraction step on a real
   login-walled posting (Edge; JSON produced; one Network observation saw no
   request issued by the bookmarklet) and it is recorded in the archived plan's
   §19.6 as 【用户报告】with its limits. `publish-capture`, the backend import and
   MySQL persistence were **not** executed in that run, so the §19.5 template's
   `created/updated/total` and public search-visibility rows are still empty and
   the acceptance as a whole stays open; it is owned by the product owner and is
   not a stage blocker.
8. **0016 is closed with a green L4 and archived**
   (`completed/0016-open-source-packaging.md`; P1-P5 landed, L4 at HEAD
   `0a6e59e`, raw lines in `## Verified Snapshot`), so the recorded MVP roadmap is
   complete through packaging and **no plan is active**. Genuinely open:
   0014's Step 5 (item 6), the bookmarklet's `publish-capture`/read-back
   acceptance (item 7), 0015's D6 deferral (0015 plan §17), and 0016's CI: the
   workflow runs: run `38057642118` at `9238b97` is **green on all three jobs** —
   `clean build`, `container image and the end-to-end gate` (the committed
   `ubuntu:24.04` image builds on a runner and the demo gate passes against it:
   CE-5 closed) and `full test suite (MySQL + Redis)`, whose first genuine
   execution ran the whole 309-test suite green. That workflow itself is
   verified as far as it can be locally (actionlint 1.7.7: **0 issues**). The
   repository being **private** is also the one thing that keeps the packaging
   work from being "open source" in practice: making it public is the owner's
   call. The Lead commits and pushes this archival change.
9. **Resolved — both plans are archived under `completed/`.** 0015A's §13 step
   boxes remain unticked on purpose (never maintained during the stage); its
   status header now records the closure evidence, and Step 5's real-database
   backfill was never applied (apply/revert are proven only against the scratch
   database).

10. **0017 is closed with a green L4 and archived**
    (`completed/0017-ops-hardening-and-acceptance-closure.md`), so **no plan is
    active** again. Genuinely open after it: 0014's Step 5 (item 6), the
    bookmarklet's real-machine run through `tools/dev/capture-acceptance.sh`
    (item 7 — the scaffold and the synthetic path are proven end to end, the real
    JSON is the owner's), 0015's D6 deferral, the `published_at` manual-import gap
    (recorded in `## Completed Plan (0017)` and **fixed by 0017A on 2026-10-10**),
    and the owner-only release actions
    (make the repository public, fill About/description, confirm the newest CI
    runs, rotate the old PAT) collected in
    [`RELEASE_CHECKLIST.md`](../RELEASE_CHECKLIST.md).

11. **0020 is merged** at `c07c35a` (PR #3, merge commit) after
    VERDICT-0020-closeout PASS with conditions; main `ci` `39375032387` and
    `image` `39375032398` are both green. The 0020 plan is archived under
    `completed/`.
12. **0021 is merged** at `d75b14e` (PR #4, merge commit) after
    VERDICT-0021-closeout PASS with conditions; main `ci` `39378254785` and
    `image` `39378254777` are both green. The 0021 plan is archived under
    `completed/`.
13. **0022 is merged** at `38bd484` (PR #5, merge commit) after
    VERDICT-0022-closeout PASS with conditions; main `ci` `39446877179` and
    `image` `39446876743` are both green, and the PR CI `Web UI smoke
    (Python fallback)` step is green (`39445774298`). The 0022 plan is archived
    under `completed/`.
14. **0023 is merged** at `539202f` (PR #6, merge commit) after
    VERDICT-0023-closeout PASS with the benchmark-rework condition completed;
    main `ci` `39462589981` and `image` `39462589965` are both green, and the
    PR image job `39461251212` passed `MARIA_DB_LINK_OK` + demo gate. The 0023
    plan is archived under `completed/`. **0024 (owner publication)** is the
    next owner-only action; the worker must not start it or make GitHub
    visibility/metadata changes without an explicit owner instruction.

Stage numbering (decided 2026-10-08). It supersedes older references inside the
archived completed plans, which are historical evidence and are never rewritten:

| Stage | Scope | Plan |
|---|---|---|
| 0014 | China campus sources and manual capture | [`completed/0014-china-campus-sources-and-manual-capture.md`](../exec-plans/completed/0014-china-campus-sources-and-manual-capture.md) |
| 0015 | Resume tailoring (Chinese resume and ATS) | not started; labeled 0014 by `completed/0013-ai-matching.md` sections 3/14/17 |
| 0016 | Open-source packaging | not started |

Note added 2026-10-09 when 0014 closed: the 0014 row above records the
2026-10-08 decision and is preserved verbatim; the archived plan now lives at
[`completed/0014-china-campus-sources-and-manual-capture.md`](../exec-plans/completed/0014-china-campus-sources-and-manual-capture.md).

Note added 2026-10-10 when 0015 opened: **0015 is now active** (Phases A-C
complete, Phase D implemented through D5 — see `## Completed Plan (0015)`), and
**0015A is closed**; the table rows above are preserved verbatim.

Note added 2026-10-10 when 0015 closed: **0015 is complete and archived with a
green L4** (HEAD `26e2069`; see `## Completed Plan (0015)`), **0015A is closed and
archived**, and **both plans moved to `completed/`** —
[`completed/0015-resume-tailoring-and-explainable-matching.md`](../exec-plans/completed/0015-resume-tailoring-and-explainable-matching.md)
and
[`completed/0015A-job-list-usability-and-intake-quality.md`](../exec-plans/completed/0015A-job-list-usability-and-intake-quality.md).
**No plan is active**; **0016 open-source packaging** is next. The table rows
above stay preserved verbatim.

Note added 2026-10-10 when 0016 closed: **0016 is complete and archived with a
green L4** (HEAD `0a6e59e`; see `## Completed Plan (0016)`) and its plan moved to
[`completed/0016-open-source-packaging.md`](../exec-plans/completed/0016-open-source-packaging.md),
so the recorded MVP roadmap is complete through packaging and **no plan is
active**. The table rows above stay preserved verbatim.

Note added 2026-10-10 when 0017 closed: **0017 is complete and archived with a
green L4** (see `## Completed Plan (0017)`) and its plan moved to
[`completed/0017-ops-hardening-and-acceptance-closure.md`](../exec-plans/completed/0017-ops-hardening-and-acceptance-closure.md),
so **no plan is active**. The table rows above stay preserved verbatim.

0011 must remain:

```text
MySQL
Hard Filter
Rule Ranking
no LLM
no Embedding
no freshness lifecycle
no Search Engine
no RabbitMQ
```

Do not start a new P2 module without its own approved plan.

## Key Entry Points

- AI routing: [README.md](README.md)
- Most recent archived ExecPlan (0016):
  [../exec-plans/completed/0016-open-source-packaging.md](../exec-plans/completed/0016-open-source-packaging.md)
- Full historical state:
  [references/project-state-full.md](references/project-state-full.md)
- Current architecture:
  [../architecture/open-source-mvp.md](../architecture/open-source-mvp.md)
- Latest completed ExecPlan:
  [../exec-plans/completed/0013-ai-matching.md](../exec-plans/completed/0013-ai-matching.md)
- Competitive research note:
  [../research/2026-10-08-open-source-job-search-agents.md](../research/2026-10-08-open-source-job-search-agents.md)
- Product direction decision:
  [../adr/0011-china-first-sourcing-and-compliance.md](../adr/0011-china-first-sourcing-and-compliance.md)
- Latest completed ExecPlan:
  [../exec-plans/completed/0012-job-source-adapter-freshness.md](../exec-plans/completed/0012-job-source-adapter-freshness.md)
- Current ADR decisions: [../adr/README.md](../adr/README.md)
- Coding and testing rules:
  [../06-coding-standard.md](../06-coding-standard.md)
- API contract: [../04-api-design.md](../04-api-design.md)
- Database contract: [../03-database-design.md](../03-database-design.md)
- GitHub and tooling research:
  [references/github_resources.md](references/github_resources.md)

`docs/00-project-overview.md`, `docs/01-requirements.md`,
`docs/02-system-architecture.md`, `docs/05-development-plan.md`,
`docs/07-deployment.md`, `docs/08-execution-guide.md`, and
`docs/project-structure.md` remain historical design material, each marked
with a Superseded banner. Where they conflict with ADR-0007 through ADR-0010,
the current architecture document and accepted ADRs take precedence.
