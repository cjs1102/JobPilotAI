SET NAMES utf8mb4;

-- 0015 phase D: tailored resume artifacts (the only new table in 0015).
-- See docs/03 section 10.6 for the contract and the invariants.
CREATE TABLE IF NOT EXISTS resume_artifacts (
    id                BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY COMMENT '产物ID',
    user_id           BIGINT UNSIGNED NOT NULL COMMENT '所属用户',
    job_id            BIGINT UNSIGNED DEFAULT NULL COMMENT '关联岗位(可空)',
    application_id    BIGINT UNSIGNED DEFAULT NULL COMMENT '关联投递(可空)',
    base_version_id   BIGINT UNSIGNED NOT NULL COMMENT '派生自哪条 resume_versions',
    artifact_type     VARCHAR(16)  NOT NULL COMMENT '本阶段只用 resume',
    language          VARCHAR(8)   NOT NULL DEFAULT 'zh',
    content           MEDIUMTEXT   DEFAULT NULL COMMENT 'ATS 纯文本草稿',
    file_path         VARCHAR(255) DEFAULT NULL COMMENT '可选渲染产物(本阶段不产生)',
    content_bytes     INT UNSIGNED DEFAULT NULL COMMENT 'content 的 UTF-8 字节数',
    generator         VARCHAR(32)  NOT NULL COMMENT 'rule_v1(本阶段唯一)',
    source_version    VARCHAR(32)  DEFAULT NULL COMMENT 'tailor_rule_v1',
    fact_check_status TINYINT      NOT NULL DEFAULT 0 COMMENT '0未检查 1通过 2失败',
    fact_check_report JSON         DEFAULT NULL COMMENT '机械事实门报告',
    confirmed_at      DATETIME     DEFAULT NULL COMMENT '人工确认时刻(NULL=草稿)',
    create_time       DATETIME     NOT NULL DEFAULT CURRENT_TIMESTAMP,
    update_time       DATETIME     NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    KEY idx_user_job (user_id, job_id),
    KEY idx_application (application_id),
    KEY idx_base_version (base_version_id),
    KEY idx_user_type_created (user_id, artifact_type, id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci COMMENT='简历定制产物(草稿/确认)';
