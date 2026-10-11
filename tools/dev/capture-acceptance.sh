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
#     [--keep-evidence <file>] [--image <image>] [--expect-review]
#
#   --capture        required; a capture_version 1 artifact (the synthetic
#                    sample is docs/examples/capture-acceptance-sample.json)
#   --keep-evidence  also copy the section 19.5 evidence block to this path
#   --image          backend image (default $JOBPILOT_BACKEND_IMAGE or
#                    jobpilot-backend:local; the development host has
#                    jobpilot-backend:verify because Docker Hub is unreachable)
#   --expect-review  require the 0015A pre-flight to route this capture to the
#                    review queue and verify the REVIEW path instead of the clean
#                    one. Without the flag the script adapts to whatever the
#                    pre-flight reports and says which path it took; a capture
#                    that is routed for review is never forced public.
#
# Two paths, one contract. A clean capture is expected to become publicly
# visible; a `needs_review` capture is expected to stay hidden from the public
# list/search and to appear in the internal review queue with its finding code.
# Both paths assert created/updated/total, the row in the database, and the F14
# rule that a colliding source observation cannot change a manual record.
#
# Isolation: this script never reuses a running stack. It owns compose project
# `jobpilot-capture`, host ports 18081 / 13307 / 16380 and its own volumes, and
# it refuses to run against the development or demo-gate coordinates. The
# internal token is generated here, lives only in this process environment, and
# is never printed nor written to the evidence file.
#
# The capture JSON is read where it lies and never copied into the repository.
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
EXPECT="auto"
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
        --expect-review) EXPECT="review"; shift ;;
        --expect-clean) EXPECT="clean"; shift ;;
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
MODE_FILE="$WORK/mode.txt"
DRY_SUMMARY="$(DRY_RUN_JSON="$DRY_RUN_JSON" EXPECTED_JSON="$EXPECTED_JSON" MODE_FILE="$MODE_FILE" \
    CI_EXPECT="$EXPECT" python3 - <<'PY'
import json, os
with open(os.environ["DRY_RUN_JSON"], encoding="utf-8") as handle:
    document = json.load(handle)
if document.get("ok") is not True or document.get("dry_run") is not True:
    raise SystemExit(f"dry run did not succeed: {json.dumps(document, ensure_ascii=False)[:400]}")
jobs = document.get("jobs") or []
if len(jobs) != 1:
    raise SystemExit(f"this scaffold accepts exactly one capture per run, got {len(jobs)}")

counters = document.get("counters") or {}
clean = counters.get("clean") or 0
needs_review = counters.get("needs_review") or 0
rejected = counters.get("rejected") or 0
if rejected:
    raise SystemExit(f"the 0015A pre-flight rejects this capture: {json.dumps(counters)}")

mode = "review" if needs_review else "clean"
expect = os.environ["CI_EXPECT"]
if expect != "auto" and expect != mode:
    raise SystemExit(
        f"the pre-flight reports {mode!r} but --expect-{expect} was requested: {json.dumps(counters)}")

payload = jobs[0]
if payload.get("source") != "manual":
    raise SystemExit(f"payload source is {payload.get('source')!r}, not 'manual'")
with open(os.environ["EXPECTED_JSON"], "w", encoding="utf-8") as handle:
    json.dump(payload, handle, ensure_ascii=False, indent=2)
with open(os.environ["MODE_FILE"], "w", encoding="utf-8") as handle:
    handle.write(mode)

# Stable finding codes only: no description text reaches the log or the evidence.
findings = []
for item in (document.get("items") or []):
    for finding in (item.get("findings") or []):
        findings.append("%s/%s:%s" % (
            finding.get("code"), finding.get("field"), finding.get("outcome")))
print("mode=%s clean=%s needs_review=%s rejected=%s findings=%s payload_fields=%s" % (
    mode, clean, needs_review, rejected, ",".join(findings) or "-",
    ",".join(sorted(payload))))
PY
)" || fail "dry run did not pass its gate"
MODE="$(cat "$MODE_FILE")"
echo "dry run: $DRY_SUMMARY"
if [ "$MODE" = "review" ]; then
    pass "publish (dry run): the 0015A pre-flight routes this capture to the review queue (hidden, never forced public)"
