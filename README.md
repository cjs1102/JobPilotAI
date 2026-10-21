# JobPilotAI

[English](README.md) | [简体中文](README.zh-CN.md)

JobPilotAI is a self-hosted job-search workspace: Python source adapters collect
public job postings, a C++17 backend searches and ranks them, explains matches
deterministically, and tracks applications. An optional AI worker provides
task-based assistance; the core workflow does not require an AI provider or API
key.

| Job detail with an explainable match | Application status board |
|---|---|
| ![Job detail showing a rule-based match score and reasons](docs/images/web-detail.png) | ![Application status board with a resume upload and tracked applications](docs/images/web-status.png) |

The screenshots show the local Web UI with synthetic demo data. The current UI
labels are in Chinese.

## Quick Start (English)

Prerequisites: Docker Engine with Compose v2, `curl`, and Python 3. The first
run builds the backend image, so Docker must be able to pull its base images and
complete that build.

```bash
git clone https://github.com/cjs1102/JobPilotAI.git
cd JobPilotAI
./tools/dev/demo-up.sh
```

The launcher:

- checks Docker, Compose, `curl`, and Python;
- creates a `.env` with random MySQL and internal-token secrets only when
  `.env` is missing, writes it with mode `0600`, and never prints the secret
  values; an existing `.env` is left untouched;
- starts MySQL 8 and Redis 7, builds and starts the backend, and serves the Web
  UI on port `8081` — through nginx when `nginx:1.27-alpine` is available, or
  through the bundled Python static/proxy server otherwise;
- imports 12 synthetic jobs by default and prints a line such as
  `DEMO_READY url=http://127.0.0.1:8081`.

Open the printed URL. The default backend is on `http://127.0.0.1:8080`. To
start without the sample jobs, pass `--no-seed`. Ports and the Compose project
name can be overridden with `DEMO_API_PORT`, `DEMO_WEB_PORT`,
`DEMO_MYSQL_PORT`, `DEMO_REDIS_PORT`, and `DEMO_PROJECT`.

Stop the stack with:

```bash
docker compose --env-file .env -p jobpilot-demo --profile app --profile web down
```

Add `-v` only if you also want to delete the demo database and uploaded files.

### Build from source (optional)

The one-command demo uses the containerized backend. To build and run the C++
service directly, install a C++17 toolchain, CMake, `pkg-config`, and the MySQL
client development files, then:

```bash
tools/dev/bootstrap.sh          # resolves the pinned Conan graph
cmake --preset release
cmake --build --preset release -j"$(nproc)"
```

See [`docs/quality/README.md`](docs/quality/README.md) for presets, test levels,
and sanitizer runs. The binary is
`build/presets/release/service/jobpilot_service`.

### Manual setup and the nginx fallback

If you prefer to manage the services yourself:

```bash
cp .env.example .env          # set MYSQL_ROOT_PASSWORD and MYSQL_PASSWORD
docker compose up -d          # MySQL + Redis
docker compose --profile app build backend
docker compose --profile app --profile web up -d
```

If `nginx:1.27-alpine` cannot be pulled, the same `web/` files can be served by
the Python standard-library runner:

```bash
python3 tools/dev/web_static_proxy.py \
  --root web --port 8081 --backend http://127.0.0.1:8080
```

`tools/dev/demo-up.sh` performs this fallback automatically. See the
[local demo guide](docs/guides/local-demo.md) for troubleshooting.

## Try the demo

1. Register a local account.
2. Open the application status board, go to resume setup, and upload a PDF, TXT,
   or Markdown resume. A resume version is required before a job can be
   favourited.
3. Open the jobs view and search for `C++`; try the location, salary, education,
   job-nature, and sort filters.
4. Open a result and read the match score, matched and missing skills,
   `reasons`, and `deal_breakers`. Matching is deterministic and needs no AI
   key.
5. Favourite the job. On the status board, move it through the application state
   machine (favourite, to apply, applied, written test, interviews, offer) or
   mark it rejected.
6. Use the apply-link button when an apply URL exists, otherwise use the
   original-announcement button. These links open the original page in a new
   tab; JobPilotAI does not fill in or submit an application.

The demo user, resume, and jobs are local fixtures. Do not use real credentials
or personal resumes in a shared environment.

## How to use

### Collect or import jobs

JobPilotAI reads public, login-free sources through Python source adapters:
Greenhouse, Lever, Ashby, and nowcoder for China campus listings. Adapters
publish observations through the token-protected internal API and never write
to the database directly. A manual-capture bookmarklet is available for pages
behind a login wall; it reads the page the user already has open and does not
send credentials or make network requests. A manual import endpoint also
accepts a JSON batch.

The source adapters are documented in
[`source_adapters/README.md`](source_adapters/README.md), including the source
register, robots handling, and per-source limits. Treat a `complete=false`
observation as a bounded sample, not a full crawl.

### Search, filter, and rank

The public job list supports pagination, keyword search, salary and skill
filters, location and education filters, deadline filters, and sorting. Signed-in
users can request the recommended view. Only jobs that passed the intake quality
gate are visible publicly; rows that need review stay behind the internal
review surface.

