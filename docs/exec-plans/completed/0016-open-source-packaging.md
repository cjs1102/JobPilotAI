# ExecPlan 0016: Open-source Packaging, Demo Gate and CI

> Status: **Completed and archived** (2026-10-10). P1-P5 are landed (§14) and the
> stage's single L4 passed at HEAD `0a6e59e` (frozen tree, working tree clean) —
> builds `debug build rc=0`, `asan build rc=0`, `ubsan build rc=0`, `release
> rc=0`, `root-release rc=0`; the three CTest phases were re-run after the
> presets fix and report `100% tests passed, 0 tests failed out of 309` in Debug
> (96.12 s), ASan (88.86 s) and UBSan (59.93 s), sanitizer diagnostics 0;
> Python `Ran 446 tests` + `OK` (`source_adapters`) and `Ran 190 tests` + `OK`
> (`ai_worker`); backfill isolation dev `[  SKIPPED ]` + scratch
> `[  PASSED  ] 7 tests`. The debug/asan/ubsan build logs each also carry a
> `configure rc=1` from a generated `service/CMakeUserPresets.json` that had
> accumulated a scratch include; `tools/dev/bootstrap.sh` now writes that file
> inside a non-default `OUTPUT_FOLDER`, the stale include is removed and configure
> re-verified rc=0 — the binaries built rc=0 regardless. **No defect surfaced after
> P3**: the two the stage found
> were the gate's own — the MySQL healthcheck (P1) and the `main.cpp` artifact
> wiring (P3) — and both are already recorded in §17. The plan is frozen here as
> historical evidence.
>
> Historical approval status, preserved as written: "Status: **APPROVED — P0
> closed by the owner (2026-10-10).** All eight §11 decisions are ruled and
> recorded there. Implementation proceeds step by step from P1 (§9); each unit is
> committed and pushed only after its targeted verification, and the stage closes
> with the single L4 of §8.3. The plan was regrouped on approval: the former P2
> run-story work (Dockerfile, compose service, storage volume) merged into P1 so
> the `resume_storage` declaration gets its owner and its verification in the
> same unit; the later steps were renumbered P2-P6 with no change of content."
>
> Task level: **L4 at stage close** (the stage touches the build entry, the local
> run path, the database bootstrap story and CI; the individual slices are
> L0/L1/L2 and the stage runs **one** L4). Verification rules:
> `docs/ai/agent-workflow.md` §2, §2.2, §8; testing levels
> `docs/quality/README.md`.
>
> Dependencies: stages 0010-0015 are closed and archived. This plan reads the
> **recorded stage table** (0016 = open-source packaging) and treats the older
> roadmap numbering inside `docs/architecture/open-source-mvp.md` §10
> (`0015 Open-source Packaging / Demo / CI`, :224) as superseded (§16).
>
> Boundaries this stage must not change: ADR-0009 (deterministic first, LLM
> optional), ADR-0011 (China-first, never auto-submit), the 0015A intake gate and
> the 48 KiB description cap, `rule_v1` weights, and the D6 deferral recorded in
> the archived 0015 plan §17.

## 1. Objective

Make the repository deliver, from a clean machine and with no AI provider key,
the open-source flow the project already promises:

```text
git clone -> configure -> start -> import/discover -> search -> match -> tailor -> track
```

`docs/quality/README.md:77-78` records that gate; `open-source-mvp.md:22-23`
records the usability goal; `:63` and `:69-70` record that core tests must pass
without an LLM and that the documented local flow must stay runnable with MySQL
and Redis only. Today none of that is executable by an outsider: the build entry
requires a generated, gitignored Conan toolchain (`CMakePresets.json:16`) that no
documented command creates, the README's first command therefore fails on a fresh
clone (`README.md:148-161`), there is no run/distribution story
(`docker-compose.yml:3-37` starts only MySQL and Redis), and there is no CI
(`.github/` absent) although two docs describe one
(`docs/07-deployment.md:282`, `docs/08-execution-guide.md:1411-1445`).

The stage's deliverable is therefore **packaging, not features**: a reproducible
build contract, a documented one-command local run, a scripted demo gate with
recorded evidence, a minimal CI workflow, and the documentation/licence hygiene
an outside reader needs. Every user-facing capability it exercises already
exists; this stage makes them reachable and provable.

## 2. Background and Current State (file-level evidence)

Build entry

- Root CMake adds `backend` + `service` (`CMakeLists.txt:14-15`); root presets
  write to `build/presets/<preset>` and hard-wire
  `CMAKE_TOOLCHAIN_FILE=${sourceDir}/service/build/conan_toolchain.cmake`
  (`CMakePresets.json:13,16`); service presets use
  `${sourceDir}/build/conan_toolchain.cmake` (`service/CMakePresets.json:16`) and
  add the canonical runnable target `jobpilot_service`
  (`service/CMakeLists.txt:73`).
- The only dependency declaration is `service/conanfile.txt:1-11` with exact pins
  (spdlog/1.14.1, yaml-cpp/0.8.0, nlohmann_json/3.11.3, gtest/1.14.0,
  hiredis/1.2.0). There is **no `conan.lock`** anywhere in the tree.
- System dependency: `pkg_check_modules(MYSQLCLIENT REQUIRED IMPORTED_TARGET
  mysqlclient)` (`backend/CMakeLists.txt:85-90`).
- The generated Conan output is gitignored (`.gitignore:2-3`), Release-only, and
  embeds host-absolute cache paths
  (`service/build/spdlog-release-x86_64-data.cmake:16`), so it cannot be reused
  by anyone else.
- Conan 2.31.2 is installed with remote `conancenter =
  https://center2.conan.io` (host state: `~/.conan2/remotes.json:2-6`).
  **The repo's recorded claim that this environment cannot reach Conan Center
  (`docs/quality/README.md:12-16`) is stale**: a bounded Lead probe on
  2026-10-10 returned HTTP 200 from `https://center2.conan.io/v1/ping` (no
  proxy). This plan therefore treats a fresh-cache dependency install as
  verifiable here, and requires it as evidence (§8).