else
    pass "publish (dry run): capture validates and normalizes to a clean manual payload"
fi

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
    JOB_JSON="$JOB_JSON" CI_MODE="$MODE" python3 - <<'PY'
import json, os, urllib.parse, urllib.request
with open(os.environ["EXPECTED_JSON"], encoding="utf-8") as handle:
    payload = json.load(handle)
api = os.environ["API"]
mode = os.environ["CI_MODE"]
title = payload["title"]
candidates = [title, payload.get("company") or "", title[:12]]

probes = []
matching = []
for keyword in candidates:
    if not keyword:
        continue
    url = f"{api}/api/v1/jobs?keyword={urllib.parse.quote(keyword)}"
    with urllib.request.urlopen(url, timeout=10) as response:
        body = json.load(response)
    rows = body["data"]["list"]
    current = [row for row in rows if row.get("title") == title]
    probes.append({"keyword_len": len(keyword), "rows": len(rows), "matched": len(current)})
    if current:
        matching = current
        if mode == "clean":
            break

with open(os.environ["SEARCH_JSON"], "w", encoding="utf-8") as handle:
    json.dump({"mode": mode, "probes": probes}, handle, ensure_ascii=False)

total_matched = sum(probe["matched"] for probe in probes)

if mode == "review":
    # 0015A: a needs_review row must stay hidden from the public list/search.
    if total_matched != 0:
        raise SystemExit(f"a needs_review row is visible in public search: {probes}")
    print("public search probes=%d matched=0 (hidden, as required for needs_review)" % len(probes))
else:
    if total_matched != 1:
        raise SystemExit(f"public search matched {total_matched} rows with the exact title: {probes}")
    row = matching[0]
    if not isinstance(row.get("id"), int) or row["id"] <= 0:
        raise SystemExit(f"the search row carries no usable id: {row.get('id')!r}")
    with open(os.environ["JOB_JSON"], "w", encoding="utf-8") as handle:
        json.dump(row, handle, ensure_ascii=False)
    print("probes=%d rows=%d matched=1 id=%d" % (
        len(probes), probes[-1]["rows"], row["id"]))
PY
)" || fail "public search visibility did not match the expected path"
echo "GET /api/v1/jobs?keyword=<redacted> -> $SEARCH_SUMMARY"
if [ "$MODE" = "review" ]; then
    pass "search: the needs_review row is NOT visible through the public query interface"
else
    pass "search: the manual job is visible through the public query interface"
fi

# ------------------------------------------------- review queue (0015A) -----
if [ "$MODE" = "review" ]; then
    say "review queue visibility (internal surface)"
    QUEUE_JSON="$WORK/review-queue.json"
    QUEUE_SUMMARY="$(API="$API" TOKEN="$INTERNAL_TOKEN" EXPECTED_JSON="$EXPECTED_JSON" \
        QUEUE_JSON="$QUEUE_JSON" JOB_JSON="$JOB_JSON" python3 - <<'PY'
import json, os, urllib.request
with open(os.environ["EXPECTED_JSON"], encoding="utf-8") as handle:
    payload = json.load(handle)
url = (f"{os.environ['API']}/internal/jobs/review-queue"
       "?status=needs_review&page=1&page_size=50")
request = urllib.request.Request(url, headers={"X-Internal-Token": os.environ["TOKEN"]})
with urllib.request.urlopen(request, timeout=10) as response:
    body = json.load(response)

items = body.get("items") or []
matching = [item for item in items if item.get("source_url") == payload.get("source_url")]
if len(matching) != 1:
    raise SystemExit(
        f"the queued row is not on the review surface (items={len(items)}, matched={len(matching)})")
item = matching[0]
if item.get("review_status") != 1:
    raise SystemExit(f"the queued row has review_status={item.get('review_status')!r}, expected 1")
if item.get("resolution") not in (None, "open"):
    raise SystemExit(f"the queued row already carries a resolution: {item.get('resolution')!r}")
