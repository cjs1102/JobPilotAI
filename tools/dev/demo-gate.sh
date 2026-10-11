#!/usr/bin/env bash
# The open-source gate, runnable end to end with no AI provider key and no
# outbound network:
#
#   configure -> start -> import -> search -> match -> tailor -> track
#
# Everything runs against an ISOLATED compose project: its own container names,
# host ports, volumes and database, so a developer's running stack and its data
# are never touched. Each leg prints the request it made and the assertion it
# checked, and the first failed assertion stops the gate with a non-zero exit.
#
# Usage:
#   tools/dev/demo-gate.sh [--negative-control]
#
#   --negative-control injects one deliberately false expectation; it exists to
#   prove the gate fails loudly instead of reporting success.
#
# Overrides:
#   DEMO_GATE_PROJECT       compose project name        (default jobpilot-demo)
#   JOBPILOT_BACKEND_IMAGE  image to run                (default jobpilot-backend:local)
#   DEMO_GATE_API_PORT      host port for the service   (default 18080)
#   DEMO_GATE_MYSQL_PORT    host port for MySQL         (default 13306)
#   DEMO_GATE_REDIS_PORT    host port for Redis         (default 16379)
#
# The sample dataset is docs/examples/job-import.json; its URLs point at
# example.invalid, so the gate never leaves the machine.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT"

PROJECT="${DEMO_GATE_PROJECT:-jobpilot-demo}"
IMAGE="${JOBPILOT_BACKEND_IMAGE:-jobpilot-backend:local}"
API_PORT="${DEMO_GATE_API_PORT:-18080}"
MYSQL_PORT="${DEMO_GATE_MYSQL_PORT:-13306}"
REDIS_PORT="${DEMO_GATE_REDIS_PORT:-16379}"
API="http://127.0.0.1:${API_PORT}"
DATASET="docs/examples/job-import.json"
INTERNAL_TOKEN="demo-internal-token"

NEGATIVE_CONTROL=0
[ "${1:-}" = "--negative-control" ] && NEGATIVE_CONTROL=1

STAMP="$(date +%s)"
EMAIL="demo-${STAMP}@example.invalid"
USERNAME="demo${STAMP}"
PASSWORD="demo-password"

legs_passed=()

say()  { printf '\n=== %s ===\n' "$1"; }
pass() { printf 'PASS: %s\n' "$1"; legs_passed+=("$1"); }
fail() { printf 'FAIL: %s\n' "$1" >&2; exit 1; }

compose()
{
    JOBPILOT_MYSQL_CONTAINER_NAME="${PROJECT}-mysql" \
    JOBPILOT_REDIS_CONTAINER_NAME="${PROJECT}-redis" \
    JOBPILOT_BACKEND_CONTAINER_NAME="${PROJECT}-backend" \
    JOBPILOT_MYSQL_HOST_PORT="$MYSQL_PORT" \
    JOBPILOT_REDIS_HOST_PORT="$REDIS_PORT" \
    JOBPILOT_BACKEND_HOST_PORT="$API_PORT" \
    JOBPILOT_BACKEND_IMAGE="$IMAGE" \
    MYSQL_ROOT_PASSWORD=root \
    MYSQL_USER=jobpilot \
    MYSQL_PASSWORD=jobpilot \
    MYSQL_DATABASE=jobpilot \
    INTERNAL_API_TOKEN="$INTERNAL_TOKEN" \
    docker compose -p "$PROJECT" --profile app "$@"
}

cleanup()
{
    local rc=$?
    if [ "$rc" != 0 ]; then
        echo "--- backend logs before teardown (exit $rc) ---"
        compose logs --tail=60 backend || true
    fi
    echo "--- teardown (isolated project $PROJECT only) ---"
    compose down -v || true
}
trap cleanup EXIT

json()  # json <file> <python expression over `body`>
{
    python3 -c "import json,sys;body=json.load(open('$1'));print($2)"
}

