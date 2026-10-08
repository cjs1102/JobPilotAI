SET NAMES utf8mb4;

CREATE TABLE IF NOT EXISTS ai_tasks (
    id          BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY COMMENT '任务ID',
    user_id     BIGINT UNSIGNED NOT NULL COMMENT '用户',
    task_type   VARCHAR(32) NOT NULL COMMENT 'job_match/resume_optimize/interview_gen',
    job_id      BIGINT UNSIGNED DEFAULT NULL COMMENT '关联岗位',
    resume_id   BIGINT UNSIGNED DEFAULT NULL COMMENT '关联简历',
    status      TINYINT NOT NULL DEFAULT 0 COMMENT '0pending 1processing 2success 3failed',
    result      JSON DEFAULT NULL COMMENT '结果JSON',
    error_msg   VARCHAR(512) DEFAULT NULL COMMENT '失败原因',
    retry_count INT NOT NULL DEFAULT 0 COMMENT '重试次数',
    locked_by        VARCHAR(64) DEFAULT NULL COMMENT '当前worker标识(0013)',
    lease_expire_at  DATETIME    DEFAULT NULL COMMENT '租约到期时间(0013)',
    rule_result      JSON        DEFAULT NULL COMMENT '确定性规则基线结果(0013)',
    create_time DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    update_time DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    KEY idx_user_status (user_id, status),
    KEY idx_status (status),
    KEY idx_type_status (task_type, status),
    KEY idx_status_lease (status, lease_expire_at)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci COMMENT='AI异步任务';

CREATE TABLE IF NOT EXISTS interview_records (
    id               BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY COMMENT '记录ID',
    user_id          BIGINT UNSIGNED NOT NULL COMMENT '用户',
    job_id           BIGINT UNSIGNED DEFAULT NULL COMMENT '关联岗位(可空)',
    question         TEXT NOT NULL COMMENT '面试问题',
    user_answer      TEXT DEFAULT NULL COMMENT '用户回答',
    reference_answer TEXT DEFAULT NULL COMMENT '参考答案',
    skill_tag        VARCHAR(64) DEFAULT NULL COMMENT '技能标签',
    create_time      DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    KEY idx_user (user_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci COMMENT='AI面试记录';
