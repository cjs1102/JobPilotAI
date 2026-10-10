# JobPilotAI Source Adapters

Python adapters that fetch public job sources (Greenhouse, Lever, Ashby, and the
0014 China framework), normalize them into the platform job contract, and publish
**one observation per board** through the C++ internal API. Adapters never write
the database directly
([ADR-0008](../docs/adr/0008-source-adapters-and-job-discovery.md),
ExecPlan 0012; China sources per
[ADR-0011](../docs/adr/0011-china-first-sourcing-and-compliance.md),
ExecPlan 0014).

## Layout

```text
source_adapters/
├── pyproject.toml              # only runtime dependency: PyYAML
├── config.example.yaml
└── jobpilot_sources/
    ├── cli.py                  # python -m jobpilot_sources sync ...
    ├── config.py               # YAML loading + validation
    ├── client.py               # stdlib HTTP transport (injectable, 0012 path)
    ├── normalizer.py           # deterministic field normalization + build_job
    ├── publisher.py            # batching + POST /internal/jobs/source-sync
    ├── fetch.py      (0014)    # robots-first fetching: retries, SSRF guard,
    │                           #   budgets, page ceiling, rich responses
    ├── robots.py     (0014)    # robots.txt parsing + per-path verdict (fail closed)
    ├── decoding.py   (0014)    # charset precedence: BOM > header > meta > utf-8
    ├── htmljob.py    (0014)    # JSON-LD / embedded JSON / links / meta
    ├── feeds.py      (0014)    # RSS / Atom / sitemap (DOCTYPE/ENTITY refused)
    ├── timestamps.py (0014)    # Beijing-time (F13) date/time normalization
    ├── salary.py     (0014)    # 月薪千元 conversion, ambiguous forms omitted
    ├── normalize_cn.py (0014)  # education / nature / experience / city / deadline
    ├── registry.py   (0015A)   # single-source platform-name registry + C++ parity
    ├── data/         (0015A)   # platform_names.yaml — the ONE platform list
    ├── capture/      (0014)    # bookmarklet.js + artifact parser + local publisher
    │                           #   (+ quality.py: 0015A pre-flight mirror)
    ├── providers/              # greenhouse.py, lever.py, ashby.py, base.py,
    │                           #   public_page.py (0014 generic public page)
    └── tests/                  # unittest suite + recorded fixtures
```

`capture/` is the **manual** channel: a credential-free bookmarklet that copies
what the user's own logged-in browser already shows, plus a local publisher that
reuses the existing `/internal/jobs/import` contract.

Requires Python 3.10+ and PyYAML. No web framework and no database driver is
used. The suite is fixture-driven and never touches the network.

## Usage

```bash
cd source_adapters

# fetch everything, report only (no writes, still validates the token)
python3 -m jobpilot_sources sync --config config.yaml --dry-run

# publish every enabled source
python3 -m jobpilot_sources sync --config config.yaml

# restrict the run to one provider type
python3 -m jobpilot_sources sync --config config.yaml --provider greenhouse
```

The internal token is read from the environment variable named by
`jobpilot.internal_token_env`. A missing token fails **before any network
call**, and the token is never logged or echoed.

## Configuration

Copy `config.example.yaml` to `config.yaml`. Duplicate `(type, board)` entries
are rejected, and no board auto-discovery happens (ExecPlan 0012 section 5).

```yaml
jobpilot:
  base_url: http://127.0.0.1:8080
  internal_token_env: INTERNAL_API_TOKEN

sources:
  - type: greenhouse
    board: example-company
    company: Example Company
    enabled: true
```

`company` is optional: Greenhouse boards use the provider `company_name` when
present, otherwise the configured `company`, otherwise the board token (Lever
and Ashby do not return a company name). `public_page` **requires** a configured
`company`: the platform's `jobs.company` column is mandatory and a page/feed
without a company name would have to be invented, so such rows are skipped.

## Provider endpoints