- Latent build-entry defect: `JP_ENABLE_TSAN` lists `test_tailor_rule` and
  `test_resume_artifact_dao` (`service/CMakeLists.txt:465-466`) **before** those
  targets are defined (`:528`, `:545`), so any TSan configure of the current
  `service/CMakeLists.txt` fails; the existing `service/build/presets/tsan` is
  stale from before those tests existed. Recorded in §17 and fixed in P1 only if
  the owner keeps TSan in scope.

Runtime

- `docker-compose.yml` has exactly two services: `mysql:8.0` (`:4`, real port
  `${JOBPILOT_MYSQL_HOST_PORT:-3306}:3306` `:16`, `MYSQL_ROOT_PASSWORD` /
  `MYSQL_PASSWORD` required `:11,:14`, `MYSQL_DATABASE` default `jobpilot` `:12`,
  volume `mysql_data` + `./database/schema:/docker-entrypoint-initdb.d:ro`
  `:18-19`, `mysqladmin ping` healthcheck `:20-24`) and `redis:7` (`:28`, port
  `:32-37`). Top-level volumes declare `mysql_data` and `resume_storage`
  (`:40-42`); **`resume_storage` is attached to no service**.
- Neither application has a compose service. The service is started by hand
  (`./build/presets/debug/service/jobpilot_service --config=/tmp/e2e.yaml`,
  `source_adapters/README.md:597-601`) from the repository root with
  `service/config/config.yaml` (`:41-45`); the worker is
  `python3 -m jobpilot_ai worker --config config.yaml` (`ai_worker/README.md:57-63`).
- Resume files are written by the service itself through
  `MakeLocalFileStorage(config.storage.root)` (`service/src/main.cpp:131-133`),
  with `storage.root` defaulting to the CWD-relative `storage`
  (`service/config/config.yaml`, the `storage:` block), and the documented run
  command starts the service from the repository root; a container therefore has
  to mount a volume at that path — which is what the unattached `resume_storage`
  declaration was for.
- `.env.example:2-26` documents MySQL/Redis variables, host/port overrides,
  `INTERNAL_API_TOKEN` (empty), storage limits and `APP_ENV`.

Database

- Schema files execute in lexical order `01_user` … `07_resume_artifact`
  (`database/schema/`); every file uses `CREATE TABLE IF NOT EXISTS`
  (`database/schema/README.md:9-11`). A fresh database is populated by MySQL's
  first-init run of the mounted directory (`docker-compose.yml:19`); the manual
  equivalent is the documented `for file in database/schema/*.sql` loop
  (`database/schema/README.md:15-19`).
- Later schema changes are **hand-applied numbered additive contracts** inside
  `docs/03-database-design.md` (§7 0012, §8 0013, §9 0014, §10 0015); re-running
  an `ALTER` reports a duplicate column by design (`docs/03-database-design.md:311-313`).
  There is no migration runner and no schema-version table.
- Tests read `MYSQL_HOST` (default `127.0.0.1`), `MYSQL_PORT` (3306),
  `MYSQL_USER` (default `root`), `MYSQL_PASSWORD` (required) and `MYSQL_DATABASE`
  (default `jobpilot`) (`backend/tests/test_schema.cpp:99-113`); `test_schema`
  asserts the live database, a freshly created one and a migrated pre-0015 one
  (`:299-300`, 13 tables `:175-189`).

Documentation state

- `README.md` has no quickstart; its only commands (`:148-161`) presuppose the
  generated toolchain, a release-preset build, a running MySQL/Redis and
  `MYSQL_PASSWORD`. It is Chinese-only and carries internal acceptance state
  (`:1-6`, `:106-109`).
- `docs/07-deployment.md:3-10` marks itself historical but still describes a
  gateway/`ai_service`/rabbitmq topology (`:76-84`), three Dockerfiles
  (`:128,:146,:166`), `ai_service/requirements.txt` (`:230`) and a
  `jobpilot_server` binary under `build/bin` (`:58`) — none of which exist;
  `docs/08-execution-guide.md:619` repeats the wrong binary path.
- Licence: MIT (`LICENSE:1-3`), the only licence file in the tree. Vendored code
  is `third_party/libbcrypt` with `PROVENANCE.md:7-24` naming both upstreams and
  their terms; there is no third-party notices file for distribution.
- No credential material is tracked (tree-wide scan for `sk-`/`ghp_`/`AKIA`
  returned 0 hits); `.env` is ignored while `.env.example` is kept
  (`.gitignore:25-27`).
- Packaging metadata: `ai_worker/pyproject.toml` and
  `source_adapters/pyproject.toml` exist (both PyYAML>=6.0, requires-python>=3.10);
  there is no root Python packaging, no `requirements*.txt`, no Dockerfile, no
  `cmake --install`/CPack rule and no CI.

## 3. Scope

### 3.1 In scope

1. **Build reproducibility**: a documented, scripted dependency-acquisition step
   that produces exactly the toolchain the presets expect, with the dependency
   set pinned (`conan.lock`) and the required system packages named.
2. **Clean-machine proof**: the ability to go from a fresh clone and an empty
   Conan cache to a built `jobpilot_service` plus green targeted tests, using
   only committed files and public registries (§8 CE-1/CE-2).
3. **Local run story**: one documented way to start MySQL + Redis + the service
   (and optionally the worker) from a clean checkout, including the `resume_storage`
   volume actually attached to the container path the service writes resumes to,
   `.env` handling and a health check.
4. **Database bootstrap and upgrade documentation**: the fresh-database path and
   the numbered additive-`ALTER` upgrade convention, written so a newcomer can
   apply them without guessing.
