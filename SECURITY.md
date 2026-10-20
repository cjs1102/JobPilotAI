# Security Policy

JobPilotAI is a **local-first, self-hosted** job-search workspace. It is not a
hosted service, and the Web UI is a local demo: HTTP, browser localStorage
sessions, and no production authentication or SLA. Do not deploy it as a
public multi-user service without a separate security review.

## Supported versions

| Version | Supported |
|---|---|
| `main` (current development) | yes, best effort |
| Latest tagged release, if any | yes, best effort |
| Older snapshots / forks | no |

There is no LTS promise. Security fixes land on `main`; backports are not
guaranteed.

## Reporting a vulnerability

Please use **GitHub private vulnerability reporting / Security Advisories** for
this repository. Do not open a public issue for a suspected vulnerability, and do
not include credentials, tokens, personal data, or exploit details in a public
issue.

If private reporting is not enabled yet (for example while the repository is
still private), contact the maintainer through the repository's private
GitHub channel and ask for the current reporting path. This file deliberately
does not publish a personal email address.

Please include: affected commit/version, a minimal reproduction, impact, and any
suggested fix. Do not test against systems or data you do not own or have
permission to test.

## Scope and security boundaries

- **No submission path**: the project does not auto-apply, auto-submit, fill
  employer forms, or bypass login walls/CAPTCHAs. The UI only opens the
  original posting for a human to apply manually.
- **Local demo authentication**: the Web UI stores the session token in
  `localStorage`; this is a demo trade-off, not a production auth design. The
  server session has a 24-hour TTL and there is no refresh-token flow.
- **Same-origin hosting**: the compose `web` profile serves static files and
  reverse-proxies `/api` and `/health` to the C++ service; no CORS is enabled.
- **No secrets in the repository**: tokens, passwords, private keys and personal
  data must not be committed. `tools/checks/release_preflight.py` and
  `tools/checks/history_secret_scan.sh` are the automated guards.
- **Local-first data**: MySQL/Redis volumes and resume storage belong to the
  operator. Do not point a test/benchmark run at production data.
- The code is not a security boundary for hostile multi-tenant use. Use separate
  credentials, network isolation, TLS, and an external auth layer if you expose
  it beyond localhost.

## Dependency and provenance notes

- Python runtime dependencies are pinned in the per-package
  `requirements.lock` files; CI installs them with `--require-hashes`.
- The shipped image is being moved to MariaDB Connector/C (`libmariadb3`) for
  the MySQL client library; see `THIRD_PARTY_NOTICES.md` and
  `docs/RELEASE_CHECKLIST.md` for the factual licence/linkage notes. This is not
  a legal opinion.
- Vendored bcrypt provenance is in `third_party/libbcrypt/PROVENANCE.md`.
- Known release limitations (historical metadata, local-demo auth, no WAF/SLA)
  are documented rather than implied to be solved.

## History metadata disclosure (owner decision A)

The repository history is **kept as-is** by owner decision (2026-10-20). It is
not rewritten and no force-push is performed. This section records facts without
printing the sensitive values.

At the planning scan on 2026-10-20 (the closeout re-runs it for the final count):

```text
history scan: 190 commits, 0 credential-shape finding(s)
  info: absolute home path x10 (values deliberately not printed)
  info: rfc1918 address x14 (values deliberately not printed)
  note: old author identity/email exists in historical commit metadata
```

- **Credential-shape findings: 0** at that scan.
- The history contains a small number of absolute developer-home paths and
  RFC1918 address literals in historical commits, plus an older author identity.
  They are metadata/disclosure items, not credentials.
- The owner accepts this disclosure and has decided **not** to rewrite history.
  Any future history rewrite or force-push is an owner-only decision and is
  outside the agent workflow.
- Counts move as commits are added. Re-run
  `tools/checks/history_secret_scan.sh` for the current count; the scanner prints
  counts, never the values.
- This section is a factual disclosure, **not a legal opinion** and not a claim
  that the history is free of all personal data.

## Out of scope

- Vulnerabilities in third-party services or deployments not controlled by this
  repository.
- Findings that require bypassing login walls, CAPTCHAs, rate limits, or a
  provider's terms.
- Denial-of-service testing against shared/production infrastructure.

## No warranty

This project is provided under the MIT licence, without warranty. See
[`LICENSE`](LICENSE).