| Provider | Endpoint | Fetch mode | Completeness |
|---|---|---|---|
| Greenhouse | `boards-api.greenhouse.io/v1/boards/{board}/jobs?content=true` | JSON | Complete only when `meta.total` equals the returned row count |
| Lever | `api.lever.co/v0/postings/{board}?mode=json` | JSON | Complete by contract (the array is the whole board) |
| Ashby | `api.ashbyhq.com/posting-api/job-board/{board}?includeCompensation=true` | JSON | Complete by contract; `isListed: false` postings are dropped |
| `public_page` (0014) | the configured absolute list URL (HTML / JSON / JSON-LD / RSS / sitemap) | text | Complete only when a JSON payload states a total equal to the returned row count; HTML/JSON-LD/feed pages can never prove completeness |

## Normalization rules

- All timestamps are converted to **UTC** and written as `YYYY-MM-DD HH:MM:SS`.
  A timestamp without an offset is treated as UTC for the 0012 providers; for
  China sources a naive value is Beijing time (see the F13 section below).
- Nothing is fabricated: a field is omitted when the provider does not supply a
  usable value, and a row without `title` or a job URL is skipped.
- HTML descriptions are converted to plain text. The **final** description is
  capped at 48 KiB (49,152 UTF-8 **bytes**, `len(value.encode("utf-8"))`): an
  over-cap value is a hard `description_too_large` rejection (nothing is
  published, `limit_bytes`/`actual_bytes` reported) - never truncated and never
  published with the description silently dropped. This cap is **not** the
  raw-page budget: that one is the independent 2 MiB response-body safety limit
  in `fetch.py` (see Access discipline).
- `requirements` stays empty: no structured skills are inferred from text. The
  0014 China framework also drops *structured* skill lists, because the source
  gives names but no level and `job_requirements.level` means something specific
  (1 = 了解), so writing one would fabricate a requirement.
- **Salary** is published only as monthly thousands (`15-25K` → 15/25,
  `1.5-2.5万/月` → 15/25, `8000-12000元/月` → 8/12, `20-35K·14薪` → 20/35).
  Ambiguous or non-monthly forms are omitted, never converted: `面议`, `300/天`,
  `30-50万/年`, a bare `8000`, an inexact `8.5K`, or any non-CNY currency.
- 0014 fields: `education_required` (`1不限/2大专/3本科/4硕士/5博士`),
  `job_nature` (`1校招/2社招/3实习`), `experience_years_min/max` (years,
  `min <= max`), `deadline` + `deadline_date_only`. Values outside those domains
  are omitted rather than clamped.

### F13 deadline time semantics (China sources)

The authoritative convention is
[`docs/03-database-design.md` §9.1](../docs/03-database-design.md); Python and
C++ implement the identical rule. In short:

- Recruitment deadlines are **Asia/Shanghai (UTC+8)**, modelled as a fixed
  offset with that timezone name. No conversion ever uses the host's timezone
  (the tests re-run everything under `TZ=Pacific/Kiritimati` and with
  `time.localtime`/`time.mktime` stubbed out to fail).
- A **date-only** value (`2026-10-31`, `2026/10/31`, `2026年10月31日`) is stored
  as that Beijing day's local **noon** converted to UTC, i.e.
  `2026-10-31 04:00:00`, with `deadline_date_only = true`. Noon is a storage
  convention that keeps the calendar date stable; it is **not** an expiry
  instant and is never rendered as one.
- An **explicit time** (`2026-10-31 18:30:00`, `2026-10-31T18:30:00+08:00`)
  keeps the real instant (`2026-10-31 10:30:00` UTC) with
  `deadline_date_only = false` and is **never** rewritten to noon.
- Ambiguous input (`10月31日` without a year, `长期有效`, `招满为止`) is omitted.
- `deadline` never influences visibility or closure in this stage; a job past its
  deadline stays visible until a later stage adds that policy
  (`docs/04` §5.1).

## Manual capture (0014 Step 6)

For placements that are **not** publicly readable (login-walled platforms,
内推 pages, anything behind an account), the合规 channel is manual capture. The
user reads the page in their own browser and publishes it from their own
machine. Nothing logs in, solves a CAPTCHA, spoofs a cookie/UA, or automates a
gated site.

