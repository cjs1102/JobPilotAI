SET NAMES utf8mb4;

CREATE TABLE IF NOT EXISTS jobs (
    id               BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY COMMENT '岗位ID',
    company          VARCHAR(128) NOT NULL COMMENT '公司',
    title            VARCHAR(128) NOT NULL COMMENT '职位',
    location         VARCHAR(64)  DEFAULT NULL COMMENT '地点',
    salary_min       INT UNSIGNED DEFAULT NULL COMMENT '月薪下限(千元)',
    salary_max       INT UNSIGNED DEFAULT NULL COMMENT '月薪上限(千元)',
    salary_currency  VARCHAR(8)   NOT NULL DEFAULT 'CNY' COMMENT '币种',
    description      TEXT         DEFAULT NULL COMMENT '岗位描述',
    source           VARCHAR(32)  NOT NULL DEFAULT 'manual' COMMENT 'manual/import/crawler',
    source_url       VARCHAR(255) DEFAULT NULL COMMENT '来源链接(去重键)',
    status           TINYINT      NOT NULL DEFAULT 1 COMMENT '1有效 0下线',
    create_time      DATETIME     NOT NULL DEFAULT CURRENT_TIMESTAMP,
    update_time      DATETIME     NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    UNIQUE KEY uk_source_url (source_url),
    KEY idx_company (company),
    KEY idx_title (title),
    KEY idx_location (location),
    KEY idx_status (status)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci COMMENT='岗位';

CREATE TABLE IF NOT EXISTS job_requirements (
    id          BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY COMMENT '主键',
    job_id      BIGINT UNSIGNED NOT NULL COMMENT '岗位ID',
    skill       VARCHAR(64) NOT NULL COMMENT '技能名',
    level       TINYINT     NOT NULL DEFAULT 1 COMMENT '要求等级',
    create_time DATETIME    NOT NULL DEFAULT CURRENT_TIMESTAMP,
    UNIQUE KEY uk_job_skill (job_id, skill),
    KEY idx_job (job_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci COMMENT='岗位技能要求';