if not item.get("reason_code"):
    raise SystemExit("the queued row carries no stable reason_code")

# Only the review metadata is kept; no title, company or description text.
with open(os.environ["QUEUE_JSON"], "w", encoding="utf-8") as handle:
    json.dump({"total": body.get("total"),
               "item": {key: item.get(key) for key in (
                   "job_id", "review_status", "reason_code", "outcome",
                   "resolution", "source", "created_at")}},
              handle, ensure_ascii=False)
with open(os.environ["JOB_JSON"], "w", encoding="utf-8") as handle:
    json.dump({"id": item.get("job_id")}, handle, ensure_ascii=False)
print("queue_total=%s job_id=%s review_status=%s reason_code=%s outcome=%s resolution=%s" % (
    body.get("total"), item.get("job_id"), item.get("review_status"),
    item.get("reason_code"), item.get("outcome"), item.get("resolution")))
PY
)" || fail "the queued row is not visible on the internal review surface"
    echo "GET /internal/jobs/review-queue?status=needs_review -> $QUEUE_SUMMARY"
    pass "review queue: the hidden row is queued with its stable finding code (no decision taken)"

    say "database record (isolated stack)"
    DB_SUMMARY="$(PROJECT="$PROJECT" EXPECTED_JSON="$EXPECTED_JSON" python3 - <<'PY'
import hashlib, json, os, subprocess
with open(os.environ["EXPECTED_JSON"], encoding="utf-8") as handle:
    payload = json.load(handle)
source_url = payload["source_url"]
if "'" in source_url or "\\" in source_url:
    raise SystemExit("refusing to query with a quote-bearing source_url")
query = ("SELECT COUNT(*), COALESCE(MAX(review_status), -1), COALESCE(MAX(source), ''), "
         "COALESCE(MAX(description) IS NOT NULL, 0) "
         f"FROM jobs WHERE source_url = '{source_url}'")
out = subprocess.run(
    ["docker", "exec", f"{os.environ['PROJECT']}-mysql",
     "mysql", "-uroot", "-proot", "-N", "-e", f"USE jobpilot; {query}"],
    capture_output=True, text=True, check=False)
if out.returncode != 0:
    raise SystemExit(f"the isolated MySQL query failed: {out.stderr.strip()[:200]}")
fields = out.stdout.strip().split("\t")
if len(fields) != 4:
    raise SystemExit(f"unexpected query output shape: {out.stdout.strip()[:120]!r}")
count, review_status, source, has_description = fields
if count != "1" or review_status != "1" or source != "manual" or has_description != "1":
    raise SystemExit(
        f"row check failed: count={count} review_status={review_status} source={source!r} "
        f"has_description={has_description}")
digest = hashlib.sha256(source_url.encode()).hexdigest()[:12]
print(f"rows=1 review_status=1 source=manual description_present=yes "
      f"source_url_sha256={digest}")
PY
)" || fail "the database row does not match the expected review state"
    echo "isolated MySQL: $DB_SUMMARY"
    pass "database: exactly one manual row, stored hidden (review_status=1)"
fi

# ------------------------------------------------------- detail read-back ---
say "detail read-back"
DETAIL_BEFORE="$WORK/detail-before.json"
JOB_ID="$(JOB_JSON="$JOB_JSON" python3 -c 'import json,os;print(json.load(open(os.environ["JOB_JSON"]))["id"])')"
READBACK="$(API="$API" JOB_ID="$JOB_ID" EXPECTED_JSON="$EXPECTED_JSON" DETAIL_BEFORE="$DETAIL_BEFORE" \
    CI_MODE="$MODE" SKIP_FILE="$WORK/detail-status.txt" python3 - <<'PY'
import json, os, urllib.error, urllib.request
api = os.environ["API"]
mode = os.environ["CI_MODE"]
with open(os.environ["EXPECTED_JSON"], encoding="utf-8") as handle:
    payload = json.load(handle)
job_id = int(os.environ["JOB_ID"])

try:
    with urllib.request.urlopen(f"{api}/api/v1/jobs/{job_id}", timeout=10) as response:
        status = getattr(response, "status", 200)
        document = json.load(response)