```bash
cd source_adapters

# 1. print the pasteable bookmarklet (or a small help page)
python3 -m jobpilot_sources capture-bookmarklet          # javascript: URL
python3 -m jobpilot_sources capture-bookmarklet --html   # help page with the link

# 2. on the job page, click the bookmark; the JSON capture lands in the clipboard.
#    Save it locally, review it (delete anything you do not want to publish).

# 3. validate and publish through the EXISTING manual import endpoint
INTERNAL_API_TOKEN=... python3 -m jobpilot_sources publish-capture \
  --file capture.json --base-url http://127.0.0.1:8080
```

### Data flow

```text
browser page (user's own logged-in session)
  bookmarklet.js: read DOM only (title, meta, JSON-LD JobPosting, bounded visible text)
      -> JSON capture  -> clipboard / local file        (no network, no credential)

local publisher: publish-capture
  artifact.py     : parse + validate the untrusted artifact (shape, sizes, enums)
  quality.py      : 0015A pre-flight mirror (same stable codes as the C++ engine,
                    byte-based 48 KiB cap, platform-name registry)
  publisher.py    : normalize with the SAME modules the adapters use
                    (fetch.normalize_url, normalize_cn, salary, timestamps/F13)
                    -> /internal/jobs/import payload, source="manual", 1..100 jobs
  client transport: POST with header X-Internal-Token (token read from the local
                    environment by NAME; never logged, never in the artifact)
      -> C++ InternalJobImportController (constant-time token check)
      -> JobIngestionService::Import (Validate: 1..100, source in manual/import/crawler)
      -> JobIngestionDao::UpsertJob (INSERT ... ON DUPLICATE KEY UPDATE on uk_source_url;
                                    never writes last_seen_at/source_status/missing_count)
      -> MySQL jobs / job_requirements
  read-back: GET /api/v1/jobs?keyword=... and GET /api/v1/jobs/{id}
```

### Authorization boundary (why the browser half is credential-free)

- The bookmarklet runs in **untrusted page context**. It must never hold a
  credential, so it makes **no network request at all**: the only output is text
  the user already sees. Tests enforce the absence of network calls, endpoint
  strings, credential markers and page-state reads in `bookmarklet.js`.
- The internal token lives only in the **local publisher's environment**, is
  read by variable name (`--token-env`, default `INTERNAL_API_TOKEN`, or the
  value from `--config`), and is sent only as the `X-Internal-Token` header.
  There is **no** `--token` flag, no token in the artifact, no token in
  `config.example.yaml` (only the variable name), and no token in any output —
  the CLI tests assert all of that on success and failure paths.
- The capture artifact is **untrusted input**: a fully attacker-controlled file
  can at most create/update a `jobs` row through the existing endpoint, which
  still requires the token that never leaves the machine. Manual capture adds no
  endpoint and no new trust boundary.
- `source="manual"` keeps the manual channel out of the freshness lifecycle: the
  import path has no `complete`/`source_scope`/`observed_at` semantics, and the
  publisher cannot express `last_seen_at`, `source_status`, `missing_count`,
  `closed_at` or `source_scope` at all.

### Manual vs automatic: F14 (manual wins)

A source-sync observation whose `source_url` collides with a `source="manual"`
row is skipped by the server: no column is updated, `source` stays `manual`, the
freshness/lifecycle fields are untouched, the manual `job_requirements` are not
replaced, and the response reports `skipped_manual` +
`skipped_manual_urls`. Both channels therefore derive `source_url` with the same
conservative normalizer (`fetch.normalize_url`), which is what makes a collision
detectable at all — asserted in `tests/test_capture_f14.py`. The authoritative
enforcement is the C++ observation upsert, proven by
`SourceSyncDaoTest.ManualRecordIsUnchangedByACollidingObservation` and
`SourceSyncHttpTest.ManualRecordWinsOverObservation`.

### Failure handling (never a false success)

- Exit code `0` only after a `200` whose `created + updated == total ==` the
  number of jobs submitted; `1` for a failed publish; `2` for an input/config
  problem detected **before** any request.
- Locally rejected with a named reason: missing/blank token, unreadable file,
  invalid JSON, missing `title`/`company`/`source_url`, non-http(s) URL,
  over-long field (never truncated), unsupported `capture_version`, duplicate
  `source_url`, more than 100 captures, missing `base_url`.
- Server-side failures are reported with the HTTP status and the server's
  `{code,message}`; unparsable or inconsistent response bodies are failures.
