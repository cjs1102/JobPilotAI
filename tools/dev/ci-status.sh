#!/usr/bin/env bash
# Read-only GitHub Actions status for this repository (0017 ops tooling).
#
# The development host cannot use `gh` (not installed) and holds no credential in
# the environment, so this script reads a token from, in order:
#
#   1. $GITHUB_TOKEN        (or $GH_TOKEN)
#   2. the file $JOBPILOT_GH_TOKEN_FILE, defaulting to
#      ~/.config/jobpilot/github-token      (0600, one line, outside the repo)
#
# The token is never printed, never written into the repository, and never
# becomes an argv element of an executed program: curl receives it through a
# bash process substitution (`--config <(printf ...)`, where printf is a shell
# builtin). Set the token up with, for example:
#
#   install -d -m 700 ~/.config/jobpilot
#   nano ~/.config/jobpilot/github-token     # paste the token, save, exit
#   chmod 600 ~/.config/jobpilot/github-token
#
# Usage:
#   tools/dev/ci-status.sh                      # the last 5 runs on the default branch
#   tools/dev/ci-status.sh --limit 10
#   tools/dev/ci-status.sh --sha 38f29fd        # the run(s) for one commit
#   tools/dev/ci-status.sh --run 38057642118    # one run's jobs and their durations
#   tools/dev/ci-status.sh --check              # credential/repository check only
#   tools/dev/ci-status.sh --json [--run <id>]  # machine-readable output
#
# A fine-grained token needs Actions: read, Contents: read and Metadata: read on
# this repository only; a classic token needs `repo` for a private repository.
#
# API responses are handed to the renderer through a private temporary file (a
# heredoc already owns python's stdin), and removed on every path.
#
# Exit codes: 0 success, 1 the API refused or returned an error, 2 usage or
# credential problem.
set -euo pipefail

REPO="${JOBPILOT_GH_REPO:-}"
TOKEN_FILE="${JOBPILOT_GH_TOKEN_FILE:-$HOME/.config/jobpilot/github-token}"
LIMIT=5
SHA=""
RUN_ID=""
MODE="runs"
AS_JSON=0
API_VERSION="2022-11-28"
TMP_FILES=()

usage_error() { printf 'ci-status: %s\n' "$1" >&2; exit 2; }
die() { printf 'ci-status: %s\n' "$1" >&2; exit 1; }

cleanup() { [ "${#TMP_FILES[@]}" -eq 0 ] || rm -f "${TMP_FILES[@]}"; }
trap cleanup EXIT

# temp_json <json>: writes the payload to a 0600 temp file, echoes its path.
temp_json()
{
    local path
    path="$(mktemp -t ci-status.XXXXXX)"
    chmod 600 "$path"
    printf '%s' "$1" > "$path"
    TMP_FILES+=("$path")
    printf '%s' "$path"
}

while [ $# -gt 0 ]; do
    case "$1" in
        --limit) [ $# -ge 2 ] || usage_error "--limit needs a value"; LIMIT="$2"; shift 2 ;;
        --sha)   [ $# -ge 2 ] || usage_error "--sha needs a value"; SHA="$2"; shift 2 ;;
        --run)   [ $# -ge 2 ] || usage_error "--run needs a value"; RUN_ID="$2"; MODE="run"; shift 2 ;;
        --repo)  [ $# -ge 2 ] || usage_error "--repo needs a value"; REPO="$2"; shift 2 ;;
        --check) MODE="check"; shift ;;
        --json)  AS_JSON=1; shift ;;
        -h|--help) sed -n '2,36p' "${BASH_SOURCE[0]}"; exit 0 ;;
        *) usage_error "unknown argument: $1" ;;
    esac
done

case "$LIMIT" in ''|*[!0-9]*) usage_error "--limit must be a positive integer" ;; esac
[ "$LIMIT" -ge 1 ] || usage_error "--limit must be a positive integer"
[ "$LIMIT" -le 20 ] || usage_error "--limit must be <= 20 (keep the response inside one temp payload)"

# --------------------------------------------------------------- repository --
if [ -z "$REPO" ]; then
    origin="$(git remote get-url origin 2>/dev/null || true)"
    case "$origin" in
        git@github.com:*) REPO="${origin#git@github.com:}"; REPO="${REPO%.git}" ;;
        https://github.com/*) REPO="${origin#https://github.com/}"; REPO="${REPO%.git}" ;;
    esac
fi
[ -n "$REPO" ] || usage_error "cannot determine the repository; pass --repo owner/name"

# -------------------------------------------------------------------- token --
TOKEN="${GITHUB_TOKEN:-${GH_TOKEN:-}}"
if [ -z "$TOKEN" ]; then
    if [ -s "$TOKEN_FILE" ]; then
        TOKEN="$(tr -d '\r\n' < "$TOKEN_FILE")"
    elif [ -e "$TOKEN_FILE" ]; then
        usage_error "the token file $TOKEN_FILE is empty; write the token into it (one line) or export GITHUB_TOKEN"
    else
        usage_error "no token: set GITHUB_TOKEN/GH_TOKEN or write $TOKEN_FILE (0600, one line)"
    fi