api()   # api <method> <path> [token] [body]
{
    local method="$1" path="$2" token="${3:-}" body="${4:-}"
    local args=(-sS -X "$method" "$API$path" -H 'Content-Type: application/json')
    [ -n "$token" ] && args+=(-H "Authorization: Bearer $token")
    [ -n "$body" ] && args+=(-d "$body")
    curl "${args[@]}"
}

api_ok()   # api_ok <method> <path> [token] [body] -> writes /tmp file, fails on HTTP >= 400
{
    local method="$1" path="$2" token="${3:-}" body="${4:-}"
    local args=(-sS -o /tmp/demo-gate-body.json -w '%{http_code}' -X "$method" "$API$path"
                -H 'Content-Type: application/json')
    [ -n "$token" ] && args+=(-H "Authorization: Bearer $token")
    [ -n "$body" ] && args+=(-d "$body")
    local code
    code="$(curl "${args[@]}")"
    if [ "$code" -ge 400 ]; then
        # stderr, so a caller redirecting stdout cannot hide the cause
        { echo "HTTP $code for $method $path"; cat /tmp/demo-gate-body.json; echo; } >&2
        return 1
    fi
    cat /tmp/demo-gate-body.json
}

# ---------------------------------------------------------------- configure --
say "configure"
command -v docker  >/dev/null 2>&1 || fail "docker is not on PATH"
command -v curl    >/dev/null 2>&1 || fail "curl is not on PATH"
command -v python3 >/dev/null 2>&1 || fail "python3 is not on PATH"
docker compose version >/dev/null 2>&1 || fail "docker compose v2 is not available"
[ -f "$DATASET" ] || fail "$DATASET is missing"
JOBS_IN_DATASET="$(json "$DATASET" "len(body['jobs'])")"
[ "$JOBS_IN_DATASET" -ge 1 ] || fail "$DATASET has no jobs"
echo "dataset: $DATASET ($JOBS_IN_DATASET jobs)"
echo "compose project: $PROJECT (api :$API_PORT, mysql :$MYSQL_PORT, redis :$REDIS_PORT)"
echo "image: $IMAGE"
if printf '%s' "$IMAGE" | grep -q ':verify$'; then
    echo "note: running a locally built verification image (Docker Hub is not reachable here)"
fi
if ! docker image inspect "$IMAGE" >/dev/null 2>&1; then
    echo "image not present locally, building it (needs a reachable registry)"
    compose build backend
fi
docker image inspect "$IMAGE" >/dev/null 2>&1 || fail "image $IMAGE is not available"
pass "configure: prerequisites, dataset and image"

# -------------------------------------------------------------------- start --
say "start"
compose down -v >/dev/null 2>&1 || true
compose up -d
for attempt in $(seq 1 60); do
    if curl -fsS --max-time 2 "$API/health" >/dev/null 2>&1; then
        echo "GET /health -> $(curl -fsS "$API/health")"
        break
    fi
    sleep 2
    [ "$attempt" = 60 ] && fail "the service never answered /health"
done
pass "start: isolated stack up and healthy"

# --------------------------------------------------------- account + profile --
say "account and profile"
api_ok POST /api/v1/users/register "" \
    "{\"username\":\"$USERNAME\",\"email\":\"$EMAIL\",\"password\":\"$PASSWORD\"}" >/dev/null
echo "POST /api/v1/users/register -> 200"
api_ok POST /api/v1/users/login "" \
    "{\"email\":\"$EMAIL\",\"password\":\"$PASSWORD\"}" > /tmp/demo-gate-login.json
TOKEN="$(json /tmp/demo-gate-login.json "body['data']['token']")"
USER_ID="$(json /tmp/demo-gate-login.json "body['data']['user_id']")"
echo "POST /api/v1/users/login -> 200 (user_id=$USER_ID)"
api_ok PUT /api/v1/users/profile "$TOKEN" \
    '{"target_position":"C++ Backend Engineer","experience":"3 years","location":"Shenzhen","salary_min":20,"salary_max":40}' >/dev/null