- `--dry-run` validates and prints the payload without calling the API.

## Pre-ingest quality pre-flight (0015A Step 7)

`publish-capture` mirrors the deterministic C++ quality engine
(`service/src/job/job_quality.cpp`) **before** it publishes, so an operator sees
why an item would be rejected or flagged instead of discovering it in an ingest
response. The mirror is UX only and **never authoritative**: the C++ gate
decides, and the same stable codes come back from the server.

The pre-flight runs on the normalized import payload (what the server actually
receives) and reports, per item, the same vocabulary as the engine:

| Code | Outcome | Trigger |
|---|---|---|
| `company_missing` | reject | employer empty after trim |
| `description_too_large` | reject | description > 49,152 UTF-8 **bytes** |
| `source_url_unusable` | reject | not an absolute `http(s)` URL |
| `employer_is_platform` | needs_review | normalized employer is in the registry |
| `source_url_not_detail` | needs_review | site root / `/jobs` `/job` `/search` `/list` / `/positions` / query-only |
| `title_equals_company` | needs_review | normalized title equals normalized employer |
| `duplicate_content_in_batch` | needs_review | equal employer+title+location under a different URL (all items) |
| `employer_implausible` | advisory (stays visible) | employer < 2 bytes or all ASCII digits |
| `description_polluted` | needs_review | ≥ 3 distinct nav markers **and** marker bytes ≥ 20 % |
| `description_marker_minor` | advisory (stays visible) | 1–2 markers, or share < 20 % |
| `source_url_shape_unrecognised` | advisory (stays visible) | no detail marker and no ≥ 4-digit segment |

An unknown platform name or URL shape is **clean** (fail-open). A `reject` stops
the whole batch before any request (exit `2`); `needs_review` items are still
published — the server stores them hidden and queues them, which is the designed
behaviour. Parsing failures (missing `title`/`company`, an over-long
`company`/`title`/`location`/`source_url`) carry the same code vocabulary in a
`findings` array.

### Platform-name registry (single source of truth)

```text
source_adapters/jobpilot_sources/data/platform_names.yaml
```

One file, read by **both** the C++ `PlatformNameRegistry` and
`jobpilot_sources.registry`. There is no second list in either language
(`tests/test_registry.py` asserts that for Python and C++). The path is resolved
the same way on both sides:

1. `JOB_QUALITY_PLATFORM_NAMES_FILE` when set and non-empty;
2. otherwise the repo-relative default
   `source_adapters/jobpilot_sources/data/platform_names.yaml` (the service is
   documented to run from the repository root);
3. Python only: the packaged copy of the *same* file, so an installed wheel can
   still resolve it (`pyproject.toml` ships `data/*.yaml` as package data).

A missing/unreadable/invalid file yields an **empty** registry plus a warning in
the `registry` block of the JSON output — fail-open, exactly like C++.

Normalization mirrors `PlatformNameRegistry::Normalize` byte for byte: trim the
six ASCII whitespace characters (`std::isspace` in the C locale — a U+3000
ideographic space is *not* trimmed), fold `A`-`Z` only, then strip the suffixes
`招聘网` / `人才网` / `官网` / `招聘` / `网` repeatedly (never to an empty
string). Membership normalizes its argument, so `牛客网` matches the registered
`牛客`. Parity is demonstrated by
`tools/verify_platform_name_parity.cpp` (opt-in; see Tests) and pinned by
`tests/test_registry.py::NormalizationParityTest`, which asserts the canonical
normalized set and the exact normalisation table.

### Description cap: 48 KiB of UTF-8 bytes

- The application-layer hard cap is **49,152 bytes** (`48 * 1024`) of UTF-8,
  checked as `len(value.encode("utf-8")) > 49152` — **bytes, never characters**.
  A 16,385-character CJK description is 49,155 bytes and is rejected.
- Over-cap is always a **rejection** with `description_too_large` plus
  `limit_bytes` and `actual_bytes`; a description is never truncated, clamped or
  silently dropped. `ManualPublisher.publish` independently refuses to send an
  over-cap description (last line of defence before the network).
