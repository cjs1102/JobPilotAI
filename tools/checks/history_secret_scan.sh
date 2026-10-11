#!/usr/bin/env bash
# Read-only scan of the Git HISTORY for credential shapes (0017A follow-up).
#
# Why a script and not a pasted shell one-liner: inside a Markdown table the
# alternation pipes of an `grep -E` pattern have to be escaped as `\|`, and in
# ERE a backslash-pipe matches a LITERAL pipe - so the documented one-liners in
# the release checklist silently searched for strings like
# `ghp_...|github_pat_...` and could never match a real key. This script keeps
# the patterns in one place, with the escaping done once.
#
# What it checks (history, not the working tree - `release_preflight.py` covers
# the tree; a clean tree does NOT prove a clean history):
#   1. GitHub tokens (classic and fine-grained), GitLab PATs
#   2. PEM private-key headers (RSA / EC / OPENSSH / encrypted / plain)
#   3. AWS access key ids, Slack tokens, OpenAI-style keys
#   4. Informational counters: RFC1918 addresses and absolute home paths that
#      history still contains (not credentials, but visible after publication)
#   5. Files ever added whose NAME suggests a local secret
#
# Only counts, commit ids and file paths are printed - never the matched text,
# and never a credential value.
#
# Usage:
#   tools/checks/history_secret_scan.sh [--repo <path>] [--json]
#
# Exit codes: 0 = no credential shape found, 1 = at least one credential shape
# found, 2 = usage/repository problem.
set -uo pipefail

REPO=""
AS_JSON=0
while [ $# -gt 0 ]; do
    case "$1" in
        --repo) [ $# -ge 2 ] || { echo "history-secret-scan: --repo needs a value" >&2; exit 2; }; REPO="$2"; shift 2 ;;
        --json) AS_JSON=1; shift ;;
        -h|--help) sed -n '2,28p' "${BASH_SOURCE[0]}"; exit 0 ;;
        *) echo "history-secret-scan: unknown argument: $1" >&2; exit 2 ;;
    esac
done

if [ -z "$REPO" ]; then
    REPO="$(git rev-parse --show-toplevel 2>/dev/null)" || true
fi
[ -n "$REPO" ] && [ -d "$REPO/.git" ] || { echo "history-secret-scan: not a Git repository: ${REPO:-<none>}" >&2; exit 2; }

LOG_FILE="$(mktemp -t jobpilot-history.XXXXXX)"
trap 'rm -f "$LOG_FILE"' EXIT
git -C "$REPO" log --all --format='JOBPILOT_COMMIT %H' -p > "$LOG_FILE" 2>/dev/null
# The program arrives on stdin (heredoc), so the log goes through a file.
REPO_PATH="$REPO" LOG_FILE="$LOG_FILE" AS_JSON="$AS_JSON" python3 - <<'PY'
import json
import os
import re
import sys

CREDENTIAL_PATTERNS = (
    ("github classic token", re.compile(r"\bghp_[A-Za-z0-9]{20,}\b")),
    ("github fine-grained token", re.compile(r"\bgithub_pat_[A-Za-z0-9_]{20,}\b")),
    ("gitlab token", re.compile(r"\bglpat-[A-Za-z0-9_-]{20,}\b")),
    ("pem private key header", re.compile(r"-----BEGIN [A-Z0-9 ]*PRIVATE KEY-----")),
    ("aws access key id", re.compile(r"\bAKIA[0-9A-Z]{16}\b")),
    ("slack token", re.compile(r"\bxox[baprs]-[A-Za-z0-9-]{10,}\b")),
    ("openai-style key", re.compile(r"\bsk-[A-Za-z0-9]{20,}\b")),
)
INFO_PATTERNS = (
    ("rfc1918 address", re.compile(r"\b(?:10\.\d{1,3}|192\.168|172\.(?:1[6-9]|2\d|3[01]))\.\d{1,3}\.\d{1,3}\b")),
    ("absolute home path", re.compile(r"(?:/home/[A-Za-z0-9._-]+/|/Users/[A-Za-z0-9._-]+/)")),
)
SUSPICIOUS_NAME = re.compile(r"(^|/)(\.env(\..*)?|.*credentials.*|.*secret.*|.*\.pem|.*\.key|id_rsa.*)$", re.I)

commit = "unknown"
file_path = "unknown"
findings = []
info = {}
commits_scanned = 0

for raw_line in open(os.environ["LOG_FILE"], encoding="utf-8", errors="replace"):
    line = raw_line.rstrip("\n")
    if line.startswith("JOBPILOT_COMMIT "):
        commit = line.split(" ", 1)[1]
        commits_scanned += 1
        continue
    if line.startswith("+++ b/"):
        file_path = line[6:]
        continue
    if not line.startswith("+"):
        continue
    body = line[1:]
    for label, pattern in CREDENTIAL_PATTERNS:
        if pattern.search(body):
            findings.append({"label": label, "commit": commit[:7], "file": file_path})
    for label, pattern in INFO_PATTERNS:
        if pattern.search(body):
            info[label] = info.get(label, 0) + 1

# Files ever added whose name alone suggests a local secret.
names = set()
for line in os.popen(f"git -C {os.environ['REPO_PATH']!r} log --all --diff-filter=A --name-only --format=''").read().splitlines():
    name = line.strip()
    if name and SUSPICIOUS_NAME.search(name):
        names.add(name)

payload = {
    "commits_scanned": commits_scanned,
    "credential_findings": findings,
    "info_counters": info,
    "suspicious_added_names": sorted(names),
}

if os.environ.get("AS_JSON") == "1":
    print(json.dumps(payload, ensure_ascii=False, indent=2))
else:
    print(f"history scan: {commits_scanned} commits, "
          f"{len(findings)} credential-shape finding(s)")
    for finding in findings:
        print(f"  CREDENTIAL {finding['label']}: commit {finding['commit']} file {finding['file']}")
    for label, count in sorted(info.items()):
        print(f"  info: {label} x{count} (values deliberately not printed)")
    if names:
        print(f"  note: files ever added with a secret-looking name: {', '.join(sorted(names))}")
    print("HISTORY_SCAN_CLEAN" if not findings else "HISTORY_SCAN_FINDINGS")

sys.exit(1 if findings else 0)
PY
