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
    source           VARCHAR(32)  NOT NULL DEFAULT 'manual' COMMENT 'manual/import/crawler 或 allowlist 观测来源标识(0014)',
    source_url       VARCHAR(255) DEFAULT NULL COMMENT '来源链接(去重键)',
    apply_url        VARCHAR(512) DEFAULT NULL COMMENT '实际申请链接(0020; source_url 是原始公告页)',
    status           TINYINT      NOT NULL DEFAULT 1 COMMENT '1有效 0下线',
    source_scope     VARCHAR(128) DEFAULT NULL COMMENT '来源 board/scope 标识(0012)',
    external_id      VARCHAR(255) DEFAULT NULL COMMENT '来源侧岗位ID(0012)',
    published_at     DATETIME     DEFAULT NULL COMMENT '来源发布时间(0012)',
    last_seen_at     DATETIME     DEFAULT NULL COMMENT '最近一次完整观测到的时间(0012)',
    last_verified_at DATETIME     DEFAULT NULL COMMENT '最近一次存活校验时间(0012)',
    closed_at        DATETIME     DEFAULT NULL COMMENT '软关闭时间(0012)',
    missing_count    INT          NOT NULL DEFAULT 0 COMMENT '连续完整观测缺失次数(0012)',
    source_status    VARCHAR(32)  NOT NULL DEFAULT 'unknown' COMMENT 'unknown/ok/missing/closed(0012)',
    education_required   TINYINT   DEFAULT NULL COMMENT '最低学历:1不限/2大专/3本科/4硕士/5博士(0014)',
    experience_years_min SMALLINT  DEFAULT NULL COMMENT '最低经验年限(0014)',
    experience_years_max SMALLINT  DEFAULT NULL COMMENT '最高经验年限(0014)',
    job_nature           TINYINT   DEFAULT NULL COMMENT '岗位性质:1校招/2社招/3实习(0014)',
    deadline             DATETIME  DEFAULT NULL COMMENT '投递截止时间(UTC瞬时;格式约定见docs/03 §9.1,0014)',
    deadline_date_only   TINYINT   DEFAULT NULL COMMENT 'deadline是否仅来源日期:1是/0否/NULL未知(0014)',
    review_status        TINYINT   NOT NULL DEFAULT 0 COMMENT '0015A 质量状态:0 clean/1 needs_review/2 quarantined',
    reviewed_at          DATETIME  DEFAULT NULL COMMENT '0015A 最近一次复核时间',
    review_note          VARCHAR(255) DEFAULT NULL COMMENT '0015A 复核备注',
    create_time      DATETIME     NOT NULL DEFAULT CURRENT_TIMESTAMP,
    update_time      DATETIME     NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    UNIQUE KEY uk_source_url (source_url),
    KEY idx_company (company),
    KEY idx_title (title),
    KEY idx_location (location),
    KEY idx_status (status),
    KEY idx_source_scope_status (source, source_scope, status),
    KEY idx_review_status (review_status, id),
    KEY idx_deadline (deadline)
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

CREATE TABLE IF NOT EXISTS job_review_items (
    id          BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY COMMENT '主键',
    job_id      BIGINT UNSIGNED NOT NULL COMMENT '岗位ID',
    reason_code VARCHAR(64)  NOT NULL COMMENT '0015A 稳定机器码',
    reason_text VARCHAR(255) DEFAULT NULL COMMENT '0015A 人类可读原因',
    outcome     VARCHAR(24)  NOT NULL COMMENT 'rejected/needs_review/accepted_advisory',
    resolution  VARCHAR(16)  NOT NULL DEFAULT 'open' COMMENT 'open/approved/fixed/rejected',
    resolved_by VARCHAR(64)  DEFAULT NULL COMMENT '复核人标签(内部 token 审计用)',
    resolved_at DATETIME     DEFAULT NULL COMMENT '复核时间',
    create_time DATETIME     NOT NULL DEFAULT CURRENT_TIMESTAMP,
    KEY idx_job (job_id),
    KEY idx_reason (reason_code),
    KEY idx_resolution (resolution)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci COMMENT='0015A 岗位质量异常与复核记录';
