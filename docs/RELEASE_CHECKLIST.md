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

> **2026-10-21 — 0023 release-readiness update.** `SECURITY.md` and
> `CONTRIBUTING.md` are now present; Python dependencies are hash-pinned; the
> Dockerfile uses MariaDB Connector/C; TSan has a recorded first run; libbcrypt
> records an explicit unresolved-upstream hash manifest; and the benchmark
> baseline is refreshed. The current evidence is in **§9**; older rows below are
> dated historical evidence unless they say otherwise.

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
| PyYAML | `6.0.3`, exact hash-pinned `requirements.lock` in both Python packages (0023) | MIT | package metadata `License: MIT`; locks in `source_adapters/requirements.lock` and `ai_worker/requirements.lock`; CI uses `--require-hashes` |
| vendored `third_party/libbcrypt` | upstream revision **unresolved from this host**; SHA-256 manifest recorded (0023) | CC0 / public-domain dedication, Openwall `crypt_blowfish` with a permissive fallback | `PROVENANCE.md` revision record + the licence headers inside `bcrypt.c` and `crypt_blowfish.c` |

**MySQL C client: evaluated, with the smaller-risk option identified (2026-10-11).**
The committed `Dockerfile` builds on `ubuntu:24.04` with `libmysqlclient-dev` and
its runtime stage installs `libmysqlclient21`; `readelf -d` on the host-built
binary shows `NEEDED libmysqlclient.so.21`, i.e. Oracle MySQL 8.0.46
(`libmysqlclient21 8.0.46-0ubuntu0.24.04.4`). `/usr/share/doc/libmysqlclient21/copyright`
states GPLv2 **plus the Oracle FOSS License Exception** (`License: GPL-2+` for the
library files; the exception is quoted at its lines 6-25). Whether the *combined
work* — the `service/jobpilot_service` binary and the image — can be distributed
under MIT because of that exception is a legal judgement the maintainer has to
make; this repository records facts and conditions instead of asserting
compliance.

The alternative that removes that question is MariaDB Connector/C, whose **client
library is LGPL-2.1-or-later** — a library licence an MIT application links
against dynamically:

| Fact | Evidence |
|---|---|
| Ubuntu 24.04 ships the packages | `libmariadb3` `1:10.11.13-0ubuntu0.24.04.1` and `libmariadb-dev-compat` `1:10.11.13-0ubuntu0.24.04.1` (source package `mariadb`) |
| The client library, not the server, is what is linked — and it is LGPL | the package's own copyright file: `Files: libmariadb/*` → `License: LGPL-2+` (read from the installed `libmariadb3 1:10.11.18-0+deb12u1`); the GPL-2 stanzas in the same file cover the *server* files |
| No source change is needed | in an evaluation image with `libmariadb-dev` + `libmariadb-dev-compat`, CMake reported `Found mysqlclient, version 3.3.19` (MariaDB Connector/C) and built the whole release preset — the service binary **and every test binary** |
| Linking is dynamic, as LGPL-2.1 expects | `ldd` inside that image: `libmariadb.so.3 => /lib/x86_64-linux-gnu/libmariadb.so.3` |
| The real paths already ran on it against MySQL 8.0 | the Debian-based verification images (`jobpilot-backend:0017a`, `:reviewgate`) contain **only** `libmariadb3` and passed the real-capture acceptance (11 legs, `CAPTURE_ACCEPTANCE_OK`) and the demo gate (9 legs, `DEMO_GATE_OK`) against compose MySQL 8.0 |

**Status of the switch (honest):** the licence facts, the Ubuntu package set, the
unchanged-build evidence and the dynamic-link evidence are complete. The *full*
C++ suite with MariaDB-linked test binaries was still being produced when this
block was written (see the second evidence block in section 8 for its result), so
the committed `Dockerfile` still ships the Oracle client. The three-line change is
ready: `libmysqlclient-dev` → `libmariadb-dev` + `libmariadb-dev-compat` in the
builder, `libmysqlclient21` → `libmariadb3` in the runtime stage, and the two CI
package lines. It is applied only together with that compatibility result, and the
runner re-verifies the `ubuntu:24.04` shape end to end through the image job.