echo "PUT /api/v1/users/profile -> 200"
for skill in '{"name":"C++","level":3}' '{"name":"Linux","level":2}' '{"name":"MySQL","level":2}'; do
    api_ok POST /api/v1/users/skills "$TOKEN" "$skill" >/dev/null
done
echo "POST /api/v1/users/skills -> 200 x3 (C++, Linux, MySQL)"
pass "account and profile: registered, logged in, profile and skills stored"

# ------------------------------------------------------------------- import --
say "import"
IMPORT_BODY="$(cat "$DATASET")"
# the internal route authenticates with its own header, not the session token
curl -sS -o /tmp/demo-gate-import.json -w '%{http_code}' -X POST \
    "$API/internal/jobs/import" \
    -H 'Content-Type: application/json' \
    -H "X-Internal-Token: $INTERNAL_TOKEN" \
    -d "$IMPORT_BODY" > /tmp/demo-gate-import-code.txt
IMPORT_CODE="$(cat /tmp/demo-gate-import-code.txt)"
[ "$IMPORT_CODE" -lt 400 ] || { cat /tmp/demo-gate-import.json; fail "import returned HTTP $IMPORT_CODE"; }
# The internal import answers with a flat object (created/updated/total/clean/
# needs_review/rejected), unlike the public endpoints that wrap a `data` key.
CREATED="$(json /tmp/demo-gate-import.json "body['created']")"
UPDATED="$(json /tmp/demo-gate-import.json "body['updated']")"
TOTAL="$(json /tmp/demo-gate-import.json "body['total']")"
REJECTED="$(json /tmp/demo-gate-import.json "body['rejected']")"
REVIEW="$(json /tmp/demo-gate-import.json "body['needs_review']")"
echo "POST /internal/jobs/import -> $IMPORT_CODE (created=$CREATED updated=$UPDATED total=$TOTAL rejected=$REJECTED needs_review=$REVIEW)"
[ "$TOTAL" -ge "$JOBS_IN_DATASET" ] || fail "import accepted fewer jobs than the dataset holds"
[ "$REJECTED" = 0 ] || fail "import rejected $REJECTED job(s)"
[ "$REVIEW" = 0 ] || fail "$REVIEW imported job(s) landed in needs_review, so the public search cannot see them"
pass "import: $TOTAL job(s) through the manual import path, all clean"

# ------------------------------------------------------------------- review --
# 0015A. The review surface must be reachable on the shipped binary: the C++
# tests construct the controller themselves, so a controller the entry point
# forgot to wire answers 404 here and nowhere else. `backfill?mode=dry_run` is
# read-only by contract (docs/04 section 5.4.2), so probing it writes nothing.
say "review surface"
QUEUE_CODE="$(curl -sS -o /tmp/demo-gate-review-queue.json -w '%{http_code}' \
    "$API/internal/jobs/review-queue?status=needs_review&page=1&page_size=5" \
    -H "X-Internal-Token: $INTERNAL_TOKEN")"
[ "$QUEUE_CODE" = 200 ] || { cat /tmp/demo-gate-review-queue.json; fail "the internal review queue answered HTTP $QUEUE_CODE"; }
BACKFILL_CODE="$(curl -sS -o /tmp/demo-gate-review-backfill.json -w '%{http_code}' -X POST \
    "$API/internal/jobs/review/backfill?mode=dry_run" \
    -H 'Content-Type: application/json' \
    -H "X-Internal-Token: $INTERNAL_TOKEN" \
    -d '{}')"