5. **Demo gate**: a committed, non-interactive script that runs
   `configure -> start -> import -> search -> match -> tailor -> track` against a
   local stack with **no AI key and no outbound network**, with assertions and
   recorded raw output, plus the sample dataset it needs.
6. **CI**: a minimal workflow that builds and tests on a clean runner with MySQL
   and Redis service containers and no AI key, using the same commands the
   demo/bootstrap use.
7. **Open-source hygiene**: README quickstart (English section added), internal
   acceptance state moved out of the README, third-party notices, correction of
   the stale claims in `docs/07`/`docs/08`/`docs/quality` that an outsider would
   otherwise follow, and a short reading-order index.
8. **The stage's documentation sync and one L4** at close.

### 3.2 Explicit Out-of-scope list (do not do in 0016)

1. Any new product feature or API. No dashboard/frontend (`open-source-mvp.md:149`
   keeps it "Good to Have"), no notifications, no interview preparation.
2. No new job source, no China adapter, no re-probe of 国聘/清华 (`0014` Step 5
   stays environment-limited), no crawler, no Playwright.
3. No AutoApply and no submission/auto-apply path of any kind
   (`open-source-mvp.md:156`; `tools/checks/no_submission_path.py` must stay
   rc=0).
4. No gateway/Nginx, RabbitMQ, multi-tenant SaaS, billing or RBAC
   (`open-source-mvp.md:152-156`).
5. No LLM authority over facts, scores or lifecycle; no un-deferring of 0015 D6.
6. No change to `rule_v1` weights, the 0015A intake gate, the 48 KiB cap, the P1
   network/HTTP behaviour, or the frozen `resume_versions`/fact-gate contracts.
7. No migration runner and no schema-version table (§11 decision 7) unless the
   owner asks for one.
8. No dev-database backfill `apply`; the backfill stays dry-run only.
9. No second L4 and no re-run of already-passed full matrices
   (`agent-workflow.md:37-46`).
10. No rewrite of archived plans or of any historical evidence section.

## 4. Reused vs New (the honesty table)

| Thing | Reused (exists today) | New in 0016 |
|---|---|---|
| C++ build system | CMake presets, `service/conanfile.txt` pins | lockfile, bootstrap script, toolchain path that a documented command actually creates |
| Dependency resolution | Conan 2 + `center2.conan.io` | pinned lock, documented remote/profile settings, fresh-cache proof |
| Runtime infra | `docker-compose.yml` MySQL + Redis, healthchecks | app service(s) behind a compose profile, Dockerfile(s), attached storage volume |
| Service start | hand-run binary + `service/config/config.yaml` | documented one-command start + health check |
| Schema | `01..07*.sql`, initdb mount, documented loop | upgrade/bootstrap documentation, isolation rules for demo databases |
| Gate capabilities | search, ranking, rule matching, tailoring, applications | the scripted end-to-end gate and its assertions |
| Tests | 309 CTest cases + Python suites | the CI job running the affected subset on a clean runner |
| Docs | README (zh), `docs/03/04`, quality rules | quickstart (en), reading order, third-party notices, corrected historical docs |
| Licence | MIT `LICENSE` | third-party notices file |
| CI | none | one minimal workflow |

## 5. Contracts

### 5.1 Clean-machine build contract (C1)

From a fresh clone, with only documented prerequisites installed, these commands
must succeed in order and produce the service binary — `build/presets/release/service/jobpilot_service`
when the root entry is used, `service/build/presets/release/jobpilot_service` when
the `service/` entry is used:

```bash
# prerequisites: cmake>=3.22, a C++17 compiler, pkg-config, libmysqlclient-dev,
# python3 + pip (for Conan 2). tools/dev/bootstrap.sh checks each one and names
# whatever is missing.
tools/dev/bootstrap.sh          # conan install, pinned by service/conan.lock
cmake --preset release          # root entry: backend + service
cmake --build --preset release -j"$(nproc)"
```

Rules: the script is idempotent; it creates the toolchain at the path the presets
expect; it never writes into a gitignored directory that the build then treats as
input evidence; failure prints the exact missing prerequisite. The concrete
script name, preset and flags are settled in P1 but the *order and the inputs*
above are the contract.

### 5.2 Local run contract (C2)

One documented sequence starts a working stack from a clean checkout: copy
`.env.example` to `.env`, `docker compose up -d`, start the service (compose
profile or script), and `GET /health` answers 200 (`service/src/router.cpp:139`).
Everything runs on `127.0.0.1`; no AI key is required; no component reaches the
public internet during the demo.

### 5.3 Demo-gate contract (C3)

A committed script runs the whole recorded gate non-interactively and exits
non-zero on the first failed assertion:

```text
configure -> start -> import (sample dataset) -> search -> match (rule_v1, no key)
          -> tailor (deterministic rule_v1) -> track (application status) -> stop
```

It must print, for each leg, the request it made and the assertion it checked,
and it must not touch the developer's existing `jobpilot` database (§7).

### 5.4 What packaging must not change

Public behaviour, contracts, schema semantics, ranking weights and the
no-submission guarantee. If a packaging goal can only be met by changing
behaviour, that is a stop condition (§13), not a licence to change it.

## 6. Selected Design