fi
[ -n "$TOKEN" ] || usage_error "no token: $TOKEN_FILE holds no usable value"

# ---------------------------------------------------------------------- API --
api_json()
{
    local response body code
    response="$(curl -sS --noproxy '*' -m 30 \
        --config <(printf 'header = "Authorization: Bearer %s"\n' "$TOKEN") \
        -H "Accept: application/vnd.github+json" \
        -H "X-GitHub-Api-Version: $API_VERSION" \
        -w '\n%{http_code}' \
        "https://api.github.com$1")" || die "curl failed for $1"
    code="${response##*$'\n'}"
    body="${response%$'\n'*}"
    case "$code" in
        2*) printf '%s' "$body" ;;
        401|403) die "GitHub refused the credential (HTTP $code): $body" ;;
        *) die "GitHub returned HTTP $code for $1: $body" ;;
    esac
}

# ---------------------------------------------------------------- rendering --
render_report()   # render_report <mode> <json>
{
    local mode="$1" json="$2" path
    path="$(temp_json "$json")"
    CI_MODE="$mode" CI_AS_JSON="$AS_JSON" CI_JSON_FILE="$path" python3 - <<'PY'
import json, os
from datetime import datetime

mode = os.environ["CI_MODE"]
as_json = os.environ["CI_AS_JSON"] == "1"
with open(os.environ["CI_JSON_FILE"], encoding="utf-8") as handle:
    data = json.load(handle)


def duration(start, end):
    if not start or not end:
        return "-"
    fmt = "%Y-%m-%dT%H:%M:%SZ"
    try:
        seconds = int((datetime.strptime(end, fmt) - datetime.strptime(start, fmt)).total_seconds())
    except ValueError:
        return "-"
    if seconds < 60:
        return f"{seconds}s"
    return f"{seconds // 60}m{seconds % 60:02d}s"


if mode == "check":
    print(f"login={data.get('login')} type={data.get('type')} scopes={data.get('scopes')}")
elif mode == "run":
    run = data["run"]
    print(f"run {run['id']} {run.get('head_sha', '')[:7]} {run.get('event')} "
          f"{run.get('status')}/{run.get('conclusion')} "
          f"total={duration(run.get('run_started_at') or run.get('created_at'), run.get('updated_at'))}")
    for job in data.get("jobs", []):
        print(f"  {job['name']:<44} {job.get('status')}/{job.get('conclusion')} "
              f"{duration(job.get('started_at'), job.get('completed_at'))}")
else:
    for run in data.get("workflow_runs", []):
        print(f"{run['id']}  {run.get('head_sha', '')[:7]}  {run.get('event'):<12} "
              f"{run.get('status')}/{run.get('conclusion')}  {run.get('created_at')}  "
              f"{duration(run.get('run_started_at') or run.get('created_at'), run.get('updated_at'))}  "
              f"{run.get('name')}")
PY
}

combine_run()   # combine_run <run_json> <jobs_json>
{
    local run_path jobs_path
    run_path="$(temp_json "$1")"
    jobs_path="$(temp_json "$2")"
    CI_RUN_FILE="$run_path" CI_JOBS_FILE="$jobs_path" python3 - <<'PY'
import json, os

with open(os.environ["CI_RUN_FILE"], encoding="utf-8") as handle:
    run = json.load(handle)
with open(os.environ["CI_JOBS_FILE"], encoding="utf-8") as handle:
    jobs = json.load(handle).get("jobs", [])
print(json.dumps({"run": run, "jobs": jobs}))
PY
}

# --------------------------------------------------------------------- modes --
if [ "$MODE" = "check" ]; then
    user_json="$(api_json /user)"
    repo_json="$(api_json "/repos/$REPO")"
    if [ "$AS_JSON" = 1 ]; then
        python3 -c '
import json, sys
print(json.dumps({"user": json.loads(sys.argv[1]), "repo": json.loads(sys.argv[2])}))
' "$user_json" "$repo_json"
    else
        render_report check "$user_json"
        python3 -c '
import json, sys
repo = json.loads(sys.argv[1])
print("repo=%s private=%s default_branch=%s" % (
    repo.get("full_name"), repo.get("private"), repo.get("default_branch")))
' "$repo_json"
    fi
    exit 0
fi

if [ "$MODE" = "run" ]; then
    run_json="$(api_json "/repos/$REPO/actions/runs/$RUN_ID")"
    jobs_json="$(api_json "/repos/$REPO/actions/runs/$RUN_ID/jobs?per_page=100")"
    if [ "$AS_JSON" = 1 ]; then
        combine_run "$run_json" "$jobs_json"
    else
        render_report run "$(combine_run "$run_json" "$jobs_json")"
    fi
    exit 0
fi

query="/repos/$REPO/actions/runs?per_page=$LIMIT"
[ -n "$SHA" ] && query="$query&head_sha=$SHA"
runs_json="$(api_json "$query")"
if [ "$AS_JSON" = 1 ]; then
    printf '%s\n' "$runs_json"
else
    render_report runs "$runs_json"
fi
