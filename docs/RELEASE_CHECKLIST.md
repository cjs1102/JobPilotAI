# Release checklist

> Applies to any release that an outside reader is meant to trust: a tag, a
> "publish this" commit, or the moment the repository is made public. Every
> checkbox is either **automated** (a command whose raw output is the evidence)
> or **OWNER** (something only the repository owner can see or do). A skipped
> automated item is not green.
>
> The automated half lives in
> [`tools/checks/release_preflight.py`](../tools/checks/release_preflight.py):
> offline, stdlib-only, exit code 0 only when nothing FAILed.
>
> ```bash
> python3 tools/checks/release_preflight.py            # release commit: clean tree required
> python3 tools/checks/release_preflight.py --json     # machine-readable copy
> python3 tools/checks/release_preflight.py --allow-dirty   # preparation only, never the release commit
> ```

## 1. Automated preflight

Run the command above from the repository root and paste its whole output into
[§8 Evidence record](#8-evidence-record).

| # | Item | What the script checks | Expected |
|---|---|---|---|
| 1 | Licence | `LICENSE` exists, is non-empty, names the MIT licence; `THIRD_PARTY_NOTICES.md` is non-empty | `PASS license` |
| 2 | Third-party notices | every `[requires]` entry of `service/conanfile.txt` is mentioned in `THIRD_PARTY_NOTICES.md`, and the non-Conan MySQL client is mentioned too | `PASS notices`, one `covered:` line per dependency |
| 3 | Secrets and private details | tracked text files contain no GitHub/AWS/Slack/OpenAI-style token, no private-key block, no tracked `.env`, and no private IPv4 outside test fixtures | `PASS secrets` |
| 4 | README for outsiders | the English Quick Start, the bootstrap command, the configure command, the local run command and the documentation index pointer are all present | `PASS readme` |
| 5 | Links | every relative Markdown link in `README.md`, `THIRD_PARTY_NOTICES.md`, `docs/README.md` resolves | `PASS links` |
| 6 | Workflows | `actionlint` reports 0 issues on `.github/workflows/*.yml` | `PASS workflows`; `SKIP` means actionlint is not installed — install it and re-run, do not tick this box on a SKIP |
| 7 | Repository hygiene | working tree clean, no tracked `.env` or `build/` file, no tracked file above 5 MiB | `PASS hygiene` |
| 8 | Developer paths | tracked text files name no absolute developer-machine path (`/home/<user>/`, `/Users/<user>/`, `C:\Users\<user>\`) | `PASS local-paths` |

`actionlint` is not vendored. Install it once (`go install
github.com/rhysd/actionlint/cmd/actionlint@latest`, or a release binary) or point
the script at a local copy with `ACTIONLINT=/path/to/actionlint`.

## 2. Licence and third-party notices

- [ ] `LICENSE` is the licence the project actually ships (MIT) and still carries
      the right copyright holder. **Evidence:** `sed -n '1,4p' LICENSE`.
- [ ] `THIRD_PARTY_NOTICES.md` covers every path that ships third-party code:
      vendored sources (`third_party/libbcrypt`), the Conan dependencies, the
      Python dependencies, and the non-Conan MySQL client. **Evidence:**
      preflight items 1 and 2, plus `git grep -n "libbcrypt\|conanfile" THIRD_PARTY_NOTICES.md`.
- [ ] A new dependency added since the last release has a notice entry and, where
      its licence requires it, its licence text. **Evidence:** the release's
      `git log --oneline <last-tag>..HEAD -- service/conanfile.txt source_adapters/pyproject.toml ai_worker/`.

### Verified licence evidence (2026-10-11)

Read from the packages themselves, not from a recipe description or a README
sentence. Paths are in the build host's Conan cache (`~/.conan2/p/…/p/licenses/`).

| Component | Version | Licence | Evidence read |
|---|---|---|---|
| spdlog | 1.14.1 | MIT | `licenses/LICENSE` — "The MIT License (MIT)" |
| fmt | 10.2.1 | MIT | `licenses/LICENSE` — "(c) 2012 - present, Victor Zverovich" + MIT text |
| yaml-cpp | 0.8.0 | MIT | `licenses/LICENSE` — "(c) 2008-2015 Jesse Beder" + MIT text |
| nlohmann/json | 3.11.3 | MIT | `licenses/LICENSE.MIT` plus a `LICENSES/` directory |
| GoogleTest | 1.14.0 | BSD-3-Clause | `p/licenses/LICENSE` — "Copyright 2008, Google Inc." |
| hiredis | 1.2.0 | BSD-3-Clause | `licenses/COPYING` |
| PyYAML | floor `>=6.0`, **no lock file** (6.0.1 installed here) | MIT | package metadata `License: MIT`; declared in `source_adapters/pyproject.toml` and `ai_worker/pyproject.toml` |
| vendored `third_party/libbcrypt` | **no upstream revision recorded** | CC0 / public-domain dedication, Openwall `crypt_blowfish` with a permissive fallback | `PROVENANCE.md` + the licence headers inside `bcrypt.c` and `crypt_blowfish.c` |

**Open, and deliberately not concluded here: the MySQL C client.** The committed
`Dockerfile` builds on `ubuntu:24.04` with `libmysqlclient-dev` and its runtime
stage installs `libmysqlclient21`; `readelf -d` on the host-built binary shows
`NEEDED libmysqlclient.so.21`, i.e. Oracle MySQL 8.0.46
(`libmysqlclient21 8.0.46-0ubuntu0.24.04.4`). `/usr/share/doc/libmysqlclient21/copyright`
states GPLv2 **plus the Oracle FOSS License Exception** (`License: GPL-2+` for the
library files; the exception is quoted at its lines 6-25). Whether the *combined
work* — the `service/jobpilot_service` binary and the image — can be distributed
under MIT because of that exception is a legal judgement the maintainer has to
make; this repository records the facts and the conditions instead of asserting
compliance. Asymmetry worth knowing: the local scratch verification image
(`build/Dockerfile.verify`, used only because Docker Hub is unreachable from the
development host) installs `libmariadb3` and links `libmariadb.so.3` (LGPL-2.1),
so it says **nothing** about the shipped image.

## 3. README and documentation for outsiders

- [ ] `README.md` opens with what the project is and the English Quick Start, and
      can be followed on a clean machine: clone → `tools/dev/bootstrap.sh` →
      `cmake --preset release` → `cmake --build` → `docker compose up`. **Evidence:**
      preflight item 4; the Clean-environment reproduction section of the newest
      archived ExecPlan.
- [ ] The internal working state stays out of the README: it lives in
      `docs/ai/project-state.md`. **Evidence:** `grep -n "docs/ai" README.md` shows
      only pointers, and `docs/README.md` lists the reading order.
- [ ] Superseded design documents still carry their Superseded banner and are not
      presented as current. **Evidence:** `grep -l "Superseded" docs/*.md`.

## 4. Secrets and private details

- [ ] Preflight item 3 is `PASS`. **Evidence:** its raw output.
- [ ] The **history** is clean too: `tools/checks/history_secret_scan.sh` exits 0
      with `HISTORY_SCAN_CLEAN` (149 commits at the time of writing). It scans the
      whole `git log -p --all`, so it also covers files that no longer exist, and
      it prints only counts, commit ids and paths — never matched text. **A clean
      working tree proves nothing about the history, and a clean history proves
      nothing about the tree: these are two different checks.** The script exists
      because the previous one-liners pasted into Markdown had to escape `|` as
      `\|` for the table, and in ERE a backslash-pipe matches a literal pipe, so
      they could never match a real key.
- [ ] CI secrets are repository secrets, never literals in a workflow. **Evidence:**
      `grep -n "secrets\." .github/workflows/*.yml` and the absence of real
      credentials there (the CI-only `ci-root-password` is an ephemeral container
      password and is labelled as such).
- [ ] OWNER: the personal access token that was used for GitHub API work in
      earlier sessions is revoked, and a fresh one is stored only where it is
      needed. This checklist deliberately contains no token value.

## 5. CI, Actions minutes and repository metadata

- [ ] OWNER: the newest `ci` workflow run on `main` is green on all jobs.
      **Where:** <https://github.com/cjs1102/JobPilotAI/actions>. The three jobs
      are `clean build`, `full test suite (MySQL + Redis)` and — when its path
      filter matches — `container image and the end-to-end gate`.
- [ ] OWNER: the newest `image` workflow run is green, or its path filter
      correctly skipped it for a commit that touches no Dockerfile, compose file
      or `tools/dev/` script.
- [ ] OWNER: the Actions minutes this month are inside the plan's budget
      (private repositories are metered; the free tier is 2000 minutes/month).
      **Where:** <https://github.com/settings/billing>. The CI cost work of
      ExecPlan 0017-A is what keeps a push near one compile instead of two.
- [ ] OWNER: the repository About text, description and topics are filled in, and
      the homepage field points at something that exists.
- [ ] OWNER: the repository visibility is what the release claims. A private
      repository cannot be "open source in practice"; the anonymous API returning
      404 for <https://api.github.com/repos/cjs1102/JobPilotAI> is exactly what a
      private repository looks like from outside.

### About and Topics (prepared 2026-10-11, verified against the implementation)

**About, ready to paste:**

> A C++17 job-search assistant backend with Linux epoll/Reactor networking,
> MySQL/Redis persistence, explainable job matching, and local-first
> job-source capture tools.

**Topics, ready to paste:** `cpp`, `c-plus-plus`, `linux`, `epoll`,
`reactor-pattern`, `cmake`, `conan`, `mysql`, `redis`, `job-search`,
`job-matching`, `resume`, `job-search-tools`

Verification notes: each claim maps to shipped code — the epoll/Reactor network
layer, MySQL and Redis persistence, `rule_v1` matching with its explanation
payload, and the bookmarklet plus the local `publish-capture` publisher. Two
deliberate deviations from the draft wording: **`job-automation` was dropped**,
because the project has no submission path at all
(`tools/checks/no_submission_path.py` enforces that), so a topic implying
auto-applying would misdescribe it; and no AI-model-service claim appears,
because the provider layer is optional and BYOK. **Applying About/Topics is an
owner action** — GitHub Topics are public even while the repository is private,
so the copy lives here instead of being pushed.

## 6. Owner-only actions that make the release real

These are not automatable from a checkout and are not part of any stage's
implementation budget:

- [ ] OWNER: switch the repository to public (or state explicitly that this
      release is not public yet).
- [ ] OWNER: fill in About/description/topics and verify the rendered home page.
- [ ] OWNER: confirm the last CI run and paste its run id + per-job durations into
      §8, so the recorded evidence is a link a reader can follow.
- [ ] OWNER: rotate the personal access token.

## 7. Publish-safety review (2026-10-10)

Read-only, before any visibility change; the scans printed no credential value.
Every command below is reproducible from a full clone.

| Check | Command | Result |
|---|---|---|
| Current tree: secrets, private addresses, developer paths | `python3 tools/checks/release_preflight.py` (items 3 and 8) | `PASS secrets`, `PASS local-paths` over 420 tracked files |
| History: credential shapes, PEM headers, ever-added secret-named files | `tools/checks/history_secret_scan.sh` | `history scan: 149 commits, 0 credential-shape finding(s)` + `HISTORY_SCAN_CLEAN` (exit 0). Only `.env.example` was ever added under a secret-looking name. |
| History scanner controls | synthetic throwaway repositories (`/tmp/hs-pos`, `/tmp/hs-neg`) | positive control (a synthetic invalid PEM private-key header — the RSA form — plus a fake `ghp_…` value) → 2 findings, exit 1; negative control (prose about a PRIVATE KEY and a `-----BEGIN PUBLIC KEY-----` header) → 0 findings, exit 0 |
| History: private addresses and home paths | `tools/checks/history_secret_scan.sh` (info counters) | `absolute home path x6`, `rfc1918 address x14`, values deliberately not printed; two RFC1918 addresses inside the development host's own `192.168.150.x` network (gateway, host), masked here and in the tree — 0016 P5 and `b2a55be` redacted the tree, history stays by policy — plus the intentional SSRF fixtures |
| Licence | `LICENSE`, `THIRD_PARTY_NOTICES.md` | MIT, holder `JobPilotAI` (no personal name); every Conan dependency's licence was verified from the pinned package's own licence file |
| Fixture data | manual read of `docs/examples/*`, `source_adapters/**/fixtures/*` | synthetic only (`example.invalid`, `示例科技`); no customer, resume or employee data |

### Risks to accept or resolve before the repository becomes public

1. **`libmysqlclient21` is GPLv2 + Oracle's FOSS License Exception** and is linked
   into `service/jobpilot_service` and into the image built from `Dockerfile`
   (evidence recorded in `THIRD_PARTY_NOTICES.md`). The repository's own source
   stays MIT; the *binaries and images* are a combined work with a GPLv2 library.
   Options: (a) rely on the FOSS License Exception and say so in the notices
   (current state), (b) swap the client library for a differently licensed one,
   (c) publish source only and let each user link their distribution's client
   library. **Owner decision.**
2. **Commit author identity**: the history carries `CJS1102 <1151883089@qq.com>`
   (146 commits). Publishing exposes that address. Future commits can use a
   GitHub `noreply` address; rewriting the existing history is against this
   repository's rules. **Owner decision.**
3. **History-retained private addresses and paths** (two addresses in the
   development host's own `192.168.150.x` network, and `/home/<user>` ×7). Low sensitivity — RFC1918 addresses and a
   generic home path, no credentials — but retrievable after publication. The
   working tree is clean; removing them would need a history rewrite.
   **Owner decision.**
4. **The internal working record becomes public**: `docs/ai/` (agent workflow,
   project state), every `docs/exec-plans/` (stage history, including the 0014
   notes about 牛客/实习僧 and the owner's own browser run) and `docs/quality/`.
   That is deliberate transparency, but it is a choice, not an accident.
5. **Things a scanner will flag that are fine**: `tom@qq.com` in
   `docs/04-api-design.md` request examples, `MYSQL_PASSWORD=root` development
   placeholders in scripts and docs, and the ephemeral `ci-root-password` inside
   `.github/workflows/ci.yml`. All placeholders/ephemeral and documented; listed
   so the next scan is not surprising.
6. **Contract mismatch around `needs_review` visibility** (found on
   2026-10-11): `docs/04:461-462` says only clean rows are public and that
   `needs_review`/`quarantined` rows are reachable **only** through the protected
   internal endpoints, but `GET /api/v1/jobs/{id}` returns 200 for a
   `needs_review` row (it is `JobDao::FindActiveJobById`, which filters
   `status = 1` only). List/search hide it correctly. The content is a public
   posting, so this is not a confidentiality blocker, but the code and the
   contract disagree. **Owner decision:** gate the detail lookup as well (touches
   three call sites and the `JOB_PUBLIC_INCLUDE_NEEDS_REVIEW` rollback switch) or
   amend the contract to say only the discovery surfaces are gated.

## 8. Evidence record

Append one block per release. Do not rewrite older blocks.

### 2026-10-11 — release-candidate evidence

Collected on the development host. "Baseline" is the last commit before this
record; "final" is the commit that carries this section. Its own CI run is noted
below without chasing the documentation-only commit that appends this line (a run
cannot contain its own commit id).

| Item | Result | Evidence |
|---|---|---|
| Starting state | `main` = `origin/main` = `95d8e70`, working tree clean | `git status --porcelain` empty, `git rev-parse` |
| Preflight, baseline | `8 pass / 0 fail / 0 skip / 5 manual`, rc=0 | raw output outside the repository: `/tmp/jobpilot-release/preflight-1-baseline-95d8e70.txt`; audited for credential shapes and local paths (none), actionlint line carries the version |
| Preflight, final | `8 pass / 0 fail / 0 skip / 5 manual`, rc=0 | raw: `/tmp/jobpilot-release/preflight-2-final-<sha>.txt` |
| actionlint | **real PASS** — 1.7.7, rc=0 over `ci.yml` + `image.yml`; not a SKIP | `actionlint --version` plus the preflight's `PASS workflows` line |
| History credential scan | `history scan: 149 commits, 0 credential-shape finding(s)` + `HISTORY_SCAN_CLEAN`; info counters `rfc1918 address x14`, `absolute home path x6`; the only secret-looking file ever added is `.env.example` | `tools/checks/history_secret_scan.sh`; controls in throwaway repos: synthetic positive (invalid PEM header + fake `ghp_…`) → 2 findings, exit 1; negative (prose plus `-----BEGIN PUBLIC KEY-----`) → exit 0 |
| In-tree PEM regex | PRIVATE / RSA / EC / OPENSSH / ENCRYPTED headers all match; `PUBLIC KEY` and `CERTIFICATE` do not | `release_preflight.py` `SECRET_PATTERNS`, exercised directly with positive and negative controls |
| Baseline CI (`95d8e70`) | `ci` success 5m31s (clean build 4m06s + full suite 1m18s, run `38067378612`); `image` success 3m27s (run `38067378630`) | read 2026-10-10 with `tools/dev/ci-status.sh` |
| Final CI (this record's push) | `<run id>` `ci` and `image` — filled in below once the run lands | `tools/dev/ci-status.sh --sha <sha>` |
| Actions minutes and storage this month | **not verifiable with the available credential**: `GET /users/cjs1102/settings/billing/actions` → HTTP 403 "Resource not accessible by personal access token" (fine-grained tokens cannot read billing) | the repository owner has to read the Billing/Usage page; a job duration is not a monthly-usage figure |
| Licence evidence | section 2's table: spdlog / fmt / yaml-cpp / nlohmann = MIT, GoogleTest / hiredis = BSD-3-Clause, PyYAML = MIT with no lock file, libbcrypt = CC0/public domain with no recorded upstream revision | read from each package's own licence file |
| MySQL C client | verified facts: the committed `Dockerfile` installs `libmysqlclient21` at runtime, the host binary needs `libmysqlclient.so.21` (Oracle MySQL 8.0.46), and the Debian copyright states GPLv2 + Oracle FOSS License Exception (`License: GPL-2+`) | **decision pending with the owner**; no compliance conclusion is asserted, and the scratch verification image links `libmariadb.so.3`, so it is not evidence about the shipped image |
| Real capture acceptance | capture JSON, basename `capture-acceptance.json`, 6846 bytes, sha256 `1a625a3b…58d0`; it lives outside the repository (its absolute path stays in the local evidence note) and was never committed | block below |
| Defect found and fixed | the shipped binary never registered the 0015A review surface: `main.cpp` did not pass an `InternalJobReviewController`, so `/internal/jobs/review-queue` answered **404** while the C++ tests (which build the controller themselves) stayed green | found by the capture scaffold against the real binary; fixed in `service/src/main.cpp` and guarded by a new demo-gate leg |
| Observation — **contract mismatch, owner decision** | `docs/04-api-design.md:461-462` states that only `review_status = 0` rows are returned publicly and that `needs_review` / `quarantined` rows are reachable **only** through the protected internal endpoints; in practice `GET /api/v1/jobs/{id}` answers **200** for a `needs_review` row, because `JobDao::FindActiveJobById` filters `status = 1` only. The row stays out of list/search (verified). | Two options for the owner: (a) gate the detail lookup on `review_status = 0` as well — a behaviour change that touches the three `FindActiveJobById` call sites (public detail, AI match input, resume tailoring) and must honour `JOB_PUBLIC_INCLUDE_NEEDS_REVIEW`; or (b) amend the contract to say only the discovery surfaces are gated. Not changed here: it is a contract/behaviour decision, and the content itself is a public posting, so this is not a confidentiality blocker. |
| About/Topics | copy prepared and checked against real capabilities; **not applied** — repository metadata is an owner action | section 5 |

Real capture acceptance, review path, isolated stack (`jobpilot-capture`,
ports 18081 / 13307 / 16380, own volumes; the development database was never
touched):

```text
dry run: mode=review clean=0 needs_review=1 rejected=0 findings=employer_is_platform/company:needs_review
publish #1: created=1 updated=0 total=1     publish #2: created=0 updated=1 total=1
public search: probes=3 matched=0 (hidden, as required for needs_review)
review queue: queue_total=1 review_status=1 reason_code=employer_is_platform outcome=needs_review resolution=open
database: rows=1 review_status=1 source=manual description_present=yes
detail by id: HTTP 200 (recorded observation)
F14: colliding source_observation skipped_manual=1, record byte-identical
11 legs passed, CAPTURE_ACCEPTANCE_OK
quality issues that need human review (not bypassed): the employer field holds the
platform name ("牛客网") and the description mixes navigation, recommended jobs and
footer text
```

Raw logs stay outside the repository (`build/0017a/acceptance-real-capture2.log`,
`build/0017a/section-19.5-real.txt`, both gitignored scratch), and the capture JSON
was never copied into the repository.

```text
Release: <version or "public switch">     Date: ____-__-__
Commit: <sha>            Previous release commit/tag: <sha or tag>

preflight: python3 tools/checks/release_preflight.py
<raw output pasted verbatim, including SKIP and MANUAL lines>

CI: <run id> at <sha> — clean build: <status/duration>, full test suite: <status/duration>,
    container image and the end-to-end gate: <status/duration or "skipped by path filter">
Actions minutes used this month: <number> / <plan allowance>
Open items carried forward: <list, or "none">
```
