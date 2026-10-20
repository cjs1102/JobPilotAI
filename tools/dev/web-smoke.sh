#!/usr/bin/env bash
# 0022 Web UI smoke.
#
# It exercises the real static resources and the same-origin API path without
# requiring the nginx image. A real Firefox UI leg is optional:
#
#   - compose mode (default): an isolated MySQL/Redis/backend stack plus the
#     stdlib Python static+proxy runner (fallback when nginx cannot be pulled);
#   - local mode: use an already-running backend (the CI integration job starts
#     the release binary against its ephemeral MySQL/Redis services);
#   - --server nginx: use a local nginx binary with a config generated from
#     docker/nginx/default.conf (host-substituted); the smoke prints which
#     server it used;
#   - --browser firefox: after the HTTP flow, drive the actual UI with
#     geckodriver/Firefox (optional; not a CI dependency).
#
# The compose project, ports, volume and database are isolated and torn down at
# the end. The smoke never talks to the developer stack's database.
#
# Usage:
#   tools/dev/web-smoke.sh
#   tools/dev/web-smoke.sh --mode local --backend-url http://127.0.0.1:8080
#   NGINX_BIN=/path/to/nginx tools/dev/web-smoke.sh --server nginx --browser firefox
#
# Overrides:
#   JOBPILOT_BACKEND_IMAGE   backend image for compose mode
#   WEB_SMOKE_PROJECT        compose project (default jobpilot-web-smoke)
#   WEB_SMOKE_API_PORT       backend host port (default 18082)
#   WEB_SMOKE_WEB_PORT       UI host port (default 18081)
#   WEB_SMOKE_MYSQL_PORT     MySQL host port (default 13308)
#   WEB_SMOKE_REDIS_PORT     Redis host port (default 16381)
#   INTERNAL_API_TOKEN       internal import token (default web-smoke-internal-token)
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT"

MODE="compose"
SERVER="python"
NGINX_BIN="${NGINX_BIN:-}"
BROWSER="${WEB_SMOKE_BROWSER:-none}"
GECKODRIVER_BIN="${GECKODRIVER_BIN:-geckodriver}"
BROWSER_SCREENSHOT_DIR="${WEB_SMOKE_SCREENSHOT_DIR:-}"
BACKEND_URL="http://127.0.0.1:8080"
PROJECT="${WEB_SMOKE_PROJECT:-jobpilot-web-smoke}"
API_PORT="${WEB_SMOKE_API_PORT:-18082}"
WEB_PORT="${WEB_SMOKE_WEB_PORT:-18081}"
MYSQL_PORT="${WEB_SMOKE_MYSQL_PORT:-13308}"
REDIS_PORT="${WEB_SMOKE_REDIS_PORT:-16381}"
IMAGE="${JOBPILOT_BACKEND_IMAGE:-jobpilot-backend:local}"
INTERNAL_TOKEN="${INTERNAL_API_TOKEN:-web-smoke-internal-token}"
DATASET="docs/examples/job-import.json"

while [ "$#" -gt 0 ]; do
    case "$1" in
        --mode) MODE="${2:-}"; shift 2 ;;
        --server) SERVER="${2:-}"; shift 2 ;;
        --nginx-bin) NGINX_BIN="${2:-}"; shift 2 ;;
        --browser) BROWSER="${2:-}"; shift 2 ;;
        --geckodriver) GECKODRIVER_BIN="${2:-}"; shift 2 ;;
        --screenshot-dir) BROWSER_SCREENSHOT_DIR="${2:-}"; shift 2 ;;
        --backend-url) BACKEND_URL="${2:-}"; shift 2 ;;
        --api-port) API_PORT="${2:-}"; shift 2 ;;
        --web-port) WEB_PORT="${2:-}"; shift 2 ;;
        --mysql-port) MYSQL_PORT="${2:-}"; shift 2 ;;
        --redis-port) REDIS_PORT="${2:-}"; shift 2 ;;
        --image) IMAGE="${2:-}"; shift 2 ;;
        --internal-token) INTERNAL_TOKEN="${2:-}"; shift 2 ;;
        -h|--help)
            sed -n '2,32p' "$0"
            exit 0
            ;;
        *)
            echo "unknown argument: $1" >&2
            exit 2
            ;;
    esac
