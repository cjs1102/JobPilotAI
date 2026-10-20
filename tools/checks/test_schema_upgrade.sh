#!/usr/bin/env bash
# 0020: forward / backward / re-forward / idempotent test for the
# jobs.apply_url additive upgrade.
#
# It creates its own scratch database (never jobpilot, jobpilot_ci or
# jobpilot_review_scratch), loads the frozen pre-0020 jobs DDL, applies the
# upgrade pair, asserts the column shape and the absence of an index, and
# drops the scratch database on every exit path.
#
# Environment: MYSQL_HOST (default 127.0.0.1), MYSQL_PORT (3306),
# MYSQL_USER (root), MYSQL_PASSWORD or MYSQL_PWD (required). The password is
# exported as MYSQL_PWD so it never appears in argv.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT"

MYSQL_HOST="${MYSQL_HOST:-127.0.0.1}"
MYSQL_PORT="${MYSQL_PORT:-3306}"
MYSQL_USER="${MYSQL_USER:-root}"
if [ -z "${MYSQL_PASSWORD:-}" ] && [ -n "${MYSQL_PWD:-}" ]; then
    MYSQL_PASSWORD="$MYSQL_PWD"
fi

if [ -z "${MYSQL_PASSWORD:-}" ]; then
    echo "test_schema_upgrade: MYSQL_PASSWORD or MYSQL_PWD is required" >&2
    exit 2
fi

export MYSQL_PWD="$MYSQL_PASSWORD"

SCRATCH_DB="${JOBPILOT_UPGRADE_DB:-jobpilot_upgrade_test_$$}"

case "$SCRATCH_DB" in
    jobpilot|jobpilot_ci|jobpilot_review_scratch)
        echo "test_schema_upgrade: refusing database '$SCRATCH_DB'" >&2
        exit 2
        ;;
esac

if ! printf '%s' "$SCRATCH_DB" | grep -qE '^[A-Za-z0-9_]+$'; then
    echo "test_schema_upgrade: database name must be [A-Za-z0-9_]+" >&2
    exit 2
fi

mysql_cmd()
{
    mysql \
        --host="$MYSQL_HOST" \
        --port="$MYSQL_PORT" \
        --user="$MYSQL_USER" \
        --batch \
        --skip-column-names \
        "$@"
}

cleanup()
{
    mysql_cmd -e "DROP DATABASE IF EXISTS \`$SCRATCH_DB\`;" >/dev/null 2>&1 || true
}
trap cleanup EXIT

step()
{
    printf '\n=== %s ===\n' "$1"
}

fail()
{
    printf 'FAIL: %s\n' "$1" >&2
    exit 1
}

column_state()
{
    mysql_cmd -e "
        SELECT CONCAT(COLUMN_TYPE, '|', IS_NULLABLE, '|',
                      IFNULL(COLUMN_DEFAULT, '<null>'))
        FROM information_schema.columns
        WHERE table_schema = '$SCRATCH_DB'
          AND table_name = 'jobs'
          AND column_name = 'apply_url';"
}

assert_present()
{
    local state
    state="$(column_state)"
    [ "$state" = "varchar(512)|YES|<null>" ] ||
        fail "apply_url state is '$state', expected 'varchar(512)|YES|<null>'"
    printf 'PASS: apply_url is varchar(512), nullable, default NULL\n'
}

assert_absent()
{
    local count
    count="$(mysql_cmd -e "
        SELECT COUNT(*)
        FROM information_schema.columns
        WHERE table_schema = '$SCRATCH_DB'
          AND table_name = 'jobs'
          AND column_name = 'apply_url';")"
    [ "$count" = "0" ] || fail "apply_url is still present (count=$count)"
    printf 'PASS: apply_url is absent\n'
}

assert_no_index()
{
    local count
    count="$(mysql_cmd -e "
        SELECT COUNT(*)
        FROM information_schema.statistics
        WHERE table_schema = '$SCRATCH_DB'
          AND table_name = 'jobs'
          AND column_name = 'apply_url';")"
    [ "$count" = "0" ] || fail "apply_url unexpectedly has an index (count=$count)"
    printf 'PASS: apply_url has no index\n'
}

[ -f database/upgrades/testdata/pre_0020_jobs.sql ] ||
    fail "missing database/upgrades/testdata/pre_0020_jobs.sql"
[ -f database/upgrades/0020_jobs_apply_url.up.sql ] ||
    fail "missing 0020 up.sql"
[ -f database/upgrades/0020_jobs_apply_url.down.sql ] ||
    fail "missing 0020 down.sql"

step "scratch database $SCRATCH_DB"
mysql_cmd -e "CREATE DATABASE \`$SCRATCH_DB\` CHARACTER SET utf8mb4 COLLATE utf8mb4_general_ci;"
mysql_cmd --database="$SCRATCH_DB" < database/upgrades/testdata/pre_0020_jobs.sql
assert_absent

step "forward: 0020 up.sql"
mysql_cmd --database="$SCRATCH_DB" < database/upgrades/0020_jobs_apply_url.up.sql
assert_present
assert_no_index

step "backward: 0020 down.sql"
mysql_cmd --database="$SCRATCH_DB" < database/upgrades/0020_jobs_apply_url.down.sql
assert_absent

step "re-forward: 0020 up.sql"
mysql_cmd --database="$SCRATCH_DB" < database/upgrades/0020_jobs_apply_url.up.sql
assert_present

step "idempotent: 0020 up.sql again"
IDEMPOTENT_OUT="$(mysql_cmd --database="$SCRATCH_DB" < database/upgrades/0020_jobs_apply_url.up.sql)"
printf '%s\n' "$IDEMPOTENT_OUT"
printf '%s\n' "$IDEMPOTENT_OUT" | grep -q "apply_url already exists; skipped" ||
    fail "second up.sql did not report the idempotent skip"
assert_present

step "backward after idempotent up: 0020 down.sql"
mysql_cmd --database="$SCRATCH_DB" < database/upgrades/0020_jobs_apply_url.down.sql
assert_absent

step "idempotent: 0020 down.sql again"
DOWN_TWICE_OUT="$(mysql_cmd --database="$SCRATCH_DB" < database/upgrades/0020_jobs_apply_url.down.sql)"
printf '%s\n' "$DOWN_TWICE_OUT"
printf '%s\n' "$DOWN_TWICE_OUT" | grep -q "apply_url absent; skipped" ||
    fail "second down.sql did not report the idempotent skip"

step "re-forward after down idempotency: 0020 up.sql"
mysql_cmd --database="$SCRATCH_DB" < database/upgrades/0020_jobs_apply_url.up.sql
assert_present

step "done"
echo "SCHEMA_UPGRADE_OK"
