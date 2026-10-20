# Contributing to JobPilotAI

JobPilotAI is a local-first, self-hosted C++17 job-search workspace with Python
source adapters and a native no-build Web UI. Contributions are welcome when
they keep the project reproducible, auditable, and inside its non-goals.

Please read [`AGENTS.md`](AGENTS.md), [`docs/06-coding-standard.md`](docs/06-coding-standard.md),
and [`docs/ai/agent-workflow.md`](docs/ai/agent-workflow.md) before changing
code. AI agents have additional protocol rules in the handoff and review bus.

## Before you start

- Open an issue for a non-trivial change so the scope and verification level can
  be agreed first.
- Security reports go through [`SECURITY.md`](SECURITY.md), **not** a public
  issue.
- Do not include tokens, passwords, personal data, absolute home paths, or
  private network addresses in commits, tests, fixtures, logs, or screenshots.
- The project is local-first: no auto-apply/auto-submit, no login-wall bypass,
  no new product features without an owner-approved plan.

## Development setup

Prerequisites: Docker with Compose v2, CMake ≥ 3.22, a C++17 compiler,
`pkg-config`, the MySQL client development files, and Python 3 with pip.

```bash
git clone https://github.com/cjs1102/JobPilotAI.git
cd JobPilotAI

tools/dev/bootstrap.sh
cmake --preset debug
cmake --build --preset debug -j"$(nproc)"

cp .env.example .env       # fill MYSQL_ROOT_PASSWORD / MYSQL_PASSWORD
docker compose up -d       # MySQL + Redis
ctest --preset debug       # targeted labels are cheaper while iterating
```

The Web UI is optional:

```bash
docker compose --profile app build backend
docker compose --profile app --profile web up -d
# http://127.0.0.1:8081
```

If the host cannot pull `nginx:1.27-alpine`, use the documented Python fallback
in [`docs/guides/local-demo.md`](docs/guides/local-demo.md). The UI has no build
step; edit `web/` directly.

### Python dependencies

The two Python packages have exact, hash-pinned locks:

```bash
python3 -m venv /tmp/jobpilot-py
/tmp/jobpilot-py/bin/pip install --require-hashes -r source_adapters/requirements.lock
/tmp/jobpilot-py/bin/pip install --require-hashes -r ai_worker/requirements.lock

(cd source_adapters && /tmp/jobpilot-py/bin/python -m unittest discover -s jobpilot_sources/tests -t .)
(cd ai_worker && /tmp/jobpilot-py/bin/python -m unittest discover -s jobpilot_ai/tests -t .)
```

The lock targets the CI runtime (CPython 3.12 on manylinux x86_64). If the
runner Python/platform changes, regenerate the hash for the matching PyYAML
wheel:

```bash
python3 -m pip download --no-deps --dest /tmp/pyyaml-lock PyYAML==6.0.3
sha256sum /tmp/pyyaml-lock/*.whl
# update the PyYAML==6.0.3 --hash line, then verify:
python3 -m venv /tmp/jobpilot-py
/tmp/jobpilot-py/bin/pip install --require-hashes -r source_adapters/requirements.lock
```

Do not replace `--require-hashes` with a floating install.

## Branch, commit and PR flow

- Work on a feature branch (`feat/<stage>-<topic>` or `fix/<topic>`); never
  force-push `main`.
- Keep commits small and independently reviewable. Reference the ExecPlan stage
  when one exists.
- End commit messages with the repository's attribution convention when your
  agent requires it; do not add secrets or personal contact details.
- Open a pull request against `main` with:
  - what changed and why;
  - the verification commands and raw pass/fail output;
  - any environment limitations;
  - docs/README updates for changed behavior.
- The repository owner performs the merge (Create a merge commit). Do not merge
  or push `main` as a contributor/agent.
- A stage close requires the verification level in
  [`docs/ai/agent-workflow.md`](docs/ai/agent-workflow.md); do not claim a level
  that was not actually run.

## Code expectations

- C++17, RAII, explicit ownership/lifetimes, thread safety, and the existing
  Controller → Service → DAO direction.
- **P1 network/HTTP freeze**: do not change
  `backend/{include,src}/jobpilot/{network,http}/` or `service/src/router.cpp`
  without an approved plan and the Reactor review checklist.
- No secrets in code or tests; no real personal data in fixtures.
- No auto-apply/auto-submit path; `tools/checks/no_submission_path.py` must stay
  at exit 0.
- Update `docs/03`/`docs/04` when schema/API contracts change; keep README and
  the documentation index current.
- Prefer the standard library and existing dependencies; do not add a framework,
  build chain, or service for a small feature.

## Verification quick reference

```bash
tools/dev/bootstrap.sh
ctest --preset debug
python3 tools/checks/release_preflight.py
python3 tools/checks/no_submission_path.py
tools/checks/history_secret_scan.sh
git diff --check
```

The full L4 (Debug + ASan + UBSan + Release builds + Python suites) is run once
per stage, not on every edit. TSan is recorded separately when concurrency
changes are in scope.

## Code of conduct

A project-wide Code of Conduct is **deferred** until the owner provides a usable
enforcement/reporting channel. Until then, the baseline expectation is simple:
be civil, assume good faith, do not publish other people's private data, and
escalate security issues privately via [`SECURITY.md`](SECURITY.md).

## Licence

By contributing, you agree that your contribution is licensed under the
repository's MIT licence (see [`LICENSE`](LICENSE)).