- The layering is deliberate: `artifact.py` reads the description **verbatim**
  (structure and the column caps for `company`/`title`/`location`/`source_url`),
  `capture/quality.py` owns the 0015A cap and the stable finding, and the CLI
  refuses to publish a rejected batch.
- The bookmarklet's extraction default is **unchanged at 4,000 characters**
  (`DEFAULT_MAX_DESCRIPTION` in `capture/bookmarklet.js`); browser collection is
  not widened. Because it counts characters while the platform counts bytes, a
  capture near the cap can still be refused at ingest — the finding carries both
  byte counts so it is diagnosable.

### `--json` output (additive)

Existing keys keep their meaning; the pre-flight adds a `registry` block, a
`counters` block and per-item `items`:

```json
{
  "ok": true,
  "submitted": 1,
  "created": 1, "updated": 0, "total": 1,
  "registry": {"path": ".../data/platform_names.yaml", "size": 18, "warning": null},
  "counters": {"clean": 0, "needs_review": 1, "rejected": 0, "advisory": 0},
  "items": [
    {
      "index": 0,
      "source_url": "https://x.invalid/campus/1001",
      "outcome": "needs_review",
      "review_status": 1,
      "findings": [
        {"code": "employer_is_platform",
         "reason": "employer equals a registered platform name (\"牛客网\"); the platform is not the employer",
         "outcome": "needs_review", "review_status": 1,
         "field": "company", "limit_bytes": null, "actual_bytes": null}
      ]
    }
  ]
}
```

`counters.advisory` counts items that carry at least one advisory finding, so it
**overlaps** `clean`/`needs_review`/`rejected` (which are disjoint). A rejected
pre-flight batch exits `2` with `"stage": "quality"`; a payload-level parse
rejection keeps `"stage": "capture"` and adds a `findings` array. The publisher
contract is untouched: `source="manual"`, `POST /internal/jobs/import`, token
only in the `X-Internal-Token` header, and still no `--token` value option.

## Access discipline (0014)

Every China-source fetch goes through `fetch.Fetcher`, which enforces:

- **robots.txt first, per path, fail closed.** The requested path *and the final
  URL after redirects* get their own verdict (longest match wins; an
  equal-length/equivalent match follows RFC 9309 section 2.2.2 and lets `Allow`
  win; matching agent groups are not merged). A `Disallow` aborts that source for
  the run: no retry, no fallback to another path, one explicit log line. A
  robots.txt that is unreachable, 5xx or undecodable denies the host;
  `404`/`410` mean "no rules" and allow. `Crawl-delay` is the inter-request
  floor (never below 200 ms).
- **Bounded retries**: only `429`, `5xx` and transport errors, with exponential
  backoff plus jitter, default `{retries: 2, baseDelayMs: 500, maxDelayMs: 8000}`.
  `Retry-After` is honoured but clamped to `maxDelayMs`. A non-`429` 4xx and a
  redirect refused by the SSRF guard are never retried.
- **Per-request timeout** (default 10 s) and an **honest User-Agent** naming the
  tool and project URL; no browser impersonation.
- **SSRF guard**: every outbound URL — including each redirect target and the
  final URL — must resolve to public addresses only (IPv4 and IPv6 loopback,
  private, link-local, CGNAT, metadata, multicast and reserved ranges are
  refused). The host is re-resolved immediately before each request, which is
  the re-check that narrows DNS-rebinding windows.
- **Budgets**: a response-size cap, a total request budget and a **hard page
  ceiling**. Hitting any limit marks the run truncated and forces
  `complete=false`; a truncated run can never claim completeness. The
  response-size cap (`fetch.DEFAULT_MAX_RESPONSE_BYTES`, 2 MiB) bounds the
  **raw page**, not a stored field: it is deliberately unrelated to the 48 KiB
  description cap, and neither number may be "aligned" to the other.
- **Charset precedence** BOM > HTTP header > `<meta>` > `utf-8`, with
  `gb2312`/`gbk` normalized to `gb18030`; decoding never raises.
- **XML safety**: RSS/Atom/sitemap documents that declare `DOCTYPE`/`ENTITY` are
  refused outright (no `defusedxml` dependency is allowed).

## Source register (来源登记表, 2026-10-09)

