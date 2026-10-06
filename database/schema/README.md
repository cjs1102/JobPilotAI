# Database Schema

SQL files in this directory are the canonical MySQL initialization scripts.

Docker mounts this directory at `/docker-entrypoint-initdb.d`. MySQL executes
the files in lexical order only when the data volume is initialized for the
first time.

The files are derived from `docs/03-database-design.md` and use
`CREATE TABLE IF NOT EXISTS` so they can also be applied to an empty database
manually without replacing existing tables.

Manual application:

```bash
for file in database/schema/*.sql; do
  mysql --host=127.0.0.1 --user=root --password \
    --database=jobpilot < "$file"
done
```
