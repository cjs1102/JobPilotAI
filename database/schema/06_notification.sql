SET NAMES utf8mb4;

CREATE TABLE IF NOT EXISTS notifications (
    id          BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY COMMENT '通知ID',
    user_id     BIGINT UNSIGNED NOT NULL COMMENT '用户',
    title       VARCHAR(128) NOT NULL COMMENT '标题',
    content     TEXT DEFAULT NULL COMMENT '内容',
    status      TINYINT NOT NULL DEFAULT 0 COMMENT '0未读 1已读',
    create_time DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    KEY idx_user_status (user_id, status)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci COMMENT='通知';