**0023 update (2026-10-21): the switch is applied.** The committed `Dockerfile`
now builds with `libmariadb-dev` + `libmariadb-dev-compat` and installs
`libmariadb3`; the `image` workflow asserts `libmariadb.so.3` and the absence of
`libmysqlclient`, then runs the demo gate against MySQL 8.0. The PR `image` job
must be green before merge. This remains a factual licence/linkage record, not a
legal conclusion; the Oracle evidence above is historical.

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
because the provider layer is optional and BYOK. **Applying this is a one-minute owner action**, because the available
credential cannot do it: `PATCH /repos/cjs1102/JobPilotAI` answers
`403 Resource not accessible by personal access token` (fine-grained tokens need
repository *administration* write to change metadata; the read-only token used
here deliberately does not have it). In the GitHub UI: repository → the gear icon
next to **About** → paste the description, add the topics, leave the homepage
empty, **Save changes**. Do not change the repository visibility in that dialog.
GitHub Topics are public even while a repository is private, which is why the
copy lives here rather than being pushed by an agent.

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
| Current tree: secrets, private addresses, developer paths | `python3 tools/checks/release_preflight.py` (items 3 and 8) | `PASS secrets`, `PASS local-paths` over **423** tracked files (refreshed 2026-10-11 after the review-gate change) |
| History: credential shapes, PEM headers, ever-added secret-named files | `tools/checks/history_secret_scan.sh` | `history scan: 161 commits, 0 credential-shape finding(s)` + `HISTORY_SCAN_CLEAN` (exit 0), refreshed 2026-10-11 at the close-out commit. Files ever added under a secret-looking name: `.env.example` and `tools/checks/history_secret_scan.sh` itself (the scanner's own name matches the pattern; harmless, listed so the next reader is not surprised). |
| History scanner controls | synthetic throwaway repositories (`/tmp/hs-pos`, `/tmp/hs-neg`) | positive control (a synthetic invalid PEM private-key header — the RSA form — plus a fake `ghp_…` value) → 2 findings, exit 1; negative control (prose about a PRIVATE KEY and a `-----BEGIN PUBLIC KEY-----` header) → 0 findings, exit 0 |
| History: private addresses and home paths | `tools/checks/history_secret_scan.sh` (info counters) | `absolute home path x7`, `rfc1918 address x14`, values deliberately not printed; two RFC1918 addresses inside the development host's own `192.168.150.x` network (gateway, host), masked here and in the tree — 0016 P5 and `b2a55be` redacted the tree, history stays by policy — plus the intentional SSRF fixtures |
| Licence | `LICENSE`, `THIRD_PARTY_NOTICES.md` | MIT, holder `JobPilotAI` (no personal name); every Conan dependency's licence was verified from the pinned package's own licence file |
| Fixture data | manual read of `docs/examples/*`, `source_adapters/**/fixtures/*` | synthetic only (`example.invalid`, `示例科技`); no customer, resume or employee data |
| CI artifacts (they become downloadable with the repository) | full download + `sha256` check + content scan of `release-build` (run `38128059424`) | **complete**: 1,175 members, integrity matched the API digest, no credential/PEM/AWS/Slack/OpenAI shape, no personal email, no RFC1918 address, no environment dump, no CI placeholder password, no developer home path; only runner-local paths (210 text + 105 binary files) — report `/tmp/jobpilot-release/artifact-audit.txt`. The whitelist was then trimmed (`d9fc0bb`) so object files, archives, dep/Make metadata, the CMake cache and `compile_commands.json` are no longer shipped |
| CI logs (they become public with the repository) | downloaded the `full test suite` job log of run `38128059424` and scanned it masked | 1,279 lines: 0 token shapes, 0 PEM headers, 0 AWS keys, 0 personal emails, 0 environment dumps; the ephemeral job-scoped `ci-root-password` literal appears (explained and hardened in `e23f98e`) |

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
6. ~~**Contract mismatch around `needs_review` visibility**~~ — **fixed
   2026-10-11** (`7ba62c8`, code first, contract unchanged). Every public read now
   shares one rule: the job list, the recommended list, the by-id detail (with
   the requirements it carries), AI match creation, resume tailoring and the
   job-existence check behind `POST /api/v1/applications` treat a queued row
   exactly like a missing one (404 / `kJobNotFound` / code 40001), so the API
   cannot confirm that a hidden row exists. The review queue, the review
   decisions and the AI worker's task input read the row regardless of its review
   state, so reviewing and processing still work. `JobDao` now exposes
   `FindPublicJobById` / `FindReviewableJobById` / `PublicJobExists` instead of a
   single ambiguous "find an active job" lookup. Evidence: 66 targeted tests,
   100% passed, and the real capture acceptance on the review path (11 legs,
   `CAPTURE_ACCEPTANCE_OK`, by-id detail now 404).

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
| Preflight, final (`9258603`) | `8 pass / 0 fail / 0 skip / 5 manual`, rc=0 | raw: `/tmp/jobpilot-release/preflight-2-final-9258603.txt` |
| actionlint | **real PASS** — 1.7.7, rc=0 over `ci.yml` + `image.yml`; not a SKIP | `actionlint --version` plus the preflight's `PASS workflows` line |
| History credential scan | `history scan: 149 commits, 0 credential-shape finding(s)` + `HISTORY_SCAN_CLEAN`; info counters `rfc1918 address x14`, `absolute home path x6`; the only secret-looking file ever added is `.env.example` | `tools/checks/history_secret_scan.sh`; controls in throwaway repos: synthetic positive (invalid PEM header + fake `ghp_…`) → 2 findings, exit 1; negative (prose plus `-----BEGIN PUBLIC KEY-----`) → exit 0 |
| In-tree PEM regex | PRIVATE / RSA / EC / OPENSSH / ENCRYPTED headers all match; `PUBLIC KEY` and `CERTIFICATE` do not | `release_preflight.py` `SECRET_PATTERNS`, exercised directly with positive and negative controls |
| Baseline CI (`95d8e70`) | `ci` success 5m31s (clean build 4m06s + full suite 1m18s, run `38067378612`); `image` success 3m27s (run `38067378630`) | read 2026-10-10 with `tools/dev/ci-status.sh` |
| Final CI (this record's push, `9258603`) | `ci` **success** 5m34s (clean build 4m08s + full test suite 1m18s, run `38127671542`); `image` **success** 2m31s (run `38127671516`) — the container job built the committed `ubuntu:24.04` image and ran the gate **including the new review-surface leg**, which is the runner-side proof of the wiring fix | read 2026-10-11 with `tools/dev/ci-status.sh`. The documentation-only commit that appends this row is not chased further: a run cannot contain its own commit id |
| CI logs and artifacts disclosure | one artifact per `ci` run: `release-build`, 28,767,607 bytes, `compression method=store`, expires 24 h after upload. The `full test suite` job log (1,279 lines) scanned with masked patterns: 0 GitHub token shapes, 0 PEM headers, 0 AWS keys, 0 personal emails, 0 environment-dump lines; it does contain the ephemeral `ci-root-password` literal from the workflow's own mysql command (job-scoped, its container is destroyed with the run) and 11 GitHub runner-local path strings (the runner's own home directory, written here without the literal prefix). A full artifact-content audit was **not** completed: the 28 MB download stalled on this host's link, so the statement rests on the artifact metadata plus the log scan | artifact metadata via the API; job log downloaded to `/tmp` and scanned without printing any value |
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

### 2026-10-11 — release-candidate close-out (second pass)

Collected after the first evidence block. Same rule: raw logs stay outside the
repository, and every number below comes from a command that was actually run.

| Item | Result | Evidence |
|---|---|---|
| `needs_review` reachable by direct id | **fixed** — a queued row is 404 on the public detail, rejected by `POST /api/v1/applications`, hidden from list/search, and still readable by the review queue and the AI worker | `7ba62c8`; 66 targeted tests 100% passed (0 failed, 1 skipped by design); real capture acceptance 11 legs / `CAPTURE_ACCEPTANCE_OK` with `a queued row is not readable by direct id (HTTP 404)` |
| Release-build artifact, full audit | **complete** — `release-build` from run `38128059424` (`040bd47`), 28,796,225 bytes, downloaded in full and `sha256` matched the API digest `c732fce0…146d`; 1 zip member, 1,175 tar members, 89,112,929 bytes, 282 text + 745 binary files scanned | report `/tmp/jobpilot-release/artifact-audit.txt`. Findings: **0** credential shapes, **0** PEM headers, **0** AWS/Slack/OpenAI keys, **0** personal email addresses, **0** RFC1918 addresses, **0** environment dumps, **0** occurrences of the CI placeholder password, **0** developer home paths. Only runner-local path strings appear (210 text + 105 binary files) |
| Artifact composition / whitelist | 123 `.o`, 3 `.a`, 305 `.make`, 181 `.d`, `CMakeCache.txt`, `compile_commands.json` and 61 `compiler_depend.ts` were being shipped although only the test binaries, `CTestTestfile.cmake` and the discovered `*_tests.cmake` files are needed at test time. Excluded now; the trimmed archive (21.0 MB vs 27.5 MB uncompressed-tar size) was re-extracted over the release tree in place and the suite ran from it: **309/309 passed in 96.5 s** | `d9fc0bb`; the runner's integration job re-verifies the round trip |
| CI service credentials (`ci-root-password` in the log) | **explained and hardened** — it is the root password of the job's own `mysql` service container, set from a workflow literal, scoped to the ephemeral job, destroyed with it, and absent from the artifact (scanned). Both service passwords are now overridable by `CI_MYSQL_ROOT_PASSWORD` / `CI_APP_DB_PASSWORD` with the documented literals as defaults, the three root steps use `MYSQL_PWD` instead of `--password=`, and the health check reads the container's own env; verified locally against `mysql:8.0` with the same options string (`exit 1` while initialising, then `exit 0`, `health=healthy`) | `e23f98e` |
| Commit identity for future commits | repository-local `user.name=cjs1102`, `user.email=180498659+cjs1102@users.noreply.github.com` (verified GitHub account id); the global config is untouched. **History still contains the previous personal address in its commit objects — fixing the configuration does not change that** | `git config --local --get user.email` |
| Local credential exposure | the shell history contained **one** line with a literal token value; it was redacted in place (mode preserved, 0 literal values remain). No credential-helper entry, `~/.git-credentials`, `~/.netrc`, `/tmp/gh.*` or environment token exists; `origin` uses SSH | counted, never printed |
| MySQL client evaluation | MariaDB Connector/C evaluated as the LGPL alternative: the Ubuntu 24.04 package set is verified, the installed package's own copyright file assigns `Files: libmariadb/*` to `License: LGPL-2+`, the project builds unchanged against it (`Found mysqlclient, version 3.3.19`), the binary carries `NEEDED libmariadb.so.3`, and both real-path suites are already green on images that contain only `libmariadb3`. The full suite with MariaDB-linked test binaries was still building when this row was written, so the committed `Dockerfile` still ships the Oracle client and the three-line switch stays unapplied | section 2's table; evaluation image `jobpilot-mariadb:verify` |
| Debug / ASan / UBSan matrix | **not completed in this pass, recorded as a gap**: the ASan tree was rebuilt to 47 % and then stopped so the single-core-bound MariaDB evaluation could finish on a 4-core host (three heavy jobs at once had already stretched everything 2-3x). The release matrix for the pushed commit does run in CI - 312 cases + coverage guard + both Python suites, green on `1ec48e1` - but no sanitizer matrix has been run since the review-gate change | re-run `build/l4-0017/run.sh` (or the three ctest presets) before the public switch; do not treat this row as a pass |
| Repository metadata (About/Topics) | **cannot be applied with the available credential**: `PATCH /repos/cjs1102/JobPilotAI` → HTTP 403 "Resource not accessible by personal access token" (fine-grained tokens need repository administration write for metadata). The verified copy is in section 5; applying it is a one-minute owner action | attempted 2026-10-11 |

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

## 9. 0023 release-readiness evidence (2026-10-21)

This is the current evidence for the 0023 release-readiness stage. It
supersedes older rows above where they conflict; the older rows remain dated
history.

| Item | Result | Evidence |
|---|---|---|
| `SECURITY.md` / `CONTRIBUTING.md` | **present**; private GitHub reporting, no personal email/path values; A history disclosure records facts + owner no-rewrite decision + no legal conclusion; CoC deferred until a reporting channel exists | `SECURITY.md`, `CONTRIBUTING.md`, `README.md`, preflight links |
| Python lock | **applied**: `PyYAML==6.0.3` wheel hash in both per-package locks; local venv `--require-hashes` install; suites 481 + 190 OK; CI installs the locks in a venv | `source_adapters/requirements.lock`, `ai_worker/requirements.lock`, `.github/workflows/ci.yml`; exact local pip output in closeout |
| MariaDB Connector/C (V8) | **Dockerfile switched** to `libmariadb-dev` + `libmariadb-dev-compat` (builder) and `libmariadb3` (runtime); image workflow asserts `libmariadb.so.3` and no `libmysqlclient` before demo gate. Local verification image `jobpilot-backend:verify` prints only `libmariadb.so.3`; **the new image build/CI run id is pending the owner-created PR and is a merge gate** | `Dockerfile`, `.github/workflows/image.yml`, `THIRD_PARTY_NOTICES.md`; local `ldd` output in closeout. No legal conclusion. |
| TSan first run | **52/52** at `ctest --preset tsan` (47.06 s) and **17/17** at `tsan-external` (18.38 s); no `WARNING: ThreadSanitizer`/data-race output. A test-harness-only fix made the ASLR `setarch` launcher apply to every test target; no product/P1 code change | `service/CMakeLists.txt` commit `3ea3bc5`; raw logs `build/l4-0023/tsan.log`, `tsan-external.log` (gitignored) |
| libbcrypt provenance | upstream revision **unresolved** from this host: GitHub API and `git ls-remote` fail at TLS (`SSL_ERROR_SYSCALL` / `gnutls_handshake`), so no commit/tag is invented; a SHA-256 manifest of the vendored sources is recorded | `third_party/libbcrypt/PROVENANCE.md` revision record; `THIRD_PARTY_NOTICES.md` |
| Benchmark baseline | **exclusively re-run 2026-10-21 00:38:57–00:48:41** (no concurrent build/test): health 2/4/8 × 32/128/512, DB jobs 2/4/8 × 32, 3 samples each, `pidstat` CPU/RSS; medians and the `~2.4×` difference vs the 2026-10-06 health baseline are documented with environment/load caveats; explicit “baseline, not threshold” | `docs/benchmarks/README.md`; raw `build/benchmarks-0023-exclusive/` (gitignored); `BENCHMARK_MATRIX_OK`; environment before/after files |
| Docs checker | **deferred** (approved): 0021's temporary schema/router checks remain; permanent checker is a maintenance-stage candidate | 0021 plan/verdict; no new tool added |
| L4 | Debug/ASan/UBSan each **312/312** (129.33 / 126.03 / 84.11 s), 1 known review-backfill SKIP each; Python lock suites 481 + 190; backfill dev SKIP / scratch **7 passed**; 0 sanitizer diagnostics; builds debug/asan/ubsan/release/root-release rc=0 | code head `933968a`; `build/l4-0023/` (gitignored) |
| Isolation | L4 project `jobpilot-l4-0023` (MySQL 14023/Redis 17023) and benchmark project `jobpilot-bench-0023` (14024/17024) torn down with `down -v`; no containers/volumes/networks remain | closeout raw output |
| A history | owner accepted, no rewrite; latest scanner output is recorded in the closeout (counts, never values) | `SECURITY.md` history section; `tools/checks/history_secret_scan.sh` |

**Known non-blocking environments:** the new MariaDB image cannot be built on
this VM (no `ubuntu:24.04`/Docker Hub), so its CI run id is pending the owner PR;
`actionlint` remains unavailable locally; TSan is not in the regular CI/L4
matrix; benchmark numbers are host-specific.
