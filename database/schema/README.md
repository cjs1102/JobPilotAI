# Database Schema

SQL files in this directory are the canonical MySQL initialization scripts.

Docker mounts this directory at `/docker-entrypoint-initdb.d`. MySQL executes
the files in lexical order only when the data volume is initialized for the
first time.

The files are derived from `docs/03-database-design.md` and use
`CREATE TABLE IF NOT EXISTS` so they can also be applied to an empty database
manually without replacing existing tables.

Files, in execution order, and the tables each one creates:

| File | Tables |
|---|---|
| `01_user.sql` | `users`, `profiles`, `skills` |
| `02_resume.sql` | `resumes`, `resume_versions` |
| `03_job.sql` | `jobs`, `job_requirements`, `job_review_items` |
| `04_application.sql` | `applications` |
| `05_ai.sql` | `ai_tasks`, `interview_records` |
| `06_notification.sql` | `notifications` |
| `07_resume_artifact.sql` | `resume_artifacts` |

A complete database therefore holds **13** tables, and
`SchemaTest.CreatesAllExpectedTables` (`backend/tests/test_schema.cpp`) asserts
exactly that set.

## Creating a database from scratch

Two supported paths:

1. **Containerised — the documented local flow.** `docker compose up -d` starts
   MySQL with this directory mounted at `/docker-entrypoint-initdb.d`, so a
   first-time data volume is initialised automatically by the server itself.
   The scripts run **only when that volume is empty**. Re-initialising means
   removing the volume (`docker compose down -v`), which destroys everything in
   it — never run that against a database whose contents matter.
2. **Manual, against an existing MySQL.**

   ```bash
   for file in database/schema/*.sql; do
     mysql --host=127.0.0.1 --user=root --password \
       --database=jobpilot < "$file"
   done
   ```

   `127.0.0.1` is deliberate: with `localhost` the MySQL client uses a unix
   socket, which does not reach a containerised server.

## Upgrading a database that already exists

`CREATE TABLE IF NOT EXISTS` never adds a column to an existing table, so a
schema change that lands after a database was created is applied as a numbered,
additive `ALTER` block, by hand:

| Stage | Contract | Block |
|---|---|---|
| 0012 | freshness columns and indexes | `docs/03-database-design.md` §7.3 |
| 0013 | AI task tables | `docs/03-database-design.md` §8 |
| 0014 | China campus source columns | `docs/03-database-design.md` §9 |
| 0015 | profile candidate columns and `resume_artifacts` | `docs/03-database-design.md` §10 |
| 0020 | `jobs.apply_url` (nullable, no index) | `database/upgrades/0020_jobs_apply_url.up.sql` (contract: `docs/03` §9.4) |

Rules:

- Apply the blocks in stage order, reading each one before running it. There is
  deliberately **no migration runner and no schema-version table** (0016 §11
  decision 7): an upgrade is a human reading an additive statement.
- **Re-running a block reports `Duplicate column name`. That is expected.** The
  older blocks (0012-0015) are additive and not idempotent, and that error is
  MySQL telling you the change is already applied; skip it and continue.
- **0020 is the exception:** `database/upgrades/0020_jobs_apply_url.up.sql` and
  `.down.sql` check `information_schema` first, so they are safe to re-run
  (they print a note instead of failing). The CI step
  `tools/checks/test_schema_upgrade.sh` proves forward, backward, re-forward and
  idempotent runs on a scratch database.
- A new table added with `CREATE TABLE IF NOT EXISTS` (for example
  `07_resume_artifact.sql` in 0015) is safe to re-run.
- Do not edit an already-applied block: append a new stage's block instead, so
  the file stays a history of additive contracts.

## Verifying a database

```bash
mysql --host=127.0.0.1 --user=root --password --database=jobpilot -e "
  SELECT COUNT(*) AS table_count
  FROM information_schema.tables
  WHERE table_schema = 'jobpilot' AND table_type = 'BASE TABLE';"
```

The authoritative check is the schema test binary, which asserts the table set,
the engine and collation, the required columns, and the migration state of a
pre-0015 database:

```bash
cd service && MYSQL_PASSWORD=... ctest --test-dir build/presets/release -R SchemaTest
```

## Isolation rule

Anything that resets, seeds or stress-writes a database — a demo run, a gate
script, an upgrade rehearsal — uses its **own** MySQL instance or its own
database name, never the developer's `jobpilot` database and never the review
scratch database. The rule exists because `docker compose down -v` and the
backfill `apply` path are destructive by design; 0016 P2 verifies the
developer's row counts before and after such a run.