- **P1 build and run contract.** Keep Conan 2 and the existing exact pins; add a
  committed `conan.lock` generated from `service/conanfile.txt`, pin
  `compiler.cppstd=gnu17` and `build_type=Release` explicitly in the bootstrap
  step (the setting the verified toolchain and the lock were resolved with —
  `service/build/conan_toolchain.cmake` sets `CMAKE_CXX_EXTENSIONS ON`, and the
  project's own C++17 requirement is enforced by `CMAKE_CXX_STANDARD 17`), and add
  `tools/dev/bootstrap.sh`, which resolves the Conan binary (it lives outside
  `PATH` for non-interactive shells on this host) and runs `conan install` with
  `--output-folder` at the path both entries' presets expect. On the same unit,
  add one multi-stage `Dockerfile` (builder = bootstrap + build; runtime = binary
  + config) that reuses that script rather than duplicating it, add the service
  as a compose service behind a profile so `docker compose up -d` still gives an
  infra-only stack by default, and attach `resume_storage` to the path the
  service writes resumes to (`service/src/main.cpp:131-133`). The worker stays
  optional and documented, not required. Rationale: the script is the smallest
  change that turns a generated, machine-specific artifact into a documented,
  reproducible one, and the container is what gives the storage declaration an
  owner (decision 1 and 2, §11).
- **P2 database.** Document the two real paths (fresh initdb mount; numbered
  additive `ALTER`s for an existing database) in one place, name the
  duplicate-column behaviour explicitly, and give the demo a dedicated database
  name created by the demo itself.
- **P3 gate script.** One shell entry point under `tools/dev/` (stdlib + curl +
  the compose CLI only) with `set -euo pipefail`, an isolated compose project
  name, its own database, a trap that tears the isolated stack down, and a
  committed sample dataset derived from `docs/examples/job-import.json`
  (`docs/examples/job-import.json:1-11`, whose URLs are `example.invalid`, so the
  demo stays offline).
- **P4 CI.** Three jobs: `build` provisions the pinned graph, configures and
  compiles the whole tree on a clean runner; `integration` runs the **full**
  CTest suite plus both Python suites against `mysql:8.0` + `redis:7` service
  containers with an isolated CI database (`jobpilot_ci`) and dedicated
  credentials; `image` builds the committed Dockerfile and runs the demo gate
  against it, which is where CE-5 gets its evidence. The first draft split the
  suite by the `external` label; replaying that locally proved the filter wrong
  (309 tests, only 17 labelled `external`/`integration`, 59 with no label, and 52
  database-backed cases fail without a database), so the compile-only /
  whole-suite split replaced it and the finding is recorded in §12. The
  workflow's command list is replayed locally in the same order before it is
  committed, because the first real run happens on GitHub after the push; TSan is
  fixed but stays out of the required matrix (decision 8).
- **P5 hygiene.** Add an English "Quick start" to `README.md` above the existing
  Chinese content rather than rewriting it; move the internal acceptance
  paragraphs to `docs/ai/project-state.md`; add a short orientation note for
  `AGENTS.md`/`docs/ai/*` and verify those files carry no secret or local-only
  private detail (decision 6); add `THIRD_PARTY_NOTICES.md` summarising
  `third_party/libbcrypt` (from its `PROVENANCE.md`) and the Conan and Python
  runtime dependencies; add dated correction blocks to `docs/07` and `docs/08`
  pointing at the quickstart; correct the stale Conan-reachability record in
  `docs/quality/README.md:12-16`.

## 7. Invariants

1. **No submission path**: `python3 tools/checks/no_submission_path.py` stays
   rc=0 after every unit.
2. **The developer's data is not the demo's data**: every clean-environment and
   demo run uses its own compose project name, its own volume set and its own
   database name. `docker compose down -v` is only ever run with that isolated
   project name. The existing `jobpilot` and `jobpilot_review_scratch` databases
   must be untouched by 0016 (the recorded observation — `jobs` 4 rows, all
   `review_status=0`, `job_review_items` 0 rows — is re-checked and reported
   before and after the stage's verification runs).
3. **No secrets**: no key/token/password is committed; `.env` stays ignored;
   `.env.example` keeps names only.
4. **No AI requirement**: every automated gate, test and CI job passes with no
   provider key and with no outbound network except package installation.
5. **Behaviour freeze**: public API, schema semantics, ranking weights, fact gate
   and lifecycle stay as closed in 0010-0015.
6. **Generated artifacts stay out of git**: build outputs and Conan output remain
   gitignored; only inputs (lockfile, profile, scripts, Dockerfile, workflow) are
   committed.
7. **Docs tell the truth at the level verified**: anything not executed is marked
   unverified/environment-limited, per `agent-workflow.md` §2 and the 0014
   precedent.

## 8. Testing Plan (targeted slices + clean-environment reproduction + ONE L4)

Change-type minimums follow `agent-workflow.md:54-63`: documentation-only edits
are L0 (`git diff --check`), script/config edits are L1/L2 (the script runs
themselves plus the tests their configuration affects), and the stage closes with
one L4.

### 8.1 Clean-environment reproduction (the stage's acceptance core)

| ID | What it proves | How it is run | Evidence recorded |
|---|---|---|---|
| CE-1 | A fresh clone builds with nothing but committed files | clone the pushed commit into a temp directory, run C1's commands | exit codes + the produced binary path + `--help`/`--version` output |
| CE-2 | The build does not depend on this host's Conan cache | run bootstrap with an isolated `CONAN_HOME` (fresh cache) and network available | raw `conan install` summary line, package count, exit code |
| CE-3 | A newcomer can start and use the stack | isolated compose project + empty volumes, then C2 and C3 | `/health` response, gate script output, per-leg assertions |
| CE-4 | CI's command list is not fiction | replay the workflow's exact command sequence locally, in order | per-command exit codes |
| CE-5 | The container path builds | `docker build` of the P1 Dockerfile (network at image-build time) | image id + a container run answering `/health` |

CE-2/CE-5 are the only network-dependent steps: if the network fails, the step is
recorded **environment-limited with the raw error** and the stage continues — it
is never marked green without evidence. CE-2 is currently expected to succeed:
the stale "cannot reach Conan Center" record was disproved by a 200 from
`center2.conan.io/v1/ping` on 2026-10-10 (§2).

**Environment note (2026-10-10, P1).** CE-2 succeeded from a fresh `CONAN_HOME`.
CE-5 could **not** run for the committed `ubuntu:24.04` base: Docker Hub is
unreachable from this host — the daemon is bound to a dead HTTP proxy configured
in `/etc/systemd/system/docker.service.d/http-proxy.conf` (the address is a local
environment detail and is deliberately not repeated in this repository), the
registry name resolves to an address that refuses connections, and the pull fails
with `failed to resolve source metadata for docker.io/library/ubuntu:24.04 ... EOF`.
The same commands were therefore verified in a **scratch image built from a
locally cached base** (`redis:7`, Debian bookworm): bootstrap inside the
container, `cmake --preset release --target jobpilot_service`, the service
answering `/health` against the compose MySQL/Redis, and an uploaded resume's
bytes surviving a container recreation inside `resume_storage`. That is evidence
for the container, compose and volume mechanics, **not** for the `ubuntu:24.04`
package names. The P4 CI job that builds the committed image is where CE-5 gets
its real evidence; until the owner reports that run, the image must not be
described as verified.

### 8.2 Targeted slices

- P1: CE-1 + CE-2 + a configure-only run of both entries (root and service) + the
  TSan configure + CE-5 + `docker compose config` showing the storage volume
  attached, and the infra-only default stack still coming up.
- P2: a fresh isolated database bootstrapped from `database/schema/*.sql` and the
  documented upgrade path applied to a copy — no test in the existing suites is
  re-run for this (their evidence stands).
- P3: CE-3, plus a deliberately failing assertion to prove the script's exit code
  is non-zero on failure (the negative control).
- P4: CE-4 + YAML lint/`actionlint` if available locally; the first real CI run
  is reported by the owner after the push, not claimed here.
- P5: L0 (`git diff --check`) + a link check over the changed documents, in the
  shape already used for the 0015 archival (28 links, 0 missing) + the
  secret/private-detail scan decision 6 asks for.

### 8.3 Stage close

One L4 exactly as recorded in `docs/quality/README.md` (Debug/ASan/UBSan CTest,
service and root Release builds, both Python suites, the review-backfill
isolation), on a frozen commit, with the raw logs archived under the gitignored
`build/` tree — the 0015 pattern, including the rule that a defect found by the
gate is fixed and only the affected levels are re-run.

## 9. Phased Steps (dependencies + acceptance criteria)

| Step | Work | Depends on | Acceptance criteria |
|---|---|---|---|
| **P0** | Owner reviewed this plan and ruled §11 (done, 2026-10-10) | — | approval recorded in §11; no implementation started before it |
| **P1** | Build and run contract (C1, C2): `conan.lock`, pinned settings, `tools/dev/bootstrap.sh`, the TSan target-order fix, multi-stage `Dockerfile`, compose service behind a profile, `resume_storage` attached to the service's storage root, `.env` handling, `/health`; worker documented as optional | P0 | CE-1 and CE-2 pass; CE-5 passes for a locally cached base and stays open for the committed `ubuntu:24.04` base until the P4 CI run (environment-limited, §8.1); `cmake --preset tsan` configures; bootstrap is idempotent; both entries configure and build; `docker compose config` shows the volume attached and the default stack still starts infra only; `/health` returns 200 against the compose MySQL/Redis; an uploaded resume's bytes survive a container recreation inside `resume_storage`; the isolated stack tears down cleanly |
| **P2** | Database bootstrap + upgrade documentation, demo database isolation | P1 | a fresh isolated database reaches the expected 13 tables from committed files; the upgrade path is documented and applied once to a copy; the dev database is verified untouched |
| **P3** | Demo gate (C3): `tools/dev/` gate script + extended `docs/examples/job-import.json` + assertions | P1, P2 | CE-3 passes end to end with no AI key and no outbound network; the negative control exits non-zero; raw output archived |
| **P4** | CI workflow: a clean-build job, a whole-suite job with MySQL/Redis service containers and an isolated CI database with dedicated credentials, and an image job that builds the committed Dockerfile and runs the gate; no AI key, TSan out of the required matrix | P1, P3 | CE-4 passes (the workflow's command list replayed locally, whole suite green against the CI-shaped database); the workflow file validates; the owner confirms the first real GitHub run afterwards, which is also where CE-5 closes |
| **P5** | Open-source hygiene: README quickstart (en) + internal state moved out, `AGENTS.md`/`docs/ai` orientation note + secret/private-detail check, `THIRD_PARTY_NOTICES.md`, `docs/07`/`docs/08` dated corrections, `docs/quality:12-16` correction, reading-order index | P3 | L0 + link check clean; an outsider can follow README to a running demo without reading any agent document; no secret or local-only detail in the public docs |
| **P6** | Docs sync + the stage's single L4 + archive | P1-P5 | full L4 green on a frozen commit; README/AGENTS/project-state/this plan consistent; plans archived under `docs/exec-plans/completed/` |

## 10. Compatibility and Rollback

- All additions are additive: a new lockfile, a new script, an optional compose
  profile, a new Dockerfile, a new workflow, new documentation. The existing
  developer flow (`cmake --preset debug` after a manual `conan install`) keeps
  working.
- Rollback of any single step is `git revert` of that commit; nothing needs a
  data migration, and no step may rewrite history or drop an existing volume.
- The demo's isolated database/volumes are disposable by construction; the
  developer's `jobpilot` database is never part of the stage's writes.

## 11. Decisions (owner-level, CLOSED 2026-10-10 at P0)

All eight decisions were ruled by the owner when approving this plan. They are
recorded here verbatim in substance, together with what each ruling forbids, so a
later reader can tell a decision from an accident.

1. **Distribution shape — Dockerfile + Docker Compose profile.** The service is
   packaged as a multi-stage image and added as a compose service behind a
   profile; documented local build remains supported. *Forbids:* a
   documentation-only run story with no container, and any container that starts
   by default and changes the existing infra-only `docker compose up -d`.
2. **Dependency acquisition — commit `conan.lock`, document the remote, do not
   vendor.** No dependencies are copied into the tree. *Forbids:* a lockfile-free
   build that depends on the host cache, and any vendoring that would drag in
   per-dependency notice work this stage did not plan for.
3. **CI — GitHub Actions with MySQL/Redis service containers, an isolated CI
   database and dedicated credentials.** *Forbids:* CI that points at a developer
   or shared database, reuses developer credentials, or needs an AI key; and
   claiming a green CI run before the owner reports the first real run.
4. **README language — keep the Chinese README, add an English Quick Start, no
   full bilingual rewrite.** *Forbids:* translating or restructuring the existing
   Chinese content wholesale, or deleting it.
5. **Demo dataset — extend `docs/examples/job-import.json`.** *Forbids:* adding a
   second competing primary example dataset.
6. **Agent-facing documents — keep `AGENTS.md` and `docs/ai/*` in the public
   tree, with a short orientation note in the README, and check them for secrets
   and local-environment private detail.** *Forbids:* publishing agent routing or
   local paths/credentials without review, and moving or deleting those documents.
7. **Schema upgrades — keep the documented, hand-applied numbered additive
   `ALTER`s; no migration runner in this stage.** *Forbids:* adding a runner, a
   schema-version table, or any automatic migration that could touch an existing
   database without a human reading it.
8. **TSan — fix the `JP_ENABLE_TSAN` target-definition-order defect; do not put
   TSan into the required CI matrix.** *Forbids:* leaving the configure defect in
   place, and requiring TSan for CI to be considered green.

Two consequences of the rulings are binding on execution: the storage volume gets
its owner in P1 (ruling 1, see §9), and the CI database isolation of ruling 3 is
the same isolation invariant §7.2 already imposes on every demo run.

## 12. Risks

1. **Network-dependent evidence**: CE-2/CE-5 need a reachable registry; mitigated
   by the 2026-10-10 probe and by the environment-limited record rule.
2. **Scope creep**: packaging work invites "while we are here" features; §3.2 and
   §13 make that a stop condition.
3. **Touching developer data**: a demo that reuses the `jobpilot` database would
   destroy real state; §7.2 makes isolation an invariant and a re-check.
4. **CI cannot be proven green locally**: only its command list can; the plan
   says so instead of claiming a green run.
5. **Documentation churn**: README/`docs/07`/`docs/08`/`docs/quality` all carry
   stale text; the plan corrects only what an outsider would follow, with dated
   notes, and refuses to rewrite archives.
6. **Build-entry brittleness**: the presets hard-wire generated toolchain paths;
   P1 must keep both entries working rather than fixing one and breaking the
   other.
7. **The test suite's labels do not mark database dependence.** Measured on the
   release preset: 309 tests, 17 labelled `external`/`integration`, 59 with no
   label at all, and 52 cases across nine suites fail when run without a
   database. A label-based split of CI would therefore run database-backed tests
   with no database; the first CI draft did exactly that and the local replay
   caught it, which is why CI compiles in one job and runs the whole suite in the
   database job.

## 13. Stop Conditions

Stop and report to the owner (do not widen scope) if:

1. A step needs a **new product feature** or an API change to satisfy the gate.
2. The gate can only pass with an **AI provider key**, an outbound network call
   at run time, or a submitted application.
3. Verifying a step would require **writing to the dev database**, running a
   backfill `apply`, or dropping an existing volume.
4. The work would touch the 0015A intake gate, the 48 KiB cap, `rule_v1` weights,
   P1 network/HTTP behaviour, the fact gate, or the D6 deferral.
5. The clean-environment build cannot be verified on this host even after the
   documented prerequisites — record it environment-limited with the raw error
   and stop at that step.
6. The stage appears to need **more than one L4** or a second full matrix
   (`agent-workflow.md` §8 budget rule).
7. A packaging goal conflicts with a closed contract in `docs/03`/`docs/04`.
8. CE-3 shows the existing capabilities do not actually satisfy the gate (that
   would be a defect story, not a packaging story — report it with evidence).

## 14. Progress Checkboxes

- [x] P0 Owner review of this plan — **approved 2026-10-10, all eight §11
      decisions ruled**
- [x] P1 Build and run contract — **done 2026-10-10** (commits `2b94ee2` for the
      lock, `tools/dev/bootstrap.sh` and the TSan fix; `838beef` for the
      Dockerfile, the compose `app` profile and the `resume_storage` mount).
      Evidence: **CE-2** from a fresh `CONAN_HOME` and a scratch output folder —
      `Downloading 5 packages`, `gtest/1.14.0: Building from source`,
      `Install finished successfully`, `conan_toolchain.cmake` 8240 B, 21 MB
      cache; `cmake --preset tsan` configures rc=0, with a three-line negative
      control showing the old ordering is a configure error; **CE-1** — a fresh
      clone of `838beef` (clean tree), no generated toolchain in the clone,
      `cmake --preset release` failing before bootstrap and succeeding after it,
      both the root entry and the `service/` entry producing a 2960344-byte
      `jobpilot_service`, `ce1_rc=0`; **run story** — isolated compose stack,
      `/health` 200, upload `resume_id 1` with `text_extracted=true` /
      `text_status=ok`, the stored bytes matching the upload both through the
      container path and through the named volume, the row and the bytes both
      surviving a container recreation, clean teardown; the developer's
      containers, volumes and database untouched before and after (`jobs` 4 rows,
      all `review_status=0`, `job_review_items` 0, `resume_artifacts` 0). The
      MySQL healthcheck defect this found is recorded in §17. **CE-5 stays open
      for the committed `ubuntu:24.04` base** (Docker Hub unreachable here, §8.1)
      and is the P4 CI job's to close; the scratch-base run is recorded as
      exactly that.
- [x] P2 Database bootstrap and upgrade documentation — **done 2026-10-10**.
      `database/schema/README.md` now carries the file→table map, the two
      from-scratch paths, the numbered additive-`ALTER` upgrade table with the
      duplicate-column caveat, the verification queries and the isolation rule;
      `docs/03` §6 points at it instead of claiming every script is idempotent.
      Evidence: an isolated MySQL instance built from the committed scripts holds
      exactly the 13 documented tables; the §10.3 block applies cleanly to a copy
      whose three columns were dropped (all three nullable, default NULL) and
      reports `ERROR 1060 Duplicate column name` when re-run, which is what the
      documents promise; the developer's database was 4/4/0/0 before and after.
- [x] P3 Demo gate — **done 2026-10-10**. `tools/dev/demo-gate.sh` runs
      configure → start → import → search → match → tailor → track against an
      isolated compose project, and `docs/examples/job-import.json` grew a third
      posting so search and match have contrast. Evidence: `gate_rc=0` with
      `DEMO_GATE_OK` and all eight legs passing — import `created=3 updated=0
      total=3 rejected=0 needs_review=0`; search returned `C++ Backend Engineer`;
      the match leg saw a scored row (`score=8.75 version=rule_v1 reasons=2`);
      tailor produced `fact_check_status=1 confirmed=False generator=rule_v1
      source_version=tailor_rule_v1` and confirmation flipped it to
      `confirmed=true`; track advanced the application one lifecycle step
      (`status 0 -> 1`). The negative control exits 1 with
      `FAIL: search returned no rows`, so the gate cannot report success falsely.
      No AI key and no outbound network were used, and the developer's database
      was 4/4/0/0 before and after. The run found and fixed the `main.cpp` wiring
      defect recorded in §17.
- [x] P4 CI workflow — **done 2026-10-10**. `.github/workflows/ci.yml` has three
      jobs: `build` (pinned bootstrap + configure + compile on a clean runner),
      `integration` (MySQL 8.0 + Redis 7 service containers, `jobpilot_ci` with
      its own credentials, schema loaded from the committed files, an explicit
      grant for the schema test, the **whole** CTest suite and both Python
      suites), and `image` (builds the committed Dockerfile and runs the demo
      gate against it — CE-5's only possible home). Evidence, replayed locally in
      order against an isolated MySQL/Redis pair using the workflow's own values:
      bootstrap/configure/build rc=0; schema from 7 files → 13 tables; grant
      applied; `100% tests passed, 0 tests failed out of 309` (41.26 s);
      `source_adapters` `Ran 446 tests` + `OK`, `ai_worker` `Ran 190 tests` +
      `OK`. The first draft's `-LE external` split was abandoned after this replay
      showed the labels do not mark database dependence (§12.7). The `image` job
      is **not** replayable here (Docker Hub unreachable, §8.1), so CE-5 and the
      first real CI run are the owner's to confirm after the push.
- [x] P5 Open-source hygiene — **done 2026-10-10**. `README.md` gained an English
      Quick Start (decision 4) built from the commands P1-P4 actually verified,
      the bookmarklet item now states the user-facing boundary instead of quoting
      the acceptance record, and a "contributors and AI agents" pointer leads to
      `AGENTS.md`/`docs/ai/` (decision 6). New `THIRD_PARTY_NOTICES.md` records the
      vendored `libbcrypt` terms from its PROVENANCE plus the Conan and Python
      dependencies (fmt=MIT and GoogleTest=BSD-3-Clause taken from the recipe and
      package metadata; the others point at the upstream licence rather than
      asserting one). `docs/07` and `docs/08` carry dated corrections naming the
      parts that cannot work, the `docs/quality` dependency-boundary note is
      corrected for the now-reachable registry, and `docs/README.md` is the
      reading order. The secret/private-detail scan over tracked files found and
      redacted two private proxy addresses (this plan and
      `docs/ai/references/project-state-full.md`); the remaining private-range
      strings are test fixtures. Evidence: `git diff --check` rc=0; link check 55
      links, 0 missing.
- [x] P6 Docs sync + the stage's single L4 + archive — **done 2026-10-10**. The
      stage's single L4 passed at HEAD `0a6e59e` (frozen tree, working tree clean):
      builds `debug build rc=0`, `asan build rc=0`, `ubsan build rc=0`, `release
      rc=0`, `root-release rc=0`; the three CTest phases were re-run after the
      presets fix (`build/l4-0016/test2-*.log`, `test2-summary.txt` rc=0) and
      report `100% tests passed, 0 tests failed out of 309` in Debug (96.12 s),
      ASan (88.86 s) and UBSan (59.93 s) with sanitizer diagnostics 0; Python
      `source_adapters` `Ran 446 tests` + `OK` and
      `ai_worker` `Ran 190 tests` + `OK`; backfill isolation dev
      `REVIEW_DB_ISOLATION connected=jobpilot scratch_env=<unset>` + `[  SKIPPED ]`
      (rc=0) and scratch `connected=jobpilot_review_scratch
      scratch_env=jobpilot_review_scratch` + `[  PASSED  ] 7 tests` (rc=0).
      **Note**: the debug/asan/ubsan build logs each also carry a `configure rc=1`
      from a generated `service/CMakeUserPresets.json` that had accumulated a
      scratch include; `tools/dev/bootstrap.sh` now writes that file inside a
      non-default `OUTPUT_FOLDER` and configure re-verified rc=0 — the binaries
      built rc=0 regardless. README,
      AGENTS.md and `docs/ai/project-state.md` are synced and this plan is
      archived under `docs/exec-plans/completed/`. **No defect surfaced after
      P3**; the two the stage found (MySQL healthcheck, `main.cpp` wiring) are
      recorded in §17. The CI `image` job remains the owner's to confirm on the
      first GitHub run.

## 15. Smallest First Implementable Unit

**P1 is the smallest unit that unblocks everything else and can be verified
alone**, and it runs as two commits. First: commit the pinned `conan.lock` plus
`tools/dev/bootstrap.sh` and the TSan target-order fix, run CE-1 and CE-2, and
record the raw output. Second: the multi-stage `Dockerfile`, the compose service
behind a profile and the `resume_storage` attachment, verified by CE-5 and
`docker compose config`. Until the first commit works, no other packaging claim —
one-command start, demo gate or CI — is honest, because every one of them starts
from a build that a clean machine currently cannot reproduce.

## 16. Stage Numbering (unchanged)

The recorded stage table (`docs/ai/project-state.md`, 2026-10-08 decision) keeps
**0016 = open-source packaging**, with 0015 = resume tailoring. The roadmap block
inside `docs/architecture/open-source-mvp.md` §10 (`:219-234`) predates that
decision and still labels packaging as 0015; it is superseded and is **not**
rewritten (archived/historical text is never rewritten here). This plan is the
0016 entry.

## 17. Known Issues and Follow-ups

- **Fixed in P6, found by the L4's own build logs: the service-entry configure
  failed with `Duplicate preset: "conan-release"`.** The cause was generated
  state, not tracked code: `service/CMakeUserPresets.json` (gitignored, written
  by Conan) had accumulated a second include pointing at the scratch output folder
  a verification run had used (`build/ce2-out/CMakePresets.json`), and both
  includes define the same `conan-release` names, so CMake refused to read the
  presets from `service/` at all. `cmake --build` then auto-reconfigured, which is
  why the binaries and the suites were unaffected. `tools/dev/bootstrap.sh` now
  passes
  `-c tools.cmake.cmaketoolchain:user_presets=$OUTPUT_FOLDER/CMakeUserPresets.json`
  whenever `OUTPUT_FOLDER` is not the default, so a scratch verification run
  writes its presets inside the scratch folder and can never append to the source
  tree again — verified by running it: the source-tree file keeps its single
  include and the scratch file lands under `service/build/<folder>/`. The stale
  include was removed; `cmake --preset debug|asan|ubsan` from `service/` and
  `cmake --preset release` from the root all return rc=0, and the three CTest
  phases were re-run green afterwards (`build/l4-0016/test2-*.log`).
- **Fixed in P3, found by the demo gate: the shipped binary never wired the
  artifact endpoints.** `service/src/main.cpp` built `ResumeController` with only
  the resume service, so the controller's `artifactService_` stayed null and the
  four `/api/v1/resumes/artifacts` routes answered 500 — while `test_resume_http`
  stayed green, because that test constructs the controller with the artifact
  service itself. This is exactly the gap the D5 tests could not see and the
  reason the gate runs the real binary. The artifact DAO and service are now
  constructed after `JobDao` (which they need) and the controller receives the
  service; `ResumeHttpTest`, `ResumeArtifactDaoTest` and `ResumeServiceTest` were
  re-run afterwards (15/15), and the gate's tailor leg is the regression test.
- **Fixed in P1 while verifying the run story:** the MySQL service's healthcheck
  was `mysqladmin ping -h localhost`, which uses the unix socket and therefore
  succeeds while the entrypoint's first-init server is still running with
  networking disabled. A dependent service could start before the database was
  reachable and fail its first query (observed as
  `mysql_real_connect failed: Can't connect to server on 'mysql' (115)` in the
  isolated stack, while the same request succeeded once MySQL had settled). The
  healthcheck now pings over TCP as the application user, which also proves the
  user and the schema load are finished.
- **Docker Hub is unreachable on this host** (the daemon is bound to a dead HTTP
  proxy and the registry name resolves to an unusable address), so the committed
  `Dockerfile`'s `ubuntu:24.04` base cannot be pulled here and CE-5 stays open for
  that base until the P4 CI job builds it. This is an environment condition; the
  stage deliberately does not "fix" the daemon proxy or the registry.
- `JP_ENABLE_TSAN` named two targets before they were defined
  (`service/CMakeLists.txt:465-466` vs `:528,:545`): any TSan configure of the
  current tree failed. **Decision 8: fixed in P1, TSan stays out of the required
  CI matrix.**
- `docker-compose.yml:40-42` declared `resume_storage` with no service attached.
  **Resolved in P1**: the real write path is
  `MakeLocalFileStorage(config.storage.root)` (`service/src/main.cpp:131-133`)
  and resumes land at `<root>/<userId>/resumes/<resumeId>/v<version><ext>`
  (`service/src/resume/resume_service.cpp:55-66`), so the volume is mounted at
  the service container's storage root (`/app/storage`, with
  `STORAGE_ROOT=/app/storage`), and P1 verified it by uploading a resume in the
  isolated stack and re-reading its exact bytes after recreating the containers.
- `docs/quality/README.md:12-16`'s "cannot reach Conan Center" record is stale
  (200 from `center2.conan.io/v1/ping`, 2026-10-10); P5 corrects it with a dated
  note rather than deleting the original.
- `docs/07-deployment.md` and `docs/08-execution-guide.md` are historical and
  contain commands that cannot work (`jobpilot_server` under `build/bin`,
  `ai_service/requirements.txt`, three non-existent Dockerfiles); P6 adds dated
  corrections, it does not rewrite them.
- The README is Chinese-only and carries internal acceptance state; P6 adds an
  English quickstart and moves the internal state to `docs/ai/project-state.md`.
- There is no root Python packaging, so `python3 -m jobpilot_sources …` only works
  from `source_adapters/`; **P5 documents that explicitly** (root packaging is a
  packaging decision the owner did not ask for, and adding it is out of scope
  under §3.2.1).
- The first CI run can only happen on GitHub after a push; until the owner
  reports it, the workflow's status must be described as "commands replayed
  locally", never as "CI green".
