# ExecPlan 0001: Repository Baseline Hardening

## Background

JobPilotAI has a working P1 network/HTTP implementation on disk, P2 database
and cache engine primitives, AI infrastructure, and project documentation. Most
of that work is uncommitted.

The repository has two build paths:

- `service/` is the documented and currently working build entry.
- The root `CMakeLists.txt` adds both `backend` and `service`; `service` also
  adds `backend`, causing duplicate targets.

Release tests define `NDEBUG`, disabling most `assert` checks. Docker Compose
also hardcodes development credentials that are inconsistent with
`.env.example`.

## Goal

Create a stable, verifiable baseline that can be submitted safely to GitHub:

- both supported build entries configure and build cleanly;
- CTest executes test assertions even under a Release configuration;
- Docker Compose uses environment-variable injection;
- third-party bcrypt has provenance documentation;
- `.gitignore` excludes generated and local files without hiding source;
- changes are split into logical commit groups without committing or pushing.

## Non-Goals

- Do not develop P1 further.
- Do not reimplement Poller or EpollPoller.
- Do not modify network behavior.
- Do not implement P2 business logic.
- Do not delete WIP DAO declarations.
- Do not commit or push.

## Constraints and Invariants

- Preserve all user changes and untracked source.
- Keep `service/` as the canonical build entry.
- Keep root CMake as a functional unified developer entry.
- Keep test code changes limited to assertion-preserving build configuration.
- Do not weaken warnings, tests, Sanitizer policy, or configuration safety.
- Do not expose real credentials.

## Selected Design

### CMake

Use the root project as a unified build aggregator:

- root project enables both C and CXX;
- root adds backend and service once;
- service adds backend only when `PROJECT_IS_TOP_LEVEL`;
- service explicitly disables C++ compiler extensions.

This preserves the documented `service/` workflow and repairs the root entry
without creating a second build-system design.

Rejected alternative: delete the root CMake file. This would create unnecessary
tooling incompatibility and history churn, while the duplicate-inclusion issue
can be fixed locally.

### Tests

Keep existing tests and assertion style. Before test targets are created, add
`-UNDEBUG` to the backend directory compile options so Release CTest executes
the existing C assertions.

Rejected alternative: rewrite every test to GoogleTest. That is a broad,
unrelated test refactor.

### Docker Configuration

Use required environment substitution for MySQL credentials and keep safe
defaults for non-secret database/user names. `.env.example` remains a
non-secret template.

### Third-Party

Keep `third_party/libbcrypt/` unchanged and add a provenance document. The
vendored source is needed by the current build.

## Phases

### Phase 1: CMake Baseline

- repair root and service inclusion rules;
- build `service/` cleanly;
- build root cleanly with the generated Conan toolchain.

### Phase 2: Test Assertion Configuration

- preserve assertions in backend test targets;
- verify `-UNDEBUG` appears after `-DNDEBUG`;
- run full CTest with live MySQL and Redis.

### Phase 3: Docker and Environment

- replace hardcoded compose credentials with environment substitution;
- align `.env.example`;
- validate Compose with `--env-file .env.example`.

### Phase 4: Third-Party Provenance

- document libbcrypt and crypt_blowfish origins and licenses;
- verify the vendored directory is not ignored and remains in the build.

### Phase 5: Git and Secret Review

- verify important source paths are not ignored;
- confirm only templates are visible for credentials;
- produce a commit grouping plan;
- leave all changes uncommitted for user approval.

## Verification

- configure and build a clean `service/` tree;
- configure and build a clean root tree;
- run full CTest with MySQL and Redis;
- validate Docker Compose using `.env.example`;
- inspect `git status`, tracked files, ignored files, and diffs;
- scan for credential-like files and hardcoded secrets.

## Progress

- [x] Phase 1: CMake baseline
- [x] Phase 2: Test assertion configuration
- [x] Phase 3: Docker and environment
- [x] Phase 4: Third-party provenance
- [x] Phase 5: Git and secret review

## Verification Results

- `service/` clean configure and Release build: passed.
- Root CMake clean configure and Release build: passed.
- `service/` CTest: 20/20 passed with live MySQL and Redis.
- Root CTest: 20/20 passed with live MySQL and Redis.
- Test compile flags contain `-DNDEBUG` followed by `-UNDEBUG`.
- Build flags use `-std=c++17` rather than GNU extensions.
- Docker Compose configuration validates with `.env.example`.
- No real secret was found; Compose now uses environment substitution.
- All source and AI infrastructure paths remain visible to Git.

## Risks

- Root builds still require the Conan-generated toolchain.
- CTest requires local MySQL and Redis services for all tests.
- Docker volume reuse may retain an old MySQL password after `.env` changes.
- The commit plan must keep the P2 engine dependency on vendored bcrypt coherent.

## Recommended Commit Grouping

1. `docs(ai): establish project takeover and AI infrastructure`
   - `AGENTS.md`
   - `.agents/skills/jobpilot-reactor-review/`
   - `docs/ai/`
   - `docs/exec-plans/`
   - all existing `docs/` project documentation

2. `feat(engine): add database and cache primitives`
   - `backend/include/jobpilot/database/`
   - `backend/src/database/`
   - `backend/include/jobpilot/cache/`
   - `backend/src/cache/`
   - `backend/tests/test_mysql_pool.cpp`
   - `backend/tests/test_sql_result.cpp`
   - `backend/tests/test_redis_client.cpp`
   - `backend/tests/test_bcrypt.cpp`
   - P2-related hunks in `backend/CMakeLists.txt`

3. `build(third_party): vendor libbcrypt with provenance`
   - `third_party/libbcrypt/`
   - `third_party/libbcrypt/PROVENANCE.md`

4. `test: preserve assertions and stabilize timing tests`
   - `-UNDEBUG` hunk in `backend/CMakeLists.txt`
   - `backend/tests/test_tcp_connection_write_buffer.cpp`
   - `backend/tests/test_redis_client.cpp`

5. `build: support root and service CMake entry points`
   - root `CMakeLists.txt`
   - `service/CMakeLists.txt`
   - remaining build-related hunks in `backend/CMakeLists.txt`

6. `chore(config): add safe local configuration template`
   - `.gitignore`
   - `.env.example`
   - `docker-compose.yml`
   - `service/config/config.yaml`

Commit 2 and commit 4 touch different hunks of `backend/CMakeLists.txt`. Use
hunk-level staging; do not combine unrelated P2 registration and test assertion
configuration in one commit.

## Follow-Up

Commit only after user approval. Push only after the commit history is reviewed.
