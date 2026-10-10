#!/usr/bin/env bash
# 0017-C: bookmarklet publish/read-back acceptance scaffold (ExecPlan 0017).
#
# It takes ONE capture JSON - the artifact capture/bookmarklet.js writes to the
# clipboard, saved to a file - and proves the whole manual channel end to end
# inside an ISOLATED compose project:
#
#   publish-capture -> POST /internal/jobs/import -> public search visibility
#     -> detail read-back (source / normalized fields / freshness)
#     -> F14: a colliding source-sync observation must not change the record
#
# It then prints the sanitized evidence lines ExecPlan 0014 section 19.5 needs
# and writes them to an evidence file, so the product owner only has to run this
# once against a capture copied from a real browser session.
#
# Usage:
#   tools/dev/capture-acceptance.sh --capture <capture.json> \
#     [--keep-evidence <file>] [--image <image>]
#
#   --capture        required; a capture_version 1 artifact (the synthetic
#                    sample is docs/examples/capture-acceptance-sample.json)
#   --keep-evidence  also copy the section 19.5 evidence block to this path
#   --image          backend image (default $JOBPILOT_BACKEND_IMAGE or
#                    jobpilot-backend:local; the development host has
#                    jobpilot-backend:verify because Docker Hub is unreachable)
#
# Isolation: this script never reuses a running stack. It owns compose project
# `jobpilot-capture`, host ports 18081 / 13307 / 16380 and its own volumes, and
# it refuses to run against the development or demo-gate coordinates. The
# internal token is generated here, lives only in this process environment, and
# is never printed nor written to the evidence file.
#
# Exit codes: 0 only after every assertion passed (CAPTURE_ACCEPTANCE_OK is the
# last line), 1 on a failed assertion, 2 on a usage/environment problem.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT"

PROJECT="${CAPTURE_ACCEPTANCE_PROJECT:-jobpilot-capture}"
IMAGE="${JOBPILOT_BACKEND_IMAGE:-jobpilot-backend:local}"
API_PORT="${CAPTURE_ACCEPTANCE_API_PORT:-18081}"
MYSQL_PORT="${CAPTURE_ACCEPTANCE_MYSQL_PORT:-13307}"
REDIS_PORT="${CAPTURE_ACCEPTANCE_REDIS_PORT:-16380}"
API="http://127.0.0.1:${API_PORT}"
INTERNAL_TOKEN="capture-acceptance-$(date +%s)-$$"

CAPTURE=""
KEEP_EVIDENCE=""
STAMP="$(date +%Y%m%d-%H%M%S)"
WORK="/tmp/capture-acceptance-${STAMP}"
EVIDENCE="/tmp/capture-acceptance-${STAMP}.txt"

legs_passed=0

say()  { printf '\n=== %s ===\n' "$1"; }
pass() { printf 'PASS: %s\n' "$1"; legs_passed=$((legs_passed + 1)); }
note() { printf 'note: %s\n' "$1"; }
fail() { printf 'FAIL: %s\n' "$1" >&2; exit 1; }
usage_error() { printf 'usage: %s\n' "$1" >&2; exit 2; }

while [ $# -gt 0 ]; do
    case "$1" in
        --capture) [ $# -ge 2 ] || usage_error "--capture needs a value"; CAPTURE="$2"; shift 2 ;;
        --keep-evidence) [ $# -ge 2 ] || usage_error "--keep-evidence needs a value"; KEEP_EVIDENCE="$2"; shift 2 ;;
        --image) [ $# -ge 2 ] || usage_error "--image needs a value"; IMAGE="$2"; shift 2 ;;
        -h|--help) sed -n '2,40p' "${BASH_SOURCE[0]}"; exit 0 ;;
        *) usage_error "unknown argument: $1" ;;
    esac
done

[ -n "$CAPTURE" ] || usage_error "run with --capture <capture.json> (see --help)"
[ -f "$CAPTURE" ] || usage_error "capture file not found: $CAPTURE"

# Resolve both path arguments to absolute paths before anything `cd`s into a
# subdirectory (the publisher runs from source_adapters/, so a relative --file
# would resolve against the wrong directory).
CAPTURE="$(cd "$(dirname "$CAPTURE")" && pwd)/$(basename "$CAPTURE")"
if [ -n "$KEEP_EVIDENCE" ]; then
    KEEP_EVIDENCE_DIR="$(dirname "$KEEP_EVIDENCE")"
    [ -d "$KEEP_EVIDENCE_DIR" ] || usage_error "directory for --keep-evidence does not exist: $KEEP_EVIDENCE_DIR"
    KEEP_EVIDENCE="$(cd "$KEEP_EVIDENCE_DIR" && pwd)/$(basename "$KEEP_EVIDENCE")"
