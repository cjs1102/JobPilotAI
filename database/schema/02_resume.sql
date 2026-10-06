SET NAMES utf8mb4;

CREATE TABLE IF NOT EXISTS resumes (
    id          BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY COMMENT '简历ID',
    user_id     BIGINT UNSIGNED NOT NULL COMMENT '所属用户',
    title       VARCHAR(128) NOT NULL COMMENT '简历名称(如C++后端版)',
    create_time DATETIME     NOT NULL DEFAULT CURRENT_TIMESTAMP,
    update_time DATETIME     NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    KEY idx_user (user_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci COMMENT='简历主表';

CREATE TABLE IF NOT EXISTS resume_versions (
    id          BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY COMMENT '版本ID',
    resume_id   BIGINT UNSIGNED NOT NULL COMMENT '简历ID',
    version     INT            NOT NULL COMMENT '版本号(自增)',
    file_path   VARCHAR(255) DEFAULT NULL COMMENT '文件路径',
    content     MEDIUMTEXT   DEFAULT NULL COMMENT '解析后的文本内容',
    create_time DATETIME     NOT NULL DEFAULT CURRENT_TIMESTAMP,
    UNIQUE KEY uk_resume_version (resume_id, version),
    KEY idx_resume (resume_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci COMMENT='简历版本';