done

if [ "$MODE" != "compose" ] && [ "$MODE" != "local" ]; then
    echo "mode must be compose or local" >&2
    exit 2
fi
if [ "$SERVER" != "python" ] && [ "$SERVER" != "nginx" ]; then
    echo "server must be python or nginx" >&2
    exit 2
fi
if [ "$BROWSER" != "none" ] && [ "$BROWSER" != "firefox" ]; then
    echo "browser must be none or firefox" >&2
    exit 2
fi
if [ "$MODE" = "compose" ]; then
    BACKEND_URL="http://127.0.0.1:${API_PORT}"
fi

TMP="$(mktemp -d)"
SERVER_PID=""
STACK_STARTED=0
cleanup() {
    local rc=$?
    set +e
    if [ -n "$SERVER_PID" ]; then
        kill "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
        echo "WEB_SMOKE_SERVER_STOPPED"
    fi
    if [ "$STACK_STARTED" = "1" ]; then
        echo "--- web smoke logs before teardown ---"
        if [ -f "$TMP/proxy.log" ]; then
            tail -20 "$TMP/proxy.log" || true
        fi
        compose down -v || true
        echo "WEB_SMOKE_TEARDOWN_OK"
    fi
    rm -rf "$TMP"
    exit "$rc"
}
trap cleanup EXIT

say() { printf '\n=== %s ===\n' "$1"; }
pass() { printf 'PASS: %s\n' "$1"; }
fail() { printf 'FAIL: %s\n' "$1" >&2; exit 1; }

json() {
    python3 -c "import json,sys;body=json.load(open('$1'));print($2)"
}

api_ok() {
    local method="$1" path="$2" token="${3:-}" body="${4:-}"
    local file="$TMP/api-body.json"
    local args=(-sS -o "$file" -w '%{http_code}' -X "$method"
                "$BACKEND_URL$path" -H 'Content-Type: application/json')
    [ -n "$token" ] && args+=(-H "Authorization: Bearer $token")
    [ -n "$body" ] && args+=(-d "$body")
    local code
    code="$(curl "${args[@]}")"
    if [ "$code" -ge 400 ]; then
        echo "HTTP $code for $method $path" >&2
        cat "$file" >&2 || true
        echo >&2
        exit 1
    fi
    cat "$file"
}

api_web() {
    local method="$1" path="$2" token="${3:-}" body="${4:-}"
    local file="$TMP/web-body.json"
    local args=(-sS -o "$file" -w '%{http_code}' -X "$method"
                "$WEB/api/v1$path" -H 'Content-Type: application/json')
    [ -n "$token" ] && args+=(-H "Authorization: Bearer $token")
    [ -n "$body" ] && args+=(-d "$body")
    local code
    code="$(curl "${args[@]}")"
    if [ "$code" -ge 400 ]; then
        echo "HTTP $code for web $method /api/v1$path" >&2
        cat "$file" >&2 || true
        echo >&2
        exit 1
    fi
    cat "$file"
}