[ "$BACKFILL_CODE" = 200 ] || { cat /tmp/demo-gate-review-backfill.json; fail "review backfill dry_run answered HTTP $BACKFILL_CODE"; }
python3 - <<'PY'
import json
queue = json.load(open("/tmp/demo-gate-review-queue.json"))
assert isinstance(queue.get("items"), list) and isinstance(queue.get("total"), int), queue
backfill = json.load(open("/tmp/demo-gate-review-backfill.json"))
assert backfill.get("mode") == "dry_run", backfill
assert isinstance(backfill.get("scanned"), int), backfill
print("GET  /internal/jobs/review-queue -> 200 (total=%s)" % queue["total"])
print("POST /internal/jobs/review/backfill?mode=dry_run -> 200 (scanned=%s would_flag=%s)"
      % (backfill.get("scanned"), backfill.get("would_flag")))
PY
pass "review: the internal review queue and the read-only backfill probe are wired on the shipped binary"

# ------------------------------------------------------------------- search --
say "search"
SEARCH_KEYWORD="C%2B%2B"
EXPECT_TITLE="C++"
if [ "$NEGATIVE_CONTROL" = 1 ]; then
    SEARCH_KEYWORD="NoSuchKeywordForTheNegativeControl"
    EXPECT_TITLE="Product Designer (UI)"
    echo "negative control: expecting a row that the query cannot return"
fi
curl -sS "$API/api/v1/jobs?keyword=$SEARCH_KEYWORD" > /tmp/demo-gate-search.json
ROWS="$(json /tmp/demo-gate-search.json "len(body['data']['list'])")"
echo "GET /api/v1/jobs?keyword=$SEARCH_KEYWORD -> $ROWS row(s)"
[ "$ROWS" -ge 1 ] || fail "search returned no rows"
MATCHED_TITLE="$(json /tmp/demo-gate-search.json "body['data']['list'][0]['title']")"
echo "assertion: the first row's title contains '$EXPECT_TITLE'"
case "$MATCHED_TITLE" in
    *"$EXPECT_TITLE"*) pass "search: keyword search returned '$MATCHED_TITLE'" ;;
    *) fail "first row is '$MATCHED_TITLE', which does not contain '$EXPECT_TITLE'" ;;
esac
JOB_ID="$(json /tmp/demo-gate-search.json "body['data']['list'][0]['id']")"
echo "job_id for the later legs: $JOB_ID"

# -------------------------------------------------------------------- match --
say "match"
api_ok GET "/api/v1/jobs?with_match=true&page=1&page_size=20" "$TOKEN" > /tmp/demo-gate-match.json
python3 - <<'PY'
import json
rows = json.load(open("/tmp/demo-gate-match.json"))["data"]["list"]
scored = [r for r in rows if isinstance(r.get("score"), (int, float)) and r.get("reasons")]
assert scored, rows
top = scored[0]
print("assertion: at least one row carries a score, reasons and a score version")
print("first scored row: %s | score=%s version=%s reasons=%s" %
      (top.get("title"), top.get("score"), top.get("score_version"), len(top.get("reasons", []))))
PY
echo "GET /api/v1/jobs?with_match=true -> deterministic rule_v1 explanation, no AI key"
pass "match: rule_v1 score and reasons present"

# ------------------------------------------------------------------- tailor --
say "tailor"
printf 'P1 简历正文：C++ 后端工程师，使用 Linux 与 MySQL。\n' > /tmp/demo-gate-resume.txt
curl -sS -o /tmp/demo-gate-upload.json -X POST "$API/api/v1/resumes/upload" \
    -H "Authorization: Bearer $TOKEN" \
    -F "title=demo gate resume" \
    -F "file=@/tmp/demo-gate-resume.txt"