except urllib.error.HTTPError as error:
    status = error.code
    document = None

with open(os.environ["SKIP_FILE"], "w", encoding="utf-8") as handle:
    handle.write(str(status))

if status != 200:
    if mode == "review":
        # 0015A review gate: a row that is still queued is not part of the public
        # surface, and the by-id detail must not confirm that it exists. Any
        # non-200 answer is accepted here, but the status stays in the evidence.
        print(f"  a queued row is not readable by direct id (HTTP {status}), "
              "exactly like a row that does not exist")
        raise SystemExit(0)
    raise SystemExit(f"the public detail returned HTTP {status}")

if mode == "review":
    raise SystemExit(
        "the public detail returned HTTP 200 for a row that is still queued "
        "for review: the review gate is open on the by-id path")

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
DETAIL_STATUS="$(cat "$WORK/detail-status.txt" 2>/dev/null || echo unknown)"
if [ "$MODE" = "review" ] && [ "$DETAIL_STATUS" != "200" ]; then
    pass "read-back: the queued row is invisible by id (HTTP $DETAIL_STATUS), so the public detail cannot confirm it exists"
else
    pass "read-back: the stored row matches the normalized payload, freshness unobserved"
fi

# --------------------------------------------------------------------- F14 --
if [ -f "$DETAIL_BEFORE" ]; then
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
else
    say "F14 (database comparison; the by-id detail was not readable)"
    F14="$(PROJECT="$PROJECT" API="$API" TOKEN="$INTERNAL_TOKEN" EXPECTED_JSON="$EXPECTED_JSON" \
        python3 - <<'PY'
import hashlib, json, os, subprocess, urllib.request
with open(os.environ["EXPECTED_JSON"], encoding="utf-8") as handle:
    payload = json.load(handle)
source_url = payload["source_url"]
if "'" in source_url or "\\" in source_url:
    raise SystemExit("refusing to query with a quote-bearing source_url")

def digest():
    query = ("SELECT COALESCE(company,''), COALESCE(title,''), COALESCE(description,''), "
             "COALESCE(review_status,-1), COALESCE(source,'') FROM jobs "
             f"WHERE source_url = '{source_url}'")
    out = subprocess.run(
        ["docker", "exec", f"{os.environ['PROJECT']}-mysql",
         "mysql", "-uroot", "-proot", "--default-character-set=utf8mb4",
         "-N", "-e", f"USE jobpilot; {query}"],
        capture_output=True, check=False)
    if out.returncode != 0 or not out.stdout.strip():
        raise SystemExit(f"the F14 digest query failed: {out.stderr.decode('utf-8', 'replace')[:200]}")
    # Hash the bytes the client returned; the row legitimately contains non-ASCII
    # text, and both digests are taken the same way, so decoding is unnecessary
    # and must not be attempted (an earlier version failed on a GBK byte).
    return hashlib.sha256(out.stdout.strip()).hexdigest()

before = digest()
observation = {
    "source": "greenhouse",
    "source_scope": "capture-acceptance-collision",
    "complete": False,
    "observed_at": "2026-10-10 12:00:00",
    "jobs": [{
        "company": "observed company must not win",
        "title": "observed title must not win",
        "source": "greenhouse",
        "source_url": source_url,
        "description": "observed description must not win",
    }],
}
request = urllib.request.Request(
    f"{os.environ['API']}/internal/jobs/source-sync",
    data=json.dumps(observation).encode("utf-8"),
    headers={"Content-Type": "application/json", "X-Internal-Token": os.environ["TOKEN"]},
    method="POST")
with urllib.request.urlopen(request, timeout=10) as response:
    sync = json.load(response)
if sync.get("skipped_manual") != 1:
    raise SystemExit(f"the observation was not skipped for the manual row: {sync}")
after = digest()
if before != after:
    raise SystemExit("F14 violated: the manual row changed (database digest differs)")
print("  source-sync skipped_manual=1; the row's database digest is unchanged")
PY
)" || fail "the F14 database comparison failed"
    printf '%s\n' "$F14"
    pass "F14: a colliding observation is skipped and the hidden manual row is unchanged (database digest)"