fi

# ------------------------------------------------------------ isolation guard --
case "$PROJECT" in
    jobpilot|jobpilotai|jobpilot-demo)
        usage_error "refusing project name '$PROJECT': it belongs to the development or demo stack" ;;
esac
case "$API_PORT" in 8080|18080) usage_error "refusing API port $API_PORT: it belongs to the development or demo stack" ;; esac
case "$MYSQL_PORT" in 3306|13306) usage_error "refusing MySQL port $MYSQL_PORT: it belongs to the development or demo stack" ;; esac
case "$REDIS_PORT" in 6379|16379) usage_error "refusing Redis port $REDIS_PORT: it belongs to the development or demo stack" ;; esac

port_busy() { ss -ltn 2>/dev/null | awk '{print $4}' | grep -qE "[:.]$1\$"; }
for port in "$API_PORT" "$MYSQL_PORT" "$REDIS_PORT"; do
    port_busy "$port" && usage_error "host port $port is already in use; override with CAPTURE_ACCEPTANCE_*_PORT"
done

mkdir -p "$WORK"

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

STACK_STARTED=0

cleanup()
{
    local rc=$?
    # A usage error exits before anything was started; there is nothing to tear
    # down and the empty teardown messages would only be noise.
    if [ "$STACK_STARTED" != 1 ]; then
        return 0
    fi
    if [ "$rc" != 0 ]; then
        echo "--- backend logs before teardown (exit $rc) ---" >&2
        compose logs --tail=60 backend >&2 || true
    fi
    echo "--- teardown (isolated project $PROJECT only) ---"
    compose down -v >/dev/null 2>&1 || true
}
trap cleanup EXIT

publish_capture()
{
    ( cd "$ROOT/source_adapters" &&
      INTERNAL_API_TOKEN="$INTERNAL_TOKEN" \
      python3 -m jobpilot_sources publish-capture \
          --file "$CAPTURE" --base-url "$API" --token-env INTERNAL_API_TOKEN "$@" )
}

# publish_capture with its stdout captured; on failure both streams are shown,
# because the CLI reports input problems on stdout in --json mode.
run_publish_capture()   # run_publish_capture <outfile> [publish-capture args...]
{
    local out="$1"; shift
    if ! publish_capture "$@" > "$out" 2> "$WORK/publish-stderr.txt"; then
        echo "publish-capture exited non-zero; stdout:" >&2
        head -c 800 "$out" >&2 2>/dev/null || true
        echo >&2
        echo "publish-capture stderr:" >&2
        head -c 800 "$WORK/publish-stderr.txt" >&2 2>/dev/null || true
        echo >&2
        return 1
    fi
}

# ---------------------------------------------------------------- configure --
say "configure"
command -v docker  >/dev/null 2>&1 || usage_error "docker is not on PATH"
command -v python3 >/dev/null 2>&1 || usage_error "python3 is not on PATH"
docker compose version >/dev/null 2>&1 || usage_error "docker compose v2 is not available"

CAPTURE_SUMMARY="$(CAPTURE_FILE="$CAPTURE" python3 - <<'PY'
import json, os
with open(os.environ["CAPTURE_FILE"], encoding="utf-8") as handle:
    document = json.load(handle)
if document.get("capture_version") != 1:
    raise SystemExit(f"capture_version must be 1, got {document.get('capture_version')!r}")
job = document.get("job")
if not isinstance(job, dict):
    raise SystemExit("capture has no job object")
for key in ("title", "company", "source_url"):
    if not job.get(key):
        raise SystemExit(f"capture job.{key} is missing")
host = document.get("page_host") or ""
print("version=1 title_bytes=%d company_bytes=%d host=%s" % (
    len(job["title"].encode()), len(job["company"].encode()),
    host if host.endswith(".invalid") else "<redacted host>"))
PY
)" || usage_error "capture file rejected by the shape check: $CAPTURE"

