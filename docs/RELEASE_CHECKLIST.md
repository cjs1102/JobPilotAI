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
[§7 Evidence record](#7-evidence-record).

| # | Item | What the script checks | Expected |
|---|---|---|---|
| 1 | Licence | `LICENSE` exists, is non-empty, names the MIT licence; `THIRD_PARTY_NOTICES.md` is non-empty | `PASS license` |
| 2 | Third-party notices | every `[requires]` entry of `service/conanfile.txt` is mentioned in `THIRD_PARTY_NOTICES.md`, and the non-Conan MySQL client is mentioned too | `PASS notices`, one `covered:` line per dependency |
| 3 | Secrets and private details | tracked text files contain no GitHub/AWS/Slack/OpenAI-style token, no private-key block, no tracked `.env`, and no private IPv4 outside test fixtures | `PASS secrets` |
| 4 | README for outsiders | the English Quick Start, the bootstrap command, the configure command, the local run command and the documentation index pointer are all present | `PASS readme` |
| 5 | Links | every relative Markdown link in `README.md`, `THIRD_PARTY_NOTICES.md`, `docs/README.md` resolves | `PASS links` |
| 6 | Workflows | `actionlint` reports 0 issues on `.github/workflows/*.yml` | `PASS workflows`; `SKIP` means actionlint is not installed — install it and re-run, do not tick this box on a SKIP |
| 7 | Repository hygiene | working tree clean, no tracked `.env` or `build/` file, no tracked file above 5 MiB | `PASS hygiene` |

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
      §7, so the recorded evidence is a link a reader can follow.
- [ ] OWNER: rotate the personal access token.

## 7. Evidence record

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