Method: for each candidate, `robots.txt` was fetched first and parsed with
`jobpilot_sources.robots`, the per-path verdict was computed for the intended
list path, and only a per-path-allowed URL was read — at most three requests per
host, once, with the honest User-Agent above. No login, no CAPTCHA solving, no
cookie/UA spoofing, no redirect circumvention. Verdicts:

| # | Source | Class | robots.txt result | Login-free evidence | Verdict |
|---|---|---|---|---|---|
| 1 | **国聘** `www.iguopin.com` | S2 公开校招门户 | `200`, body `User-agent: *` + empty `Disallow:` → **all paths allowed**, no `Crawl-delay` | `/`, `/jobs`, `/job` all `200 text/html`, no 登录/CAPTCHA markers | **OUT for automation (Step 5 re-verified).** The job list exists (`POST /api/jobs/v3/list`, detail `GET /api/jobs/v3/info`, found in the public bundle) but in production it requires the site's client-side request signature (`Sign`/`T`/`Nonce` computed with a bundled secret) → risk control. Unsigned access: POST → nginx `405`; GET, `/sitemap.xml`, `/sitemap_index.xml` → the 1.3 KB SPA shell. Not bypassed; manual capture remains the channel. |
| 2 | **Moka** `app.mokahr.com` | S1 国内 ATS 公开页 | `200`: two specific paths disallowed (`/social-recruitment/lingjuninvest/46355`, `/social-recruitment/shopee/74378`); the campus/social tenant roots are **allowed**, no `Crawl-delay` | tenant roots `302`; the shell carries no 登录/CAPTCHA markers | **Candidate, shape unverified.** A plain GET of a tenant root self-redirects (`/campus-recruitment/lingjuninvest` → `/…/36723` → `302` to itself, 10-hop loop) → the public list URL/API for a tenant is still unverified. Not worked around. |
| 3 | **清华就业中心** `career.tsinghua.edu.cn` | S3 高校就业网 | `404` → no rules published → **allowed** | landing page `200 text/html` (38 KB, real content), no 登录/CAPTCHA markers | **Not usable (Step 5 re-verified).** This host publishes announcements only (`/tzgg.htm`, `/xwrd.htm`, `/gjzz.htm`, `/gatxs.htm`, `/jyrl.htm`); its 「招聘信息」 nav points at **another host**, `career.cic.tsinghua.edu.cn/xsglxt/f/jyxt/anony/xxfb`, whose `robots.txt` returns **HTTP 503** (persistent: 4 attempts, both schemes, ~12 min) → RFC 9309 "unavailable" → fail-closed `Disallowed` → the list was **not read**. |
| 4 | **南大就业门户** `job.nju.edu.cn` | S3 高校就业网 | `/robots.txt` returns the SPA HTML with `200` → **no parseable rules → allowed** | landing page `200` but a JS SPA shell ("南京大学就业门户管理后台") | **Unverified.** JS-rendered (and titled a management portal); a plain GET yields no job list. Needs the school's public API. |
| 5 | **牛客** `www.nowcoder.com` | S4 公开聚合 | `200`: `Disallow: /search`, `/nccommon`, `/ab/ab-test-flow`; `/` allowed | `/` is `200` but a logged-out shell with 登录 markers; the job-list path was not read | **Unverified / not automated.** `/search` is robots-disallowed anyway and no login-free job list was proven. |
| 6 | **北森** `www.zhiye.com` | S1 国内 ATS 公开页 | **unavailable** — TLS certificate verify failed (self-signed) → fail closed | — | **OUT this stage.** The certificate is not bypassed; re-check when a valid tenant endpoint is known. |
| 7 | **北大就业网** `job.pku.edu.cn` | S3 | DNS failure → fail closed | — | **Unverified / unreachable** from this host. |
| 8 | **浙大就业网** `career.zju.edu.cn` | S3 | DNS failure → fail closed | — | **Unverified / unreachable** from this host. |
| 9 | Boss直聘 / 智联招聘 / 51job / 猎聘 / 脉脉 / 拉勾 / 实习僧 | O1 登录墙 | not probed | login-walled by design | **OUT** per ADR-0011 §2.3. Manual capture (`/internal/jobs/import`, `source="manual"`) is the only channel; never automated. |
| 10 | 微信公众号 / APP 内推 | O2 | not probed | no public Web API | **OUT** per ADR-0011. |
| 11 | RSS/Atom feeds (S5) | S5 | framework + fixtures only | no live source verified this stage | **Framework ready, no source registered.** |

