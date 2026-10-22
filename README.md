# JobPilotAI

**A self-hosted, compliance-first job-search workspace: collect public postings,
understand your match, and track applications — without auto-submitting them.**

[English](README.md) | [简体中文](README.zh-CN.md)

[![CI](https://github.com/cjs1102/JobPilotAI/actions/workflows/ci.yml/badge.svg)](https://github.com/cjs1102/JobPilotAI/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![Release](https://img.shields.io/github/v/release/cjs1102/JobPilotAI?display_name=tag)](https://github.com/cjs1102/JobPilotAI/releases/tag/v0.1.0)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](CMakePresets.json)
[![C++ tests](https://img.shields.io/badge/tests-321%20C%2B%2B-brightgreen.svg)](docs/quality/README.md)

![Job detail with an explainable rule-based match](docs/images/web-detail.png)

*Job detail with a deterministic match score, matched skills, reasons, and
deal-breakers. The current UI labels are Chinese.*

<details>
<summary>Application status board</summary>

![Application status board with tracked applications](docs/images/web-status.png)

*Favourites and application states stay in one local board; the actual
application opens on the original page.*

</details>

## Why

Job boards are good at listing jobs and bad at telling you why one is worth your
time. JobPilotAI turns public postings into a searchable workspace, explains the
match with a deterministic `rule_v1` model, and keeps the application state in
one place.

It deliberately stops before the submit button. Matching is explainable, resume
tailoring is a draft with a fact-check gate, and the actual application opens in
the original page for a human to review. It is **not** a bot that mass-applies.

## Contents

[Why](#why) · [Features](#features) · [Architecture](#architecture) ·
[Quick Start](#quick-start-english) · [Usage](#usage) ·
[Verification](#verification) · [Documentation](#documentation) ·
[Roadmap](#roadmap) · [Non-goals](#non-goals) ·
[Contributing](#contributing) · [Security](#security) · [License](#license) · [Acknowledgements](#acknowledgements)

## Features

- **Collect public jobs** through Greenhouse, Lever, Ashby, and nowcoder
  adapters, plus manual capture and JSON import.
- **Search and filter** by keyword, salary, skill, location, education,
  job nature, deadline, and sort order.
- **Explain every match** with a score, matched/missing skills, additive
  reasons, deal-breakers, and deadline urgency.
- **Draft a resume** with source annotations and a mechanical fact gate before
  confirmation.
- **Track applications** through a validated state machine and a local status
  board.
- **Run locally** with a native HTML/CSS/JS UI; nginx is optional and a Python
  stdlib proxy is the fallback.
- **Keep AI optional**: the deterministic path works without a provider key;
  the Python AI worker adds task-based assistance.

## Architecture

`backend/` is `jobpilot_core` (network/HTTP, config, logging, MySQL, Redis).
`service/` is the business layer and the runnable entry, split into controllers,
services, and DAOs. Python tools are kept outside the C++ service.

```text
JobPilotAI/
├── backend/          # jobpilot_core: network/HTTP, config, logging, DB, cache
├── service/          # business layer + runnable service + C++ tests
├── source_adapters/  # public-source adapters and manual capture (Python)
├── ai_worker/        # optional AI task worker (Python)
├── web/              # native HTML/CSS/JS UI, no build step
├── database/schema/  # MySQL 8 schema and additive migrations
├── docker/           # nginx static-host config
├── docs/             # contracts, ADRs, quality, guides, stage index
└── tools/            # demo, release, and verification tools
```

Request path:

```text
Browser -> nginx (or web_static_proxy.py) -> Router -> Controller -> Service -> DAO -> MySQL
                                                                  \-> Redis (sessions)
```

The C++ service speaks HTTP/1.1 over an epoll-based Reactor. P1 network/HTTP is
behaviorally frozen; changes there need an explicitly approved correctness plan.

## Quick Start (English)

Prerequisites: Docker Engine with Compose v2, `curl`, and Python 3. The first run
builds the backend image and pulls MySQL 8 and Redis 7.

```bash
git clone https://github.com/cjs1102/JobPilotAI.git
cd JobPilotAI
./tools/dev/demo-up.sh
```

The launcher creates a `.env` with random secrets only when the file is missing
(mode `0600`, values never printed), starts the stack, serves the UI, and imports
12 synthetic jobs. A successful run prints:

```text
DEMO_READY url=http://127.0.0.1:8081
```

Open that URL. The API is on `http://127.0.0.1:8080` by default. Stop the stack
with:

```bash
docker compose --env-file .env -p jobpilot-demo --profile app --profile web down
```

If `nginx:1.27-alpine` is unavailable, `demo-up.sh` automatically uses the
Python static/proxy server. Pass `--no-seed` to skip the sample jobs. Full
troubleshooting is in [`docs/guides/local-demo.md`](docs/guides/local-demo.md).

### Build from source (optional)

The demo uses the containerized backend. To build the C++ service directly,
install a C++17 toolchain, CMake, `pkg-config`, and the MySQL client development
files, then:

```bash
tools/dev/bootstrap.sh          # resolve the pinned Conan graph
cmake --preset release
cmake --build --preset release -j"$(nproc)"
```

The binary is `build/presets/release/service/jobpilot_service`. See
[`docs/quality/README.md`](docs/quality/README.md) for presets and test levels.

## Usage

1. Register a local account; the demo uses HTTP and browser `localStorage`.
2. Upload a PDF/TXT/Markdown resume before favouriting a job.
3. Search `C++`, open the recommended view, and use the filters.
4. Read the score, matched/missing skills, `reasons`, and `deal_breakers`.
5. Favourite a job and move it through the status board.
6. Open the apply link or original announcement and apply manually.

The demo data is synthetic and stays in local Docker volumes. No application is
submitted by JobPilotAI.

## Verification

- **321 C++ tests**; the full Debug suite completed `321/321` with zero
  failures and zero sanitizer diagnostics (one review-backfill skip on the
  developer database; it passes on the isolated scratch database). The last
  full ASan/UBSan matrix was `312/312` before the latest nine tests; the 14
  affected tests pass under both sanitizers.
- **TSan**: 52/52 harness tests and 17/17 external-runtime tests, zero
  diagnostics. TSan does not cover the full suite.
- **Python**: 481 source-adapter tests and 190 AI-worker tests pass in the pinned
  environment.
- **Data model**: 13 MySQL tables; **36 routes** (27 non-internal, 9 internal).
- **Benchmark** (4 vCPU VMware, Release): **13,421.82 req/s** at 2 threads / 32
  connections, p99 **7.17 ms**. This is a dated, host-specific number, not a
  threshold.
- **Latest main CI** (`aed4abb`): `ci` `39818983565` (6m12s) and `image`
  `39818983370` (2m14s) completed successfully.

## Documentation

| Topic | Document |
|---|---|
| Start here | [`docs/README.md`](docs/README.md) |
| Overview, requirements, architecture | [`docs/00-project-overview.md`](docs/00-project-overview.md), [`docs/01-requirements.md`](docs/01-requirements.md), [`docs/02-system-architecture.md`](docs/02-system-architecture.md) |
| Database and API contracts | [`docs/03-database-design.md`](docs/03-database-design.md), [`docs/04-api-design.md`](docs/04-api-design.md) |
| Architecture decisions | [`docs/adr/README.md`](docs/adr/README.md) |
| Coding and verification | [`docs/06-coding-standard.md`](docs/06-coding-standard.md), [`docs/quality/README.md`](docs/quality/README.md) |
| Local demo and troubleshooting | [`docs/guides/local-demo.md`](docs/guides/local-demo.md) |
| Benchmarks | [`docs/benchmarks/README.md`](docs/benchmarks/README.md) |
| Stage history | [`docs/exec-plans/README.md`](docs/exec-plans/README.md) |
| Document inventory | [`docs/PROJECT_DOCUMENTATION_INDEX.md`](docs/PROJECT_DOCUMENTATION_INDEX.md) |

## Roadmap

These are directions, not commitments; all are **not started**.

- Verify and register additional China sources (Moka is currently unverified).
- Add provider-backed resume wording behind the existing fact gate.
- Add local notifications for deadlines and status changes.
- Harden the demo UI for production authentication and TLS.

## Non-goals

- Automatic application, form filling, or auto-submit.
- Login-wall, CAPTCHA, cookie/UA, or proxy-pool bypass.
- A general-purpose crawler, message queue, API gateway, or multi-tenant SaaS.
- A promise of complete source coverage; some sources are intentionally bounded.

## Contributing

Read [`CONTRIBUTING.md`](CONTRIBUTING.md) before opening a pull request. Work on
a branch, include the relevant tests or a documented verification gap, and keep
the P1 freeze in mind. The stage history is indexed in
[`docs/exec-plans/README.md`](docs/exec-plans/README.md).

## Security

Report vulnerabilities privately as described in [`SECURITY.md`](SECURITY.md).
Do not open a public issue for a security report. The local demo is not a
production deployment: it uses HTTP and browser storage, and should not hold
real credentials or personal resumes.

## License

MIT. See [`LICENSE`](LICENSE) and [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md).

## Acknowledgements

Built with libbcrypt, nlohmann/json, hiredis, spdlog, yaml-cpp, GoogleTest, and
Conan. Third-party licences and provenance are recorded in
[`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md).