echo "capture: $CAPTURE"
echo "capture shape: $CAPTURE_SUMMARY"
echo "compose project: $PROJECT (api :$API_PORT, mysql :$MYSQL_PORT, redis :$REDIS_PORT)"
echo "image: $IMAGE"
if [ -z "$KEEP_EVIDENCE" ]; then
    EVIDENCE_NOTE="(section 19.5 block also written to $EVIDENCE)"
else
    EVIDENCE_NOTE="(section 19.5 block also written to $EVIDENCE and $KEEP_EVIDENCE)"
fi
echo "evidence: $EVIDENCE_NOTE"

if ! docker image inspect "$IMAGE" >/dev/null 2>&1; then
    note "image $IMAGE is not present locally; building it (needs a reachable registry)"
    compose build backend
fi
docker image inspect "$IMAGE" >/dev/null 2>&1 || usage_error "image $IMAGE is not available"

DEV_CONTAINERS_BEFORE="$(docker ps --format '{{.Names}}' | grep -v "^${PROJECT}-" | sort | tr '\n' ' ')"
pass "configure: prerequisites, capture shape, ports free, image available"

# ------------------------------------------------------------------- start --
say "start"
compose down -v >/dev/null 2>&1 || true
compose up -d
STACK_STARTED=1
HEALTHY=0
for attempt in $(seq 1 60); do
    if curl -fsS --max-time 2 "$API/health" >/dev/null 2>&1; then
        HEALTHY=1
        echo "GET /health -> $(curl -fsS "$API/health")"
        break
    fi
    sleep 2
done
[ "$HEALTHY" = 1 ] || fail "the isolated service never answered /health"
pass "start: isolated stack up and healthy (project $PROJECT)"

# ------------------------------------------------------- publish (dry run) --
say "publish (dry run)"
DRY_RUN_JSON="$WORK/dry-run.json"
run_publish_capture "$DRY_RUN_JSON" --dry-run --json || fail "publish-capture --dry-run exited non-zero"
EXPECTED_JSON="$WORK/expected-payload.json"
DRY_SUMMARY="$(DRY_RUN_JSON="$DRY_RUN_JSON" EXPECTED_JSON="$EXPECTED_JSON" python3 - <<'PY'
import json, os
with open(os.environ["DRY_RUN_JSON"], encoding="utf-8") as handle:
    document = json.load(handle)
if document.get("ok") is not True or document.get("dry_run") is not True:
    raise SystemExit(f"dry run did not succeed: {json.dumps(document, ensure_ascii=False)[:400]}")
jobs = document.get("jobs") or []
if len(jobs) != 1:
    raise SystemExit(f"this scaffold accepts exactly one capture per run, got {len(jobs)}")
counters = document.get("counters") or {}
if counters.get("rejected") or counters.get("needs_review"):
    raise SystemExit(f"capture would be rejected or hidden for review: {json.dumps(counters)}")
payload = jobs[0]
if payload.get("source") != "manual":
    raise SystemExit(f"payload source is {payload.get('source')!r}, not 'manual'")
with open(os.environ["EXPECTED_JSON"], "w", encoding="utf-8") as handle:
    json.dump(payload, handle, ensure_ascii=False, indent=2)
print("clean=%s payload_fields=%s" % (counters.get("clean"), ",".join(sorted(payload))))
PY
)" || fail "dry run rejected the capture"
echo "dry run: $DRY_SUMMARY"
pass "publish (dry run): capture validates and normalizes to a clean manual payload"

# ---------------------------------------------------- publish (real, twice) --
say "publish"
PUBLISH1_JSON="$WORK/publish-1.json"
run_publish_capture "$PUBLISH1_JSON" --json || fail "publish-capture exited non-zero"
PUBLISH1_SUMMARY="$(PUBLISH_JSON="$PUBLISH1_JSON" TOKEN="$INTERNAL_TOKEN" MODE=first python3 - <<'PY'
import json, os
raw = open(os.environ["PUBLISH_JSON"], encoding="utf-8").read()
document = json.loads(raw)
if os.environ["TOKEN"] in raw:
    raise SystemExit("the internal token leaked into publish-capture output")
if document.get("ok") is not True:
    raise SystemExit(f"publish failed: {json.dumps(document, ensure_ascii=False)[:400]}")
expected = (1, 0, 1)
got = (document.get("created"), document.get("updated"), document.get("total"))
if got != expected:
    raise SystemExit(f"first publish created/updated/total={got}, expected {expected}")
