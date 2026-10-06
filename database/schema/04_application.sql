SET NAMES utf8mb4;

CREATE TABLE IF NOT EXISTS applications (
    id                 BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY COMMENT '投递ID',
    user_id            BIGINT UNSIGNED NOT NULL COMMENT '用户',
    job_id             BIGINT UNSIGNED NOT NULL COMMENT '岗位',
    resume_version_id  BIGINT UNSIGNED DEFAULT NULL COMMENT '使用的简历版本',
    status             TINYINT      NOT NULL DEFAULT 0 COMMENT '0收藏 1待投递 2已投递 3笔试 4一面 5二面 6Offer 7拒绝',
    apply_time         DATETIME     DEFAULT NULL COMMENT '投递时间',
    remark             TEXT         DEFAULT NULL COMMENT '备注',
    create_time        DATETIME     NOT NULL DEFAULT CURRENT_TIMESTAMP,
    update_time        DATETIME     NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    UNIQUE KEY uk_user_job (user_id, job_id),
    KEY idx_user_status (user_id, status)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci COMMENT='投递记录';