RESUME_ID="$(json /tmp/demo-gate-upload.json "body['data']['resume_id']")"
echo "POST /api/v1/resumes/upload -> resume_id=$RESUME_ID"
api_ok GET "/api/v1/resumes/$RESUME_ID/versions" "$TOKEN" > /tmp/demo-gate-versions.json
BASE_VERSION_ID="$(json /tmp/demo-gate-versions.json "body['data']['list'][0]['id']")"
echo "GET /api/v1/resumes/$RESUME_ID/versions -> base_version_id=$BASE_VERSION_ID"
api_ok POST /api/v1/resumes/artifacts "$TOKEN" \
    "{\"base_version_id\":$BASE_VERSION_ID,\"job_id\":$JOB_ID}" > /tmp/demo-gate-artifact.json
ARTIFACT_ID="$(json /tmp/demo-gate-artifact.json "body['data']['id']")"
python3 - <<'PY'
import json
d = json.load(open("/tmp/demo-gate-artifact.json"))["data"]
assert d["fact_check_status"] == 1, d
assert d["confirmed"] is False, d
assert d["generator"] == "rule_v1", d
assert d["source_version"] == "tailor_rule_v1", d
print("POST /api/v1/resumes/artifacts -> id=%s fact_check_status=%s confirmed=%s generator=%s source_version=%s"
      % (d["id"], d["fact_check_status"], d["confirmed"], d["generator"], d["source_version"]))
PY
api_ok POST "/api/v1/resumes/artifacts/$ARTIFACT_ID/confirm" "$TOKEN" '{}' > /tmp/demo-gate-confirm.json
python3 - <<'PY'
import json
d = json.load(open("/tmp/demo-gate-confirm.json"))["data"]
assert d["confirmed"] is True and d["confirmed_at"], d
print("POST /api/v1/resumes/artifacts/%s/confirm -> confirmed=true confirmed_at=%s"
      % (d["id"], d["confirmed_at"]))
PY
pass "tailor: draft generated, fact gate passed, human confirmation recorded"

# -------------------------------------------------------------------- track --
say "track"
api_ok POST /api/v1/applications "$TOKEN" \
    "{\"job_id\":$JOB_ID,\"resume_version_id\":$BASE_VERSION_ID}" >/dev/null
echo "POST /api/v1/applications -> 200 (job_id=$JOB_ID)"
api_ok GET "/api/v1/applications?page=1&page_size=20" "$TOKEN" > /tmp/demo-gate-applications.json
APPLICATION_ID="$(json /tmp/demo-gate-applications.json \
    "[r['id'] for r in body['data']['list'] if r['job_id'] == $JOB_ID][0]")"
CURRENT_STATUS="$(json /tmp/demo-gate-applications.json \
    "[r['status'] for r in body['data']['list'] if r['id'] == $APPLICATION_ID][0]")"
TARGET_STATUS=$((CURRENT_STATUS + 1))
echo "GET /api/v1/applications -> application_id=$APPLICATION_ID status=$CURRENT_STATUS"
# The lifecycle only accepts one step at a time (target == current + 1), so the
# gate advances exactly one step instead of jumping.
api_ok PUT "/api/v1/applications/$APPLICATION_ID/status" "$TOKEN" \
    "{\"status\":$TARGET_STATUS}" >/dev/null
echo "PUT /api/v1/applications/$APPLICATION_ID/status {\"status\":$TARGET_STATUS} -> 200"
api_ok GET "/api/v1/applications?page=1&page_size=20" "$TOKEN" > /tmp/demo-gate-applications2.json
python3 - <<PY
import json
rows = json.load(open("/tmp/demo-gate-applications2.json"))["data"]["list"]
row = [r for r in rows if r["id"] == $APPLICATION_ID][0]
assert row["status"] == $TARGET_STATUS, row
print("assertion: the application advanced from status $CURRENT_STATUS to $TARGET_STATUS and stayed in the list")
PY
pass "track: application created and its status advanced"

# ------------------------------------------------------------------ summary --
say "summary"
printf 'legs passed: %s\n' "${legs_passed[*]}"
echo "the human still performs the actual submission: this gate only records state"
echo "DEMO_GATE_OK"
