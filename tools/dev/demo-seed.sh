#!/usr/bin/env bash
# Seed a running demo stack with synthetic jobs (10-20, including C++ backend).
#
# Usage:
#   tools/dev/demo-seed.sh
#   DEMO_ENV_FILE=/tmp/demo.env DEMO_API_PORT=18080 tools/dev/demo-seed.sh
#
# The import is idempotent on source_url; re-running updates the same rows.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
ENV_FILE="${DEMO_ENV_FILE:-$ROOT/.env}"
FIXTURE="${DEMO_JOBS_FILE:-$ROOT/docs/examples/demo-jobs.json}"

if [ ! -f "$ENV_FILE" ]; then
    echo "demo-seed: $ENV_FILE is missing; run tools/dev/demo-up.sh first" >&2
    exit 2
fi
if [ ! -f "$FIXTURE" ]; then
    echo "demo-seed: fixture not found: $FIXTURE" >&2
    exit 2
fi

# Load the generated/compose env. DEMO_INTERNAL_TOKEN wins over a stale/empty
# value in the file only for this invocation; the file is never modified.
set -a
# shellcheck disable=SC1090
. "$ENV_FILE"
set +a

API_PORT="${DEMO_API_PORT:-${JOBPILOT_BACKEND_HOST_PORT:-8080}}"
TOKEN="${DEMO_INTERNAL_TOKEN:-${INTERNAL_API_TOKEN:-}}"
BASE="http://127.0.0.1:${API_PORT}"

if [ -z "$TOKEN" ]; then
    echo "demo-seed: INTERNAL_API_TOKEN is empty; set it in $ENV_FILE or DEMO_INTERNAL_TOKEN" >&2
    exit 2
fi

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

for _ in $(seq 1 60); do
    if curl -fsS --max-time 2 "$BASE/health" >/dev/null 2>&1; then
        break
    fi
    sleep 0.5
done
curl -fsS --max-time 2 "$BASE/health" >/dev/null || {
    echo "demo-seed: backend is not healthy at $BASE/health" >&2
    exit 1
}

echo "demo-seed: importing $FIXTURE through $BASE/internal/jobs/import"
CODE="$(curl -sS -o "$TMP/import.json" -w '%{http_code}' -X POST \
    "$BASE/internal/jobs/import" \
    -H 'Content-Type: application/json' \
    -H "X-Internal-Token: $TOKEN" \
    -d @"$FIXTURE")"
if [ "$CODE" -ge 400 ]; then
    cat "$TMP/import.json" >&2
    echo "demo-seed: import returned HTTP $CODE" >&2
    exit 1
fi

python3 - "$TMP/import.json" <<'PY'
import json
import sys

body = json.load(open(sys.argv[1]))
created = int(body.get("created", 0))
updated = int(body.get("updated", 0))
total = int(body.get("total", 0))
rejected = int(body.get("rejected", 0))
needs_review = int(body.get("needs_review", 0))
assert total >= 10, body
assert created + updated <= total, body
assert rejected == 0, body
assert needs_review == 0, body
print("DEMO_SEED_OK total=%d created=%d updated=%d" % (total, created, updated))
PY