fi

# --------------------------------------------------------------- evidence ---
say "section 19.5 evidence (sanitized)"
CAPTURE_DIGEST="$(python3 -c 'import hashlib,sys;print(hashlib.sha256(open(sys.argv[1],"rb").read()).hexdigest())' "$CAPTURE")"
CAPTURE_BYTES="$(wc -c < "$CAPTURE" | tr -d ' ')"
SEARCH_ROWS="$(SEARCH_JSON="$SEARCH_JSON" CI_MODE="$MODE" python3 -c '
import json, os
data = json.load(open(os.environ["SEARCH_JSON"]))
probes = data.get("probes") or []
matched = sum(probe["matched"] for probe in probes)
if os.environ["CI_MODE"] == "review":
    print("不可见（matched=0 probes=%d keyword=<redacted>）" % len(probes))
else:
    print("可见（matched=%d keyword=<redacted>）" % matched)
')"
REVIEW_LINE=""
if [ "$MODE" = "review" ]; then
    REVIEW_LINE="$(QUEUE_JSON="$QUEUE_JSON" DETAIL_STATUS_FILE="$WORK/detail-status.txt" python3 -c '
import json, os
data = json.load(open(os.environ["QUEUE_JSON"]))
item = data.get("item") or {}
status = open(os.environ["DETAIL_STATUS_FILE"]).read().strip()
print("审核队列: review_status=%s reason_code=%s outcome=%s resolution=%s queue_total=%s；公共列表/搜索不可见；按 id 详情也不可见（HTTP %s）" % (
    item.get("review_status"), item.get("reason_code"), item.get("outcome"),
    item.get("resolution"), data.get("total"), status))
')"
fi
PUBLISH_TOTALS="$(PUBLISH1_JSON="$PUBLISH1_JSON" python3 -c 'import json,os;d=json.load(open(os.environ["PUBLISH1_JSON"]));print("created=%s updated=%s total=%s" % (d["created"], d["updated"], d["total"]))')"
GIT_REV="$(git rev-parse --short HEAD 2>/dev/null || echo unknown)"

EVIDENCE_TMP="$WORK/section-19.5.txt"
{
    printf '# ExecPlan 0014 section 19.5 evidence - generated by tools/dev/capture-acceptance.sh\n'
    printf '# date: %s   commit: %s   isolated project: %s   image: %s   path: %s\n' \
        "$(date -Iseconds)" "$GIT_REV" "$PROJECT" "$IMAGE" "$MODE"
    printf '站点类别（不写具体平台）: manual capture file（host 已脱敏）   判定日期: %s\n' "$(date +%F)"
    printf '书签点击结果（复制到剪贴板/回退 prompt）: <owner 填写>\n'
    printf '脱敏后的 capture JSON: { "capture_version": 1, "sha256": "%s", "bytes": %s }\n' \
        "$CAPTURE_DIGEST" "$CAPTURE_BYTES"
    printf '经 publish-capture 发布的结果行: %s\n' "$PUBLISH_TOTALS"
    printf '公共搜索可见性（GET /api/v1/jobs?keyword=…）: %s\n' "$SEARCH_ROWS"
    if [ -n "$REVIEW_LINE" ]; then
        printf '%s\n' "$REVIEW_LINE"
        printf '数据质量问题（稳定 finding code；需人工复核后再决定，不绕过审核）: %s\n' \
            "$(DRY_RUN_JSON="$DRY_RUN_JSON" python3 -c '
import json, os
document = json.load(open(os.environ["DRY_RUN_JSON"]))
codes = sorted({finding.get("code")
                for item in (document.get("items") or [])
                for finding in (item.get("findings") or [])
                if finding.get("code")})
print(",".join(codes) or "-")
')"
    fi
    printf 'robots/登录墙判定: 未绕过登录、未提交表单（人工确认）: <owner 填写>\n'
    printf '断言: %s 项通过（dry-run/publish/search/read-back/F14）+ 证据完整性检查通过，token 未出现在任何输出中\n' "$legs_passed"
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