print("ok=true submitted=%s created=%s updated=%s total=%s" % (
    document.get("submitted"), *got))
PY
)" || fail "first publish did not create exactly one job"
echo "publish #1: $PUBLISH1_SUMMARY"
pass "publish: the capture became exactly one manual job through /internal/jobs/import"

PUBLISH2_JSON="$WORK/publish-2.json"
run_publish_capture "$PUBLISH2_JSON" --json || fail "the second publish-capture exited non-zero"
PUBLISH2_SUMMARY="$(PUBLISH_JSON="$PUBLISH2_JSON" python3 - <<'PY'
import json, os
document = json.load(open(os.environ["PUBLISH_JSON"], encoding="utf-8"))
got = (document.get("created"), document.get("updated"), document.get("total"))
expected = (0, 1, 1)
if document.get("ok") is not True or got != expected:
    raise SystemExit(f"re-publish created/updated/total={got}, expected {expected}")
print("ok=true submitted=%s created=%s updated=%s total=%s" % (document.get("submitted"), *got))
PY
)" || fail "re-publishing the same capture was not an idempotent update"
echo "publish #2: $PUBLISH2_SUMMARY (uk_source_url idempotency)"
pass "publish (repeat): the same capture updates the same row, never duplicates it"

# ------------------------------------------------------------------ search --
say "public search visibility"
SEARCH_JSON="$WORK/search.json"
JOB_JSON="$WORK/job.json"
SEARCH_SUMMARY="$(API="$API" EXPECTED_JSON="$EXPECTED_JSON" SEARCH_JSON="$SEARCH_JSON" \
    JOB_JSON="$JOB_JSON" python3 - <<'PY'
import json, os, urllib.parse, urllib.request
with open(os.environ["EXPECTED_JSON"], encoding="utf-8") as handle:
    payload = json.load(handle)
api = os.environ["API"]
title = payload["title"]
candidates = [title, payload.get("company") or "", title[:12]]
last = None
for keyword in candidates:
    if not keyword:
        continue
    url = f"{api}/api/v1/jobs?keyword={urllib.parse.quote(keyword)}"
    with urllib.request.urlopen(url, timeout=10) as response:
        body = json.load(response)
    rows = body["data"]["list"]
    matching = [row for row in rows if row.get("title") == title]
    last = (keyword, len(rows), matching)
    if matching:
        break
if last is None or not last[2]:
    raise SystemExit(f"the published job is not visible in public search; last={last}")
keyword, row_count, matching = last
if len(matching) != 1:
    raise SystemExit(f"public search returned {len(matching)} rows with the exact title")
row = matching[0]
if not isinstance(row.get("id"), int) or row["id"] <= 0:
    raise SystemExit(f"the search row carries no usable id: {row.get('id')!r}")
with open(os.environ["SEARCH_JSON"], "w", encoding="utf-8") as handle:
    json.dump({"keyword": keyword, "rows": row_count}, handle, ensure_ascii=False)
with open(os.environ["JOB_JSON"], "w", encoding="utf-8") as handle:
    json.dump(row, handle, ensure_ascii=False)
print("keyword_len=%d rows=%d matched=1 id=%d" % (len(keyword), row_count, row["id"]))
PY
)" || fail "the published job is not visible through the public search"
echo "GET /api/v1/jobs?keyword=<redacted> -> $SEARCH_SUMMARY"
pass "search: the manual job is visible through the public query interface"

# ------------------------------------------------------- detail read-back ---
say "detail read-back"
DETAIL_BEFORE="$WORK/detail-before.json"
JOB_ID="$(JOB_JSON="$JOB_JSON" python3 -c 'import json,os;print(json.load(open(os.environ["JOB_JSON"]))["id"])')"
READBACK="$(API="$API" JOB_ID="$JOB_ID" EXPECTED_JSON="$EXPECTED_JSON" DETAIL_BEFORE="$DETAIL_BEFORE" \
    python3 - <<'PY'
import json, os, urllib.request
api = os.environ["API"]
with open(os.environ["EXPECTED_JSON"], encoding="utf-8") as handle:
    payload = json.load(handle)
job_id = int(os.environ["JOB_ID"])
with urllib.request.urlopen(f"{api}/api/v1/jobs/{job_id}", timeout=10) as response:
    document = json.load(response)