Raw evidence for each row is reproducible with the opt-in probe below. Notable
raw lines: `iguopin robots: "User-agent: * Disallow:"`; `moka robots:
"Disallow: /social-recruitment/lingjuninvest/46355"`; `tsinghua robots: HTTP
404`; `beisen robots: [SSL: CERTIFICATE_VERIFY_FAILED] self-signed certificate`.

### Step 5 re-verification (2026-10-09) — no compliant list contract found

The 0014 Step 5 attempt probed the two preferred candidates deeper, still
robots-first, read-only, low volume and with the honest UA (12 requests to
iguopin, 16 to tsinghua, all well under budget; no login, CAPTCHA, cookie/UA
spoofing or TLS bypass). Machine-readable record:
`jobpilot_sources/tests/fixtures/cn_source_probe_2026-10-09.json`.

**国聘 (preferred).** The landing page is an SPA; its public bundle
(`/static/js/main.a5c1e78d.js`, 3,331,541 bytes, robots-allowed) declares the real
list contract:

```text
list:   POST /api/jobs/v3/list     (s=…{url:"/api/jobs/v3/list",method:"post",data:e})
detail: GET  /api/jobs/v3/info     (c=…{url:"/api/jobs/v3/info",params:e})
```

The same bundle shows that in production (`REACT_APP_ENV=production`) every
request whose path starts with `/api/jobs/v3` is signed:

```text
o = [Date.now()/1000, randomNonce, METHOD.toUpperCase(), url, "pc"].join("|")
headers: { Sign: g(p.MB, n, r, o), T: r, Nonce: i }      // g(...) uses a bundled secret
```

Unsigned access confirms the gate: `POST /api/jobs/v3/list` (3 payload variants)
→ **405 Not Allowed** (nginx); `GET /api/jobs/v3/list`, `/api/jobs/v3/info`,
`/sitemap.xml`, `/sitemap_index.xml` → the 1.3 KB SPA shell, not data. Reproducing
that signature would mean reimplementing the site's anti-automation mechanism from
a bundled secret, so the source is **OUT** for automated collection (manual
capture remains the合规 channel). Verdict recorded; not worked around.

**清华 (fallback).** `career.tsinghua.edu.cn/robots.txt` is `404` → no rules →
allowed, and the landing page is public server-rendered HTML — but it contains
announcements, not job postings, and its 「招聘信息」 link targets another host:

```text
job list host : career.cic.tsinghua.edu.cn/xsglxt/f/jyxt/anony/xxfb   (anchor text 招聘信息)
robots.txt    : HTTP 503 on https:// and http:// robots.txt — 4 attempts over ~12 minutes
policy        : RFC 9309 "unavailable" ⇒ complete disallow (fail closed) ⇒ list NOT read
```

**Outcome.** No compliant, reusable list contract could be verified inside the
budget, so the Step 5 adapter and the four-place allowlist registration were
**not started** — the ExecPlan 0014 §12 stop-condition-2 degradation path
(“人工采集通道 + 框架 + 文档”). What would unblock a later attempt: written
permission / a documented public 国聘 API, or a re-probe of
`career.cic.tsinghua.edu.cn` robots once it stops returning 503 (then its anonymous
list can be verified and an adapter built).

### Registration status (read before adding a China source)

Once a source passes verification, its identifier must be added in **four**
places at the same time (`docs/04` §5.5):

1. `jobpilot_sources/config.py` → `SUPPORTED_PROVIDERS`
2. `jobpilot_sources/providers/__init__.py` → `PROVIDERS`
3. `jobpilot_sources/cli.py` `--provider` choices (derived from (1))
4. **C++** `IsSupportedObservationSource` in
   `service/src/job/source_observation.cpp`

Adding fewer than four leaves the pipeline broken (config-time rejection, or a
runtime `400` from the observation endpoint). **As of this stage no China
identifier is registered**: the framework and the generic `public_page` provider
are fixture-tested only, and `SUPPORTED_PROVIDERS`/`PROVIDERS` still list exactly
`greenhouse`, `lever`, `ashby`. Registering a verified source is a coordinated
change that also touches the C++ allowlist.