### Read the match explanation

`rule_v1` is a deterministic ranking and explanation model. It returns a score,
matched and missing skills, additive `reasons`, `deal_breakers`, and deadline
urgency. There is no hidden LLM step in this path. The AI task pipeline is
optional and cannot overwrite the deterministic score.

### Prepare a resume draft

The resume tailor produces a deterministic plain-text ATS draft with source
annotations for each factual claim; unknown facts remain explicit placeholders.
A mechanical fact gate must pass before a draft can be confirmed. This is a
draft workflow, not an automatic application generator.

### Track applications and apply manually

Favourites and application status changes are recorded in the local database.
The backend enforces the status state machine. When it is time to apply, open
the original posting or the explicit apply link and complete the application in
the browser. There is no auto-submit, form-filling, or notification path.

All demo data stays in local Docker volumes. Network access happens only when
you run a source adapter or configure an optional AI provider.

## What's included

- **Source ingestion** — public ATS adapters, a nowcoder campus source, manual
  capture, JSON import, source observations, freshness, and an intake quality
  gate.
- **Search and ranking** — keyword and structured filters, stable pagination, a
  recommended view, and deterministic `rule_v1` scoring.
- **Explainable matching** — scores, matched and missing skills, reasons,
  deal-breakers, and deadline urgency on the job list and detail pages.
- **Resume tailoring** — deterministic ATS drafts with source annotations and a
  fact-check gate.
- **Application tracking** — favourites, a validated status state machine,
  statistics, and a local status board.
- **Local Web UI** — native HTML/CSS/JavaScript with no build step, same-origin
  `/api` and `/health` proxying, and a stdlib fallback when nginx is unavailable.
- **Optional AI worker** — a Python worker for task-based assistance; the core
  product degrades gracefully without a provider key.
- **Operational tooling** — Docker Compose, a committed container image
  definition, CI, sanitizer presets, benchmark harnesses, a release preflight,
  and a check that no submission path exists.

## Verification

The release-readiness baseline recorded on 2026-10-21:

- **312 C++ tests** through CTest. The Debug, ASan, and UBSan runs each
  completed `312/312` with zero failures and zero sanitizer diagnostics. One
  review-backfill test is skipped on the developer database and passes on the
  isolated scratch database.
- **TSan**: 52/52 harness tests and 17/17 external-runtime tests, with zero
  diagnostics. TSan does not cover the entire suite.
- **Python**: 481 source-adapter tests and 190 AI-worker tests pass in their
  pinned environment.
- **CI**: the `ci` and `image` workflows were green on `main` after the
  release-readiness merge. The image workflow builds the committed Dockerfile
  and runs an end-to-end demo gate.
- **Benchmark** on a 4-vCPU host: 13.4–16.1k requests/second on `/health` and
  180–194 requests/second on the database-backed jobs endpoint. These are
  host-specific measurements, not a service-level objective or a pass/fail
  threshold. Environment and raw results are in
  [`docs/benchmarks/README.md`](docs/benchmarks/README.md).

## Limits and non-goals

- **China source coverage is narrow.** nowcoder is the only registered China
  campus source. It reads a bounded login-free list: at most 20 rows per query,
  no pagination, and always `complete=false`. Other China sources are not
  registered.
- **Some postings have no apply URL.** Greenhouse and nowcoder aggregated rows
  do not expose a source-provided `apply_url`; the UI falls back to the original
  announcement. It never derives or guesses an apply URL. Moka has not been
  verified and is not registered.
- **Login walls are not bypassed.** There is no automatic login, CAPTCHA
  bypass, cookie or user-agent spoofing, or proxy pool. Login-walled jobs use
  manual capture only.
- **No automatic application.** There is no auto-apply, auto-submit, form
  filling, or notification path. There is also no general-purpose crawler,
  message queue, API gateway, or SaaS RBAC.
- **Deadlines are display-only.** An expired deadline does not automatically
  close a job.
- **The UI is a local demo.** Authentication uses HTTP and browser
  `localStorage`, not production authentication or TLS.
- **No legal conclusion.** Source terms, robots directives, and data-use rules
  remain the operator's responsibility.

## Documentation

- [Documentation map](docs/README.md) and
  [project documentation index](docs/PROJECT_DOCUMENTATION_INDEX.md)
- [Local Web UI guide](docs/guides/local-demo.md)
- [Architecture](docs/architecture/open-source-mvp.md) and
  [architecture decisions](docs/adr/README.md)
- [API contract](docs/04-api-design.md) and
  [database contract](docs/03-database-design.md)
- [Source adapters](source_adapters/README.md) and
  [benchmarks](docs/benchmarks/README.md)

## Contributing and security

Read [`CONTRIBUTING.md`](CONTRIBUTING.md) before opening a pull request. Report
security issues privately as described in [`SECURITY.md`](SECURITY.md); do not
open a public issue for a vulnerability. A code of conduct is deferred until a
reporting channel is available.

## License

MIT. See [`LICENSE`](LICENSE) and [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md).
