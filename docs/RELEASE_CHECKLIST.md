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
- [ ] `git log -p <last-tag>..HEAD | grep -nE "ghp_|github_pat_|BEGIN [A-Z ]*PRIVATE KEY"` is empty. The script scans the working tree; history needs this extra check.
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
| History: credential shapes | `git log -p --all \| grep -aE 'ghp_[A-Za-z0-9]{20,}\|github_pat_[A-Za-z0-9_]{20,}\|AKIA[0-9A-Z]{16}\|-----BEGIN [A-Z ]*PRIVATE KEY\|xox[baprs]-'` | **no match** |
| History: files ever added | `git log --all --diff-filter=A --name-only --format='' \| sort -u \| grep -aiE '\.env$\|credentials\|secret\|token\|\.pem$\|\.key$\|id_rsa'` | only `.env.example` (placeholders) |
| History: private IPv4 | `git log -p --all \| grep -aoE '192\.168\.[0-9.]+' \| sort \| uniq -c` | two RFC1918 addresses inside the development host's own `192.168.150.x` network (gateway ×8, host ×2), masked here and in the tree — 0016 P5 and `b2a55be` redacted it from the working tree, history stays by policy — plus the intentional SSRF fixtures |
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

## 8. Evidence record

Append one block per release. Do not rewrite older blocks.

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