## Batch rules

The internal observation API accepts at most 200 jobs per request. A board
larger than that is split into 200-job chunks and every chunk is published with
`complete=false`, so a partial observation can never close jobs. An empty board
is only published when completeness was proven; otherwise the run reports
"nothing published".

## Tests

```bash
cd source_adapters
python3 -m unittest discover -s jobpilot_sources/tests -t .
```

The suite is fixture driven and never contacts a live source. For an opt-in live
smoke (not part of CI), point a config at real boards and use `--dry-run`:

```bash
INTERNAL_API_TOKEN=dummy python3 -m jobpilot_sources sync \
  --config /tmp/live-config.yaml --dry-run
```

The 0015A pre-flight has its own suites: `tests/test_capture_quality.py`
(mirrored codes, thresholds and the 48 KiB byte boundary incl. a multi-byte CJK
case), `tests/test_registry.py` (platform-name parity with the C++ engine,
single-source guard), plus additions to `test_capture_artifact.py`,
`test_capture_publisher.py` and `test_capture_cli.py`.

### Opt-in source verification (not CI)

The register above was produced with a robots-first, low-volume probe built on
the package itself:

```python
from jobpilot_sources.fetch import Fetcher, FetchBudget, RetryPolicy

fetcher = Fetcher(
    retry=RetryPolicy(retries=1, base_delay_ms=800, max_delay_ms=4000),
    budget=FetchBudget(max_requests=12, max_pages=8, max_response_bytes=200_000),
    timeout=15.0,
)
robots = fetcher.get("https://www.example.com/robots.txt", check_robots=False)
print(robots.status)
url = "https://www.example.com/jobs"
print(fetcher.robots.verdict_for(url))       # per-path verdict
text, page = fetcher.get_text(url)           # refuses a disallowed path
```

Live verification stays opt-in and off by default: no test in this suite opens a
socket.

### Opt-in end-to-end check of the manual channel (not CI)

`tools/verify_manual_capture_e2e.py` runs against a **running** service and
proves the whole manual flow: publish a capture through
`POST /internal/jobs/import`, see it through `GET /api/v1/jobs` /
`GET /api/v1/jobs/{id}` with the 0014 fields, then fire a source-sync
observation for the same `source_url` and assert the manual record is
**byte-identical before and after** (F14), with `skipped_manual` reported. It
creates one job row and prints the cleanup SQL.

```bash
MYSQL_PASSWORD=... INTERNAL_API_TOKEN=... \
  ./build/presets/debug/service/jobpilot_service --config=/tmp/e2e.yaml &
E2E_TOKEN=... python3 tools/verify_manual_capture_e2e.py \
  --base-url http://127.0.0.1:18080 --token-env E2E_TOKEN
```

### Opt-in cross-language platform-name parity check (not CI)

`tools/verify_platform_name_parity.cpp` prints the C++ normalisation table, the
registry size and membership so the Python expectations can be produced from the
real `PlatformNameRegistry` instead of being assumed:

```bash
g++ -std=c++17 -I service/include -I backend/include \
    -o /tmp/verify_platform_name_parity \
    source_adapters/tools/verify_platform_name_parity.cpp \
    service/src/job/job_quality.cpp
cd <repository root> && /tmp/verify_platform_name_parity   # N|SIZE|C|F|SET rows
```

Its `N|…` rows are byte-identical to `tests/test_registry.py`'s normalisation
table; `tests/test_registry.py` also compares the env-var name and the default
path against `job_quality.h` when the C++ tree is present (and skips otherwise).
The default Python suite never compiles or runs C++.

### Browser evidence is still pending

The plan's Step 6 acceptance also asks for one capture from a **real
login-walled page**, produced by a human running the bookmarklet in their own
browser. That cannot be automated (and must not be simulated by logging in), so
it is recorded as **UNVERIFIED** in the ExecPlan until the product owner runs it.
What *is* verified offline is the extraction itself: `node` executes the real
`bookmarklet.js` against recorded fake DOMs (a rich JSON-LD page and a minimal
page), and the produced artifacts are fed back through the Python parser.
