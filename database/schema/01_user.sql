SET NAMES utf8mb4;
ALTER DATABASE
    CHARACTER SET utf8mb4
    COLLATE utf8mb4_general_ci;

CREATE TABLE IF NOT EXISTS users (
    id            BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY COMMENT '用户ID',
    username      VARCHAR(64)  NOT NULL COMMENT '用户名',
    email         VARCHAR(128) NOT NULL COMMENT '邮箱（登录账号）',
    password_hash VARCHAR(255) NOT NULL COMMENT 'bcrypt密码哈希',
    avatar        VARCHAR(255) DEFAULT NULL COMMENT '头像URL',
    status        TINYINT      NOT NULL DEFAULT 1 COMMENT '1正常 0禁用',
    create_time   DATETIME     NOT NULL DEFAULT CURRENT_TIMESTAMP,
    update_time   DATETIME     NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    UNIQUE KEY uk_email (email),
    KEY idx_username (username)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci COMMENT='用户账号';

CREATE TABLE IF NOT EXISTS profiles (
    id              BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY COMMENT '主键',
    user_id         BIGINT UNSIGNED NOT NULL COMMENT '用户ID',
    target_position VARCHAR(128) DEFAULT NULL COMMENT '目标岗位',
    experience      VARCHAR(64)  DEFAULT NULL COMMENT '经验描述',
    location        VARCHAR(64)  DEFAULT NULL COMMENT '期望城市',
    salary_min      INT UNSIGNED DEFAULT NULL COMMENT '期望月薪下限(千元)',
    salary_max      INT UNSIGNED DEFAULT NULL COMMENT '期望月薪上限(千元)',
    summary         TEXT         DEFAULT NULL COMMENT '自我评价',
    create_time     DATETIME     NOT NULL DEFAULT CURRENT_TIMESTAMP,
    update_time     DATETIME     NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    UNIQUE KEY uk_user_id (user_id),
    KEY idx_location (location)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci COMMENT='用户求职画像(1:1)';

CREATE TABLE IF NOT EXISTS skills (
    id          BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY COMMENT '主键',
    user_id     BIGINT UNSIGNED NOT NULL COMMENT '用户ID',
    name        VARCHAR(64)  NOT NULL COMMENT '技能名',
    level       TINYINT      NOT NULL DEFAULT 1 COMMENT '1了解 2熟悉 3精通',
    create_time DATETIME     NOT NULL DEFAULT CURRENT_TIMESTAMP,
    UNIQUE KEY uk_user_skill (user_id, name),
    KEY idx_user (user_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci COMMENT='用户技能';