detail = document["data"]
with open(os.environ["DETAIL_BEFORE"], "w", encoding="utf-8") as handle:
    json.dump(detail, handle, ensure_ascii=False, sort_keys=True)

def check(label, ok, actual):
    if not ok:
        raise SystemExit(f"{label}: got {actual!r}")
    print(f"  {label}: {actual!r}")

# 0017A: the manual import contract now carries the optional `published_at`
# (docs/04 section 5.2), so it must round-trip into the public detail exactly
# like every other documented field. The only difference is the public name:
# `posted_at` is the column's read-back name.
PUBLIC_FIELD_ALIASES = {"published_at": "posted_at"}

check("source == manual", detail.get("source") == "manual", detail.get("source"))
check("source_url round-trips", detail.get("source_url") == payload.get("source_url"),
      detail.get("source_url"))
for key in ("title", "company", "location", "description", "education_required",
            "job_nature", "deadline", "deadline_date_only", "salary_min", "salary_max",
            "salary_currency", "published_at"):
    if key not in payload:
        continue
    if key == "deadline" and payload.get("deadline_date_only"):
        # F13: a date-only deadline is stored as the Beijing date at 04:00:00 and
        # rendered back as the date, so the payload value is not what the public
        # detail returns.
        expected = str(payload["deadline"])[:10]
        check("deadline renders as the Beijing date", detail.get(key) == expected,
              f"{detail.get(key)!r} == {expected!r}")
        continue
    public_key = PUBLIC_FIELD_ALIASES.get(key, key)
    if public_key not in detail:
        raise SystemExit(f"{key}: the public detail does not expose {public_key}")
    check(f"{key} round-trips", detail.get(public_key) == payload[key],
          f"{detail.get(public_key)!r} == {payload[key]!r}")
check("manual import leaves freshness unobserved", detail.get("last_seen_at") is None,
      detail.get("last_seen_at"))
check("manual import leaves freshness null", detail.get("freshness") is None,
      detail.get("freshness"))
print("fields_compared=%d" % (len(payload) + 2))
PY
)" || fail "the public detail does not match the published payload"
printf '%s\n' "$READBACK"
pass "read-back: the public detail matches the normalized payload, freshness unobserved"

# --------------------------------------------------------------------- F14 --
say "F14 (a colliding source observation must not win)"
F14="$(API="$API" JOB_ID="$JOB_ID" EXPECTED_JSON="$EXPECTED_JSON" TOKEN="$INTERNAL_TOKEN" \
    DETAIL_BEFORE="$DETAIL_BEFORE" WORK="$WORK" python3 - <<'PY'
import json, os, urllib.request
api, token = os.environ["API"], os.environ["TOKEN"]
with open(os.environ["EXPECTED_JSON"], encoding="utf-8") as handle:
    payload = json.load(handle)
job_id = int(os.environ["JOB_ID"])
source_url = payload["source_url"]

observation = {
    "source": "greenhouse",
    "source_scope": "capture-acceptance-collision",
    "complete": False,
    "observed_at": "2026-10-10 12:00:00",
    "jobs": [{
        "company": "观测到的公司（不得覆盖人工记录）",
        "title": "观测到的标题（不得覆盖人工记录）",
        "source": "greenhouse",
        "source_url": source_url,
        "description": "observed description must not win",
        "education_required": 1,
        "job_nature": 3,
        "deadline": "2026-11-15 10:30:00",
        "deadline_date_only": False,
    }],
}
request = urllib.request.Request(
    f"{api}/internal/jobs/source-sync",
    data=json.dumps(observation).encode("utf-8"),
    headers={"Content-Type": "application/json", "X-Internal-Token": token},
    method="POST",
)
with urllib.request.urlopen(request, timeout=10) as response:
    sync = json.load(response)
if sync.get("skipped_manual") != 1:
    raise SystemExit(f"the observation was not skipped for the manual row: {sync}")
if sync.get("skipped_manual_urls") != [source_url]:
    raise SystemExit(f"the skip is not diagnosable by url: {sync.get('skipped_manual_urls')}")
print("  POST /internal/jobs/source-sync -> skipped_manual=1 skipped_manual_urls=[<url>]")

with urllib.request.urlopen(f"{api}/api/v1/jobs/{job_id}", timeout=10) as response:
    after = json.load(response)["data"]
with open(os.environ["DETAIL_BEFORE"], encoding="utf-8") as handle:
    before = json.load(handle)
