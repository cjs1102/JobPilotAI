SET NAMES utf8mb4;

-- 0020 down migration. TEST/SCRATCH USE ONLY: dropping the column loses every
-- stored apply_url value. Production rollback should first revert the code;
-- the old code ignores the extra column.
SET @jp_apply_url_exists := (
    SELECT COUNT(*)
    FROM information_schema.columns
    WHERE table_schema = DATABASE()
      AND table_name = 'jobs'
      AND column_name = 'apply_url'
);

SET @jp_sql := IF(
    @jp_apply_url_exists = 1,
    'ALTER TABLE jobs DROP COLUMN apply_url',
    'SELECT ''apply_url absent; skipped'' AS downgrade_note'
);

PREPARE jp_stmt FROM @jp_sql;
EXECUTE jp_stmt;
DEALLOCATE PREPARE jp_stmt;