compose() {
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

say "configure"
[ -f "$DATASET" ] || fail "$DATASET is missing"
command -v curl >/dev/null 2>&1 || fail "curl is not on PATH"
command -v python3 >/dev/null 2>&1 || fail "python3 is not on PATH"
[ -f web/index.html ] || fail "web/index.html is missing"

if [ "$MODE" = "compose" ]; then
    command -v docker >/dev/null 2>&1 || fail "docker is not on PATH"
    docker compose version >/dev/null 2>&1 || fail "docker compose v2 is not available"
    if ! docker image inspect "$IMAGE" >/dev/null 2>&1; then
        fail "backend image $IMAGE is not available locally; set JOBPILOT_BACKEND_IMAGE"
    fi
    echo "mode=compose project=$PROJECT api=:$API_PORT web=:$WEB_PORT"
else
    echo "mode=local backend=$BACKEND_URL web=:$WEB_PORT"
fi
echo "web server: $SERVER (python fallback or local nginx binary; compose nginx image not required)"
echo "internal token: configured (value not printed)"
pass "configure: prerequisites and dataset"

if [ "$MODE" = "compose" ]; then
    say "start isolated stack"
    compose down -v >/dev/null 2>&1 || true
    compose up -d
    STACK_STARTED=1
    for attempt in $(seq 1 60); do
        if curl -fsS --max-time 2 "$BACKEND_URL/health" >/dev/null 2>&1; then
            echo "GET $BACKEND_URL/health -> 200"
            break
        fi
        sleep 2
        [ "$attempt" = 60 ] && fail "the backend never answered /health"
    done
    pass "start: isolated MySQL/Redis/backend stack is healthy"
else
    say "check backend"
    curl -fsS --max-time 5 "$BACKEND_URL/health" >/dev/null \
        || fail "backend $BACKEND_URL/health is not reachable"
    echo "GET $BACKEND_URL/health -> 200"
    pass "start: external backend is healthy"
fi

say "start web server"
WEB="http://127.0.0.1:${WEB_PORT}"
if [ "$SERVER" = "nginx" ]; then
    [ -n "$NGINX_BIN" ] && [ -x "$NGINX_BIN" ] || fail "NGINX_BIN is not executable"
    mkdir -p "$TMP/nginx-prefix" "$TMP/nginx-temp/body" "$TMP/nginx-temp/proxy" \
        "$TMP/nginx-temp/fastcgi" "$TMP/nginx-temp/uwsgi" "$TMP/nginx-temp/scgi"
    python3 - "$ROOT" "$WEB_PORT" "$BACKEND_URL" "$TMP" <<'PYCONF'
import sys
from pathlib import Path

root, port, backend, tmp = sys.argv[1:5]
server = (Path(root) / "docker" / "nginx" / "default.conf").read_text()
server = server.replace("listen 80;", f"listen 127.0.0.1:{port};")
server = server.replace("root /usr/share/nginx/html;", f"root {root}/web;")
server = server.replace("http://backend:8080", backend)
config = f"""daemon off;
worker_processes 1;
pid {tmp}/nginx-prefix/nginx.pid;
error_log {tmp}/nginx-prefix/error.log;
events {{ worker_connections 128; }}
http {{
    access_log off;
    client_body_temp_path {tmp}/nginx-temp/body;
    proxy_temp_path {tmp}/nginx-temp/proxy;
    fastcgi_temp_path {tmp}/nginx-temp/fastcgi;
    uwsgi_temp_path {tmp}/nginx-temp/uwsgi;
    scgi_temp_path {tmp}/nginx-temp/scgi;
    types {{
        text/html html htm;
        text/css css;
        application/javascript js mjs;
        application/json json;
        image/svg+xml svg;
    }}
    {server}
}}
"""
Path(tmp, "nginx.conf").write_text(config)
PYCONF
    "$NGINX_BIN" -c "$TMP/nginx.conf" -p "$TMP/nginx-prefix" \
        > "$TMP/proxy.log" 2>&1 &
    SERVER_PID=$!
    echo "WEB_SMOKE_SERVER=nginx"
else
    python3 tools/dev/web_static_proxy.py \
        --root web --listen 127.0.0.1 --port "$WEB_PORT" --backend "$BACKEND_URL" \
        > "$TMP/proxy.log" 2>&1 &
    SERVER_PID=$!
    echo "WEB_SMOKE_SERVER=python-static-proxy"
fi
for attempt in $(seq 1 30); do
    if curl -fsS --max-time 2 "$WEB/" >/dev/null 2>&1; then
        break
    fi
    sleep 1
    [ "$attempt" = 30 ] && fail "the web server never served /"
done
pass "web: static page reachable through the same-origin server"

say "static page"
HTML_CODE="$(curl -sS -o "$TMP/index.html" -w '%{http_code}' "$WEB/")"
[ "$HTML_CODE" = "200" ] || fail "GET / returned HTTP $HTML_CODE"
grep -q 'JobPilotAI' "$TMP/index.html" || fail "index.html does not name JobPilotAI"
grep -q 'js/app.js' "$TMP/index.html" || fail "index.html does not load js/app.js"
curl -fsS "$WEB/health" >/dev/null || fail "GET /health through the proxy failed"
curl -fsS "$WEB/api/v1/jobs?page=1&page_size=1" >/dev/null \
    || fail "GET /api/v1/jobs through the proxy failed"
pass "web: index, /health and /api/v1/jobs are reachable"

say "account, profile and match prerequisites"
STAMP="$(date +%s)"
EMAIL="web-smoke-${STAMP}@example.invalid"
USERNAME="websmoke${STAMP}"
PASSWORD="web-smoke-password"

api_web POST /users/register "" \
    "{\"username\":\"$USERNAME\",\"email\":\"$EMAIL\",\"password\":\"$PASSWORD\"}" >/dev/null
api_web POST /users/login "" \
    "{\"email\":\"$EMAIL\",\"password\":\"$PASSWORD\"}" > "$TMP/login.json"
TOKEN="$(json "$TMP/login.json" "body['data']['token']")"
[ -n "$TOKEN" ] || fail "login returned no token"
api_web PUT /users/profile "$TOKEN" \
    '{"target_position":"C++ Backend Engineer","experience":"3 years","location":"Shenzhen","salary_min":20,"salary_max":40}' >/dev/null
api_web POST /users/skills "$TOKEN" '{"name":"C++","level":3}' >/dev/null
api_web POST /users/skills "$TOKEN" '{"name":"Linux","level":2}' >/dev/null
api_web POST /users/skills "$TOKEN" '{"name":"MySQL","level":2}' >/dev/null
echo "POST /users/register, /users/login, PUT /users/profile, 3 skills -> 200"
pass "auth: register/login/me prerequisites work through the web proxy"

say "resume prerequisite"
printf 'Web smoke resume: C++ backend engineer using Linux and MySQL.\n' > "$TMP/resume.txt"
UPLOAD_CODE="$(curl -sS -o "$TMP/upload.json" -w '%{http_code}' -X POST \
    "$WEB/api/v1/resumes/upload" \
    -H "Authorization: Bearer $TOKEN" \
    -F "title=web smoke resume" \
    -F "file=@$TMP/resume.txt;type=text/plain")"
[ "$UPLOAD_CODE" -lt 400 ] || { cat "$TMP/upload.json" >&2; fail "resume upload HTTP $UPLOAD_CODE"; }
RESUME_ID="$(json "$TMP/upload.json" "body['data']['resume_id']")"
api_web GET "/resumes/${RESUME_ID}/versions" "$TOKEN" > "$TMP/versions.json"
VERSION_ID="$(json "$TMP/versions.json" "body['data']['list'][0]['id']")"
echo "POST /resumes/upload -> resume_id=$RESUME_ID; versions -> version_id=$VERSION_ID"
pass "resume: upload and version lookup work"

say "import and read jobs"
IMPORT_CODE="$(curl -sS -o "$TMP/import.json" -w '%{http_code}' -X POST \
    "$BACKEND_URL/internal/jobs/import" \
    -H 'Content-Type: application/json' \
    -H "X-Internal-Token: $INTERNAL_TOKEN" \
    -d @"$DATASET")"
[ "$IMPORT_CODE" -lt 400 ] || { cat "$TMP/import.json" >&2; fail "import HTTP $IMPORT_CODE"; }
TOTAL="$(json "$TMP/import.json" "body['total']")"
REJECTED="$(json "$TMP/import.json" "body['rejected']")"
REVIEW="$(json "$TMP/import.json" "body['needs_review']")"
[ "$TOTAL" -ge 1 ] || fail "import produced no jobs"
[ "$REJECTED" = "0" ] || fail "import rejected $REJECTED jobs"
[ "$REVIEW" = "0" ] || fail "import sent $REVIEW jobs to review"
echo "POST /internal/jobs/import -> total=$TOTAL rejected=$REJECTED needs_review=$REVIEW"

curl -fsS "$WEB/api/v1/jobs?keyword=C%2B%2B&page=1&page_size=5" > "$TMP/search.json"
ROWS="$(json "$TMP/search.json" "len(body['data']['list'])")"
[ "$ROWS" -ge 1 ] || fail "search returned no rows"
JOB_ID="$(json "$TMP/search.json" "body['data']['list'][0]['id']")"
curl -fsS "$WEB/api/v1/jobs/$JOB_ID" > "$TMP/detail.json"
DETAIL_TITLE="$(json "$TMP/detail.json" "body['data']['title']")"
echo "GET /api/v1/jobs?keyword=C++ -> $ROWS row(s); detail title=$DETAIL_TITLE"

api_web GET "/jobs?with_match=true&page=1&page_size=20" "$TOKEN" > "$TMP/match.json"
SCORED="$(python3 -c "import json;rows=json.load(open('$TMP/match.json'))['data']['list'];print(sum(1 for r in rows if isinstance(r.get('score'),(int,float)) and r.get('reasons')))" )"
[ "$SCORED" -ge 1 ] || fail "with_match returned no scored row with reasons"
echo "GET /api/v1/jobs?with_match=true -> $SCORED row(s) with score and reasons"
pass "jobs: search, detail and match explanations work"

say "favourite and status board"
api_web POST /applications "$TOKEN" \
    "{\"job_id\":$JOB_ID,\"resume_version_id\":$VERSION_ID}" > "$TMP/application.json"
APPLICATION_ID="$(json "$TMP/application.json" "body['data']['application_id']")"
api_web GET "/applications?page=1&page_size=20" "$TOKEN" > "$TMP/applications.json"
CURRENT_STATUS="$(json "$TMP/applications.json" "[r['status'] for r in body['data']['list'] if r['id'] == $APPLICATION_ID][0]")"
[ "$CURRENT_STATUS" = "0" ] || fail "new application status is $CURRENT_STATUS, expected 0"
TARGET_STATUS=$((CURRENT_STATUS + 1))
api_web PUT "/applications/$APPLICATION_ID/status" "$TOKEN" "{\"status\":$TARGET_STATUS}" >/dev/null
api_web GET "/applications?page=1&page_size=20" "$TOKEN" > "$TMP/applications2.json"
FINAL_STATUS="$(json "$TMP/applications2.json" "[r['status'] for r in body['data']['list'] if r['id'] == $APPLICATION_ID][0]")"
[ "$FINAL_STATUS" = "$TARGET_STATUS" ] || fail "application status $FINAL_STATUS, expected $TARGET_STATUS"
echo "POST /applications -> id=$APPLICATION_ID status=0; PUT status=$TARGET_STATUS -> 200"
pass "track: favourite and one status transition work"

if [ "$BROWSER" = "firefox" ]; then
    say "browser smoke (Firefox + geckodriver)"
    command -v "$GECKODRIVER_BIN" >/dev/null 2>&1         || fail "geckodriver is not on PATH"
    BROWSER_ARGS=(--web-url "$WEB" --email "$EMAIL" --password "$PASSWORD"
                  --geckodriver "$GECKODRIVER_BIN")
    [ -n "$BROWSER_SCREENSHOT_DIR" ] && BROWSER_ARGS+=(--screenshot-dir "$BROWSER_SCREENSHOT_DIR")
    python3 tools/dev/web_browser_smoke.py "${BROWSER_ARGS[@]}"
    pass "browser: login, resume upload, search/detail, favourite and status board"
fi

say "summary"
echo "WEB_SMOKE_OK"