if json.dumps(before, sort_keys=True) != json.dumps(after, sort_keys=True):
    changed = sorted(k for k in set(before) | set(after) if before.get(k) != after.get(k))
    raise SystemExit(f"F14 violated: the manual record changed: {changed}")
print("  detail byte-identical before and after the colliding observation")
if after.get("source") != "manual":
    raise SystemExit(f"source changed to {after.get('source')!r}")
print("  source is still manual")
PY
)" || fail "the F14 manual-record priority check failed"
printf '%s\n' "$F14"
pass "F14: a colliding source observation is skipped and the manual record is unchanged"

# --------------------------------------------------------------- evidence ---
say "section 19.5 evidence (sanitized)"
CAPTURE_DIGEST="$(python3 -c 'import hashlib,sys;print(hashlib.sha256(open(sys.argv[1],"rb").read()).hexdigest())' "$CAPTURE")"
CAPTURE_BYTES="$(wc -c < "$CAPTURE" | tr -d ' ')"
SEARCH_ROWS="$(SEARCH_JSON="$SEARCH_JSON" python3 -c 'import json,os;print(json.load(open(os.environ["SEARCH_JSON"]))["rows"])')"
PUBLISH_TOTALS="$(PUBLISH1_JSON="$PUBLISH1_JSON" python3 -c 'import json,os;d=json.load(open(os.environ["PUBLISH1_JSON"]));print("created=%s updated=%s total=%s" % (d["created"], d["updated"], d["total"]))')"
GIT_REV="$(git rev-parse --short HEAD 2>/dev/null || echo unknown)"

EVIDENCE_TMP="$WORK/section-19.5.txt"
{
    printf '# ExecPlan 0014 section 19.5 evidence - generated by tools/dev/capture-acceptance.sh\n'
    printf '# date: %s   commit: %s   isolated project: %s   image: %s\n' \
        "$(date -Iseconds)" "$GIT_REV" "$PROJECT" "$IMAGE"
    printf '站点类别（不写具体平台）: manual capture file（host 已脱敏）   判定日期: %s\n' "$(date +%F)"
    printf '书签点击结果（复制到剪贴板/回退 prompt）: <owner 填写>\n'
    printf '脱敏后的 capture JSON: { "capture_version": 1, "sha256": "%s", "bytes": %s }\n' \
        "$CAPTURE_DIGEST" "$CAPTURE_BYTES"
    printf '经 publish-capture 发布的结果行: %s\n' "$PUBLISH_TOTALS"
    printf '公共搜索可见性（GET /api/v1/jobs?keyword=…）: 可见（matched=1 rows=%s keyword=<redacted>）\n' "$SEARCH_ROWS"
    printf 'robots/登录墙判定: 未绕过登录、未提交表单（人工确认）: <owner 填写>\n'
    printf '断言: %s 项通过（publish/dry-run/search/read-back/F14）+ 证据完整性检查通过，token 未出现在任何输出中\n' "$legs_passed"
} > "$EVIDENCE_TMP"

# Credential guard: the generated token must not appear in anything we keep.
if grep -rqF "$INTERNAL_TOKEN" "$WORK" 2>/dev/null; then
    fail "the internal token was found in the run artifacts"
fi
pass "evidence: section 19.5 block written without the internal token"

# Printed after the guard so the assertion count in the block counts the
# evidence check itself.
tee "$EVIDENCE" < "$EVIDENCE_TMP"
[ -z "$KEEP_EVIDENCE" ] || cp "$EVIDENCE" "$KEEP_EVIDENCE"

# ---------------------------------------------------------------- summary ---
say "summary"
DEV_CONTAINERS_AFTER="$(docker ps --format '{{.Names}}' | grep -v "^${PROJECT}-" | sort | tr '\n' ' ')"
if [ "$DEV_CONTAINERS_BEFORE" != "$DEV_CONTAINERS_AFTER" ]; then
    note "docker ps changed during the run (was: $DEV_CONTAINERS_BEFORE / now: $DEV_CONTAINERS_AFTER)"
else
    echo "docker ps outside this project is unchanged: $DEV_CONTAINERS_AFTER"
fi
echo "legs passed: $legs_passed"
echo "the human still performs the actual bookmarklet click and the login-wall judgement"
echo "CAPTURE_ACCEPTANCE_OK"
