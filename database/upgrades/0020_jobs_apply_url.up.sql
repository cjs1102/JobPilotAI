SET NAMES utf8mb4;

-- 0020: add the nullable actual-application link. MySQL 8.0 has no
-- ADD COLUMN IF NOT EXISTS, so check information_schema first. The statement
-- is safe to re-run: the second run reports a note instead of failing.
SET @jp_apply_url_exists := (
    SELECT COUNT(*)
    FROM information_schema.columns
    WHERE table_schema = DATABASE()
      AND table_name = 'jobs'
      AND column_name = 'apply_url'
);

SET @jp_sql := IF(
    @jp_apply_url_exists = 0,
    'ALTER TABLE jobs ADD COLUMN apply_url VARCHAR(512) DEFAULT NULL COMMENT ''实际申请链接(0020; source_url 是原始公告页)''',
    'SELECT ''apply_url already exists; skipped'' AS upgrade_note'
);

PREPARE jp_stmt FROM @jp_sql;
EXECUTE jp_stmt;
DEALLOCATE PREPARE jp_stmt;
