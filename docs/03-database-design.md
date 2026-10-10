# JobPilot AI 数据库设计文档 V2.0

> 依据架构 V3.0 与 [ADR-0003](adr/0003-database-choice.md) 编写。本文档是当前 database contract authority，数据库字段、索引和迁移语义以本文档为准。

## 1. 数据库总览

| 项 | 值 |
|---|---|
| 引擎 | MySQL 8.0 |
| 库名 | `jobpilot` |
| 字符集 | `utf8mb4` |
| 排序规则 | `utf8mb4_general_ci` |
| 表数量 | 11 |

## 2. ER 关系

```mermaid
erDiagram
    users ||--o| profiles : "1:1 画像"
    users ||--o{ skills : "1:N 技能"
    users ||--o{ resumes : "1:N 简历"
    resumes ||--o{ resume_versions : "1:N 版本"
    users ||--o{ applications : "1:N 投递"
    jobs ||--o{ applications : "1:N"
    jobs ||--o{ job_requirements : "1:N 技能要求"
    resume_versions ||--o{ applications : "投递使用版本"
    users ||--o{ ai_tasks : "1:N AI任务"
    users ||--o{ interview_records : "1:N 面试记录"
    users ||--o{ notifications : "1:N 通知"
```

## 3. 建表 DDL（锁定版）

```sql
CREATE DATABASE IF NOT EXISTS jobpilot
  DEFAULT CHARACTER SET utf8mb4
  COLLATE utf8mb4_general_ci;

USE jobpilot;

-- ============ 用户域 ============

CREATE TABLE users (
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
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='用户账号';

CREATE TABLE profiles (
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
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='用户求职画像(1:1)';

CREATE TABLE skills (
    id          BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY COMMENT '主键',
    user_id     BIGINT UNSIGNED NOT NULL COMMENT '用户ID',
    name        VARCHAR(64)  NOT NULL COMMENT '技能名',
    level       TINYINT      NOT NULL DEFAULT 1 COMMENT '1了解 2熟悉 3精通',
    create_time DATETIME     NOT NULL DEFAULT CURRENT_TIMESTAMP,
    UNIQUE KEY uk_user_skill (user_id, name),
    KEY idx_user (user_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='用户技能';

-- ============ 简历域 ============

CREATE TABLE resumes (
    id          BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY COMMENT '简历ID',
    user_id     BIGINT UNSIGNED NOT NULL COMMENT '所属用户',
    title       VARCHAR(128) NOT NULL COMMENT '简历名称(如C++后端版)',
    create_time DATETIME     NOT NULL DEFAULT CURRENT_TIMESTAMP,
    update_time DATETIME     NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    KEY idx_user (user_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='简历主表';

CREATE TABLE resume_versions (
    id          BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY COMMENT '版本ID',
    resume_id   BIGINT UNSIGNED NOT NULL COMMENT '简历ID',
    version     INT            NOT NULL COMMENT '版本号(自增)',
    file_path   VARCHAR(255) DEFAULT NULL COMMENT '文件路径',
    content     MEDIUMTEXT   DEFAULT NULL COMMENT '解析后的文本内容',
    create_time DATETIME     NOT NULL DEFAULT CURRENT_TIMESTAMP,
    UNIQUE KEY uk_resume_version (resume_id, version),
    KEY idx_resume (resume_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='简历版本';

-- ============ 岗位域 ============

CREATE TABLE jobs (
    id             BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY COMMENT '岗位ID',
    company        VARCHAR(128) NOT NULL COMMENT '公司',
    title          VARCHAR(128) NOT NULL COMMENT '职位',
    location       VARCHAR(64)  DEFAULT NULL COMMENT '地点',
    salary_min     INT UNSIGNED DEFAULT NULL COMMENT '月薪下限(千元)',
    salary_max     INT UNSIGNED DEFAULT NULL COMMENT '月薪上限(千元)',
    salary_currency VARCHAR(8)   NOT NULL DEFAULT 'CNY' COMMENT '币种',
    description    TEXT         DEFAULT NULL COMMENT '岗位描述',
    source         VARCHAR(32)  NOT NULL DEFAULT 'manual' COMMENT 'manual/import/crawler 或 allowlist 观测来源标识(0014)',
    source_url     VARCHAR(255) DEFAULT NULL COMMENT '来源链接(去重键)',
    status         TINYINT      NOT NULL DEFAULT 1 COMMENT '1有效 0下线',
    source_scope   VARCHAR(128) DEFAULT NULL COMMENT '来源 board/scope 标识(0012)',
    external_id    VARCHAR(255) DEFAULT NULL COMMENT '来源侧岗位ID(0012)',
    published_at   DATETIME     DEFAULT NULL COMMENT '来源发布时间(0012)',
    last_seen_at   DATETIME     DEFAULT NULL COMMENT '最近一次完整观测到的时间(0012)',
    last_verified_at DATETIME   DEFAULT NULL COMMENT '最近一次存活校验时间(0012)',
    closed_at      DATETIME     DEFAULT NULL COMMENT '软关闭时间(0012)',
    missing_count  INT          NOT NULL DEFAULT 0 COMMENT '连续完整观测缺失次数(0012)',
    source_status  VARCHAR(32)  NOT NULL DEFAULT 'unknown' COMMENT 'unknown/ok/missing/closed(0012)',
    education_required   TINYINT   DEFAULT NULL COMMENT '最低学历:1不限/2大专/3本科/4硕士/5博士(0014)',
    experience_years_min SMALLINT  DEFAULT NULL COMMENT '最低经验年限(0014)',
    experience_years_max SMALLINT  DEFAULT NULL COMMENT '最高经验年限(0014)',
    job_nature           TINYINT   DEFAULT NULL COMMENT '岗位性质:1校招/2社招/3实习(0014)',
    deadline             DATETIME  DEFAULT NULL COMMENT '投递截止时间(UTC瞬时;格式约定见§9.1,0014)',
    deadline_date_only   TINYINT   DEFAULT NULL COMMENT 'deadline是否仅来源日期:1是/0否/NULL未知(0014)',
    create_time    DATETIME     NOT NULL DEFAULT CURRENT_TIMESTAMP,
    update_time    DATETIME     NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    UNIQUE KEY uk_source_url (source_url),
    KEY idx_company (company),
    KEY idx_title (title),
    KEY idx_location (location),
    KEY idx_status (status),
    KEY idx_source_scope_status (source, source_scope, status)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='岗位';

CREATE TABLE job_requirements (
    id          BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY COMMENT '主键',
    job_id      BIGINT UNSIGNED NOT NULL COMMENT '岗位ID',
    skill       VARCHAR(64) NOT NULL COMMENT '技能名',
    level       TINYINT     NOT NULL DEFAULT 1 COMMENT '要求等级',
    create_time DATETIME    NOT NULL DEFAULT CURRENT_TIMESTAMP,
    UNIQUE KEY uk_job_skill (job_id, skill),
    KEY idx_job (job_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='岗位技能要求';

-- ============ 投递域 ============

CREATE TABLE applications (
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
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='投递记录';

-- ============ AI 域 ============

CREATE TABLE ai_tasks (
    id          BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY COMMENT '任务ID',
    user_id     BIGINT UNSIGNED NOT NULL COMMENT '用户',
    task_type   VARCHAR(32) NOT NULL COMMENT 'job_match/resume_optimize/interview_gen',
    job_id      BIGINT UNSIGNED DEFAULT NULL COMMENT '关联岗位',
    resume_id   BIGINT UNSIGNED DEFAULT NULL COMMENT '关联简历',
    status      TINYINT NOT NULL DEFAULT 0 COMMENT '0pending 1processing 2success 3failed',
    result      JSON DEFAULT NULL COMMENT '结果JSON',
    error_msg   VARCHAR(512) DEFAULT NULL COMMENT '失败原因',
    retry_count INT NOT NULL DEFAULT 0 COMMENT '重试次数',
    locked_by       VARCHAR(64) DEFAULT NULL COMMENT '当前worker标识(0013)',
    lease_expire_at DATETIME    DEFAULT NULL COMMENT '租约到期时间(0013)',
    rule_result     JSON        DEFAULT NULL COMMENT '确定性规则基线结果(0013)',
    create_time DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    update_time DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    KEY idx_user_status (user_id, status),
    KEY idx_status (status),
    KEY idx_type_status (task_type, status),
    KEY idx_status_lease (status, lease_expire_at)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='AI异步任务';

CREATE TABLE interview_records (
    id               BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY COMMENT '记录ID',
    user_id          BIGINT UNSIGNED NOT NULL COMMENT '用户',
    job_id           BIGINT UNSIGNED DEFAULT NULL COMMENT '关联岗位(可空)',
    question         TEXT NOT NULL COMMENT '面试问题',
    user_answer      TEXT DEFAULT NULL COMMENT '用户回答',
    reference_answer TEXT DEFAULT NULL COMMENT '参考答案',
    skill_tag        VARCHAR(64) DEFAULT NULL COMMENT '技能标签',
    create_time      DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    KEY idx_user (user_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='AI面试记录';

-- ============ 通知域 ============

CREATE TABLE notifications (
    id          BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY COMMENT '通知ID',
    user_id     BIGINT UNSIGNED NOT NULL COMMENT '用户',
    title       VARCHAR(128) NOT NULL COMMENT '标题',
    content     TEXT DEFAULT NULL COMMENT '内容',
    status      TINYINT NOT NULL DEFAULT 0 COMMENT '0未读 1已读',
    create_time DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    KEY idx_user_status (user_id, status)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='通知';
```

## 4. 设计决策说明

| 决策 | 理由 |
|---|---|
| `resumes` 不存 `current_version` | 用 `MAX(version)` 推导，避免双写不一致 |
| `applications` 引用 `resume_version_id` | 记录"用哪个版本投的"，可追溯 |
| `(user_id, job_id)` 唯一 | 同一用户对同一岗位只有一条投递，收藏→投递走状态流转 |
| `jobs.source_url` 唯一 | 爬虫/导入去重键（NULL 值在 MySQL 唯一索引中允许多个，不影响手工岗位） |
| 薪资数值化 `salary_min/max` | 支持按薪资区间筛选（VARCHAR 薪资无法排序） |
| 状态用 `TINYINT` + 注释 | 代码层枚举对应，禁魔法数 |
| 不加物理外键 | 应用层保证一致性，便于后续拆分/归档 |
| `ai_tasks.result` 用 JSON | 结果结构随功能演进，免改表 |

## 5. 索引策略

| 表 | 查询模式 | 索引 |
|---|---|---|
| users | 按 email 登录 | `uk_email` |
| profiles | 按 user 查画像 | `uk_user_id` |
| applications | 按用户×状态列表 | `idx_user_status` |
| jobs | 按公司/地点/状态过滤；按来源 scope 观测 | `idx_company` `idx_location` `idx_status` `idx_source_scope_status` |
| ai_tasks | 用户查任务 + Worker 捞待办 + 租约扫描 | `idx_user_status` `idx_status` `idx_type_status` `idx_status_lease` |
| notifications | 用户查未读 | `idx_user_status` |

> 0014 的 6 个新列（`education_required` / `experience_years_*` / `job_nature` /
> `deadline` / `deadline_date_only`）本阶段**不新增索引**：计划曾提议
> `idx_job_nature_education`，按确认决定延后（新字段本阶段不进入检索/排名消费，
> 见 §9.4 与 `docs/04` §5.1）。其中 `deadline_date_only` 是 F13 落地时派生的
> 类型标志（见 §9.1/§9.6）。

> 岗位搜索初期用 `LIKE '%kw%'`，数据量大后引入全文索引或 ES（见架构文档 §12 演进路径）。

## 6. 数据初始化

- `database/schema/` 挂载到容器 `docker-entrypoint-initdb.d`，首次启动自动建表
- 种子数据尚未纳入当前 P0 schema；后续引入时必须保持幂等
- 所有脚本幂等可重复执行

## 7. 0012 Additive Freshness Migration Contract（增量契约）

本节登记 0012 Job Source Adapter + Freshness 的**增量字段契约**。
该契约已作为 0012 Step 1 落地：列与索引已写入 §3 建表 DDL 与
`database/schema/03_job.sql`（新库由初始化脚本直接创建）。

对齐来源：
[`exec-plans/completed/0012-job-source-adapter-freshness.md`](exec-plans/completed/0012-job-source-adapter-freshness.md) §8
与 [`adr/0010-job-freshness-and-lifecycle.md`](adr/0010-job-freshness-and-lifecycle.md)。

### 7.1 `jobs` 新增列（已落地）

已写入 §3 建表 DDL 与 `database/schema/03_job.sql`：

```sql
source_scope      VARCHAR(128) NULL
external_id       VARCHAR(255) NULL
published_at      DATETIME NULL
last_seen_at      DATETIME NULL
last_verified_at  DATETIME NULL
closed_at         DATETIME NULL
missing_count     INT NOT NULL DEFAULT 0
source_status     VARCHAR(32) NOT NULL DEFAULT 'unknown'
```

已建索引：

```sql
KEY idx_source_scope_status (source, source_scope, status)
```

### 7.2 不变量

- `source_url UNIQUE`（`uk_source_url`）不变，仍是唯一幂等键。
- `create_time` 继续表示 first seen；**不新增** `first_seen_at` 列。
- `update_time` **不等于** `last_seen_at`：它只在已存元数据变化时更新。
- freshness 状态（fresh / aging / stale / closed）与阈值（24h / 7d / 48h 宽限）
  都是**计算值，不落库**；数据库只存观测事实。
- 现有 `status`（`1` 有效 / `0` 下线）语义不变。
- 既有行默认 `source_status='unknown'`，保持向后兼容。
- **不引入** Canonical Job / JobSource / 多源 alias 拆分；当前 `jobs` 行仍是
  唯一规范行。

### 7.3 既有库升级

`database/schema/*.sql` 使用 `CREATE TABLE IF NOT EXISTS`，对已存在的库不会自动
加列。既有自建库按下述**增量 ALTER** 升级；重复执行会报 duplicate column，
属预期行为，运维侧跳过即可：

```sql
ALTER TABLE jobs
    ADD COLUMN source_scope      VARCHAR(128) DEFAULT NULL COMMENT '来源 board/scope 标识(0012)',
    ADD COLUMN external_id       VARCHAR(255) DEFAULT NULL COMMENT '来源侧岗位ID(0012)',
    ADD COLUMN published_at      DATETIME     DEFAULT NULL COMMENT '来源发布时间(0012)',
    ADD COLUMN last_seen_at      DATETIME     DEFAULT NULL COMMENT '最近一次完整观测到的时间(0012)',
    ADD COLUMN last_verified_at  DATETIME     DEFAULT NULL COMMENT '最近一次存活校验时间(0012)',
    ADD COLUMN closed_at         DATETIME     DEFAULT NULL COMMENT '软关闭时间(0012)',
    ADD COLUMN missing_count     INT          NOT NULL DEFAULT 0 COMMENT '连续完整观测缺失次数(0012)',
    ADD COLUMN source_status     VARCHAR(32)  NOT NULL DEFAULT 'unknown' COMMENT 'unknown/ok/missing/closed(0012)',
    ADD KEY idx_source_scope_status (source, source_scope, status);
```

既有行无需回填：`missing_count` 取默认 `0`，`source_status` 取 `'unknown'`。

### 7.4 实施顺序

1. [x] 0012 Step 1：列与索引已写入 §3 DDL 与 `database/schema/03_job.sql`。
2. 再实现 Source Observation 领域模型与 Source Sync DAO / Service / API
   （写入路径 `POST /internal/jobs/source-sync`，见 `docs/04` §5.5）。
3. 迁移落地后，`docs/04` §10.2 的 freshness 公开响应字段才生效。

## 8. 0013 Additive AI Task Migration Contract（增量契约）

本节登记 0013 AI Matching 对 `ai_tasks` 的**增量字段契约**。契约已作为 0013
Step 1 落地：列与索引已写入 §3 建表 DDL 与 `database/schema/05_ai.sql`。

对齐来源：
[`exec-plans/completed/0013-ai-matching.md`](exec-plans/completed/0013-ai-matching.md) §5.1
与 [`adr/0009-ai-pipeline-and-provider-strategy.md`](adr/0009-ai-pipeline-and-provider-strategy.md)。

### 8.1 `ai_tasks` 新增列（已落地）

```sql
locked_by        VARCHAR(64) NULL   -- 当前 worker 标识
lease_expire_at  DATETIME    NULL   -- 租约到期时间
rule_result      JSON        NULL   -- 确定性规则基线结果（match_rule_v1）
```

已建索引：`KEY idx_status_lease (status, lease_expire_at)`（租约扫描）。

### 8.2 不变量

- `status`（0 pending / 1 processing / 2 success / 3 failed）语义不变。
- `result` 始终是**最终**结果；`rule_result` 是创建时写入的确定性基线，只增不改
  （worker 只能追加字段，不得覆盖 `score`/`breakdown`/`matched_skills`）。
- `retry_count` 表示已尝试次数；尝试上限默认 3，租约默认 120s。
- `locked_by` / `lease_expire_at` 只表示当前租约；`status` 回到 0 时二者清空。
- 不新增表、不改既有列类型、不加唯一约束；既有行保持可读（新列为 NULL）。

### 8.3 既有库升级

`database/schema/*.sql` 使用 `CREATE TABLE IF NOT EXISTS`，对已存在的库不会自动
加列。既有自建库按下述**增量 ALTER** 升级；重复执行会报 duplicate column，属预期：

```sql
ALTER TABLE ai_tasks
    ADD COLUMN locked_by       VARCHAR(64) DEFAULT NULL COMMENT '当前worker标识(0013)',
    ADD COLUMN lease_expire_at DATETIME    DEFAULT NULL COMMENT '租约到期时间(0013)',
    ADD COLUMN rule_result     JSON        DEFAULT NULL COMMENT '确定性规则基线结果(0013)',
    ADD KEY idx_status_lease (status, lease_expire_at);
```

既有行无需回填。

### 8.4 实施顺序

1. [x] 0013 Step 1：列与索引已写入 §3 DDL 与 `database/schema/05_ai.sql`。
2. 再实现 `match_rule_v1` 领域、AI Task DAO / Service / 内部 API 与 worker。

## 9. 0014 Additive Job Field Migration Contract（增量契约）

本节登记 0014 China Campus Sources + Manual Capture 对 `jobs` 的**增量字段契约**，
并定义 `deadline` 的格式与时区语义。列已作为 0014 Step 4 写入 §3 建表 DDL 与
`database/schema/03_job.sql`（新库由初始化脚本直接创建）。

对齐来源：
[`exec-plans/completed/0014-china-campus-sources-and-manual-capture.md`](exec-plans/completed/0014-china-campus-sources-and-manual-capture.md)
§5.1/§9 Step 4/§15.4（F13/F14 已决定；该计划已于 2026-10-09 归档，Step 5 环境受限
并结转）与
[`adr/0011-china-first-sourcing-and-compliance.md`](adr/0011-china-first-sourcing-and-compliance.md) §4。

### 9.1 `deadline` 格式与时区约定（唯一权威，四端一致）

**本节是唯一权威。** DB、Python 归一化器、C++ DTO、API 响应都引用本节，
任何模块不得自行解释 `deadline` 的时区或格式。

```text
权威时区: Asia/Shanghai（UTC+8）。中国自 1991 年起不再实行夏令时，
          因此实现为固定偏移 +08:00；任何模块都不得使用**服务器本地时区**
          （禁止 localtime/mktime/系统 TZ 参与 deadline 换算）。

DB 列 jobs.deadline      DATETIME，UTC 瞬时，文本 "YYYY-MM-DD HH:MM:SS"
                         （与 published_at 的存储格式一致）。
DB 列 jobs.deadline_date_only TINYINT NULL:
                         1 = 来源只给了日期（date-only）
                         0 = 来源给了具体时刻（explicit time）
                         NULL = 无 deadline / 未知
```

- **date-only 存储约定**：来源只给日期（如 `2026-10-08`）时，存 **北京本地正午
  12:00** 对应的 UTC 瞬时，即 `2026-10-08 04:00:00`（= 北京 12:00）。
  因此 `deadline_date_only = 1` 时**必须**满足 `deadline` 的 UTC 时间部分
  等于 `04:00:00`；这是可校验的存储不变量。
  **正午只是存储约定，不是到期时刻**：它不得被当作、也不得被渲染成过期时间。
- **explicit-time**：来源给了具体时刻时保留真实瞬时，由 Python 归一化器按该值的
  来源时区换算成 UTC 写入；C++ 只做格式与范围校验，不重算时区、绝不把真实时刻
  改写成正午。
- **读回（日期往返）**：读取时把 UTC 加 `+08:00` 还原为北京本地时间，date-only
  值因此回到 `12:00` 并**原样回到原日期**，不会跨天漂移。
- **C++ DTO**：`JobImportData::deadline` / `deadlineDateOnly` 与 DB 同构
  （UTC 文本 + 可空布尔）；`JobSummary::deadline` / `deadlineDateOnly` 读回同样的
  UTC 文本 + 标志。时区换算只在 API 边界发生一次
  （`job_controller.cpp` 的响应映射），其余模块搬运原始值。
- **未知类型（不臆造精度）**：`deadline` 非空但 `deadline_date_only = NULL` 时，
  该值的类型**未知**（pre-0014 既有行、或运维/SQL 直写只给了 `deadline`）。
  处理规则：**不默认成任一类型**——保留已存的 UTC 瞬时，响应按北京本地
  `"YYYY-MM-DD HH:MM:SS"` 原样输出真实瞬时，并输出 `deadline_date_only: null`
  表示类型未知。写入路径（两条 HTTP 路径）不会产生这种组合：`deadline` 非空时
  缺省 `deadline_date_only=false`（见下）。选择该「显式 NULL」规则而不是给
  legacy 行补 `0`/`1`，是因为两种补法都会臆造精度：补 `0` 会把日历语义谎报成
  精确时刻，补 `1` 会把真实时刻谎报成日期并掩盖真实时间。
- **API 响应**（`docs/04` §5.1/§5.2）：`deadline` 输出**北京本地**值——
  `deadline_date_only = true` 时输出裸日期 `"YYYY-MM-DD"`（**不输出 12:00**，
  避免被读成到期时刻）；`false` 或 `NULL` 时输出 `"YYYY-MM-DD HH:MM:SS"`（UTC+8）；
  `deadline_date_only` 键为 `true`/`false`/`null`（无 `deadline` 时为 `null`）。
- **写入校验**：两条写入路径（`POST /internal/jobs/import`、`POST /internal/jobs/source-sync`）
  统一按上表校验，非法值返回 `400`（`kInvalidParam`），绝不静默丢弃、改写或截断。
- **本阶段不做的消费**：`deadline` 不参与可见性、排名或自动关闭；本阶段不新增
  「过期自动关闭」规则（`docs/04` §5.1 记录该可见性限制）。

### 9.2 `jobs` 新增列（已落地）

写入 §3 建表 DDL 与 `database/schema/03_job.sql`：

```sql
education_required   TINYINT  NULL   -- 最低学历: 1不限/2大专/3本科/4硕士/5博士
experience_years_min SMALLINT NULL   -- 最低经验年限（非负整年）
experience_years_max SMALLINT NULL   -- 最高经验年限（非负整年）
job_nature           TINYINT  NULL   -- 岗位性质: 1校招/2社招/3实习
deadline             DATETIME NULL   -- 投递截止时间（UTC 瞬时，见 §9.1）
deadline_date_only   TINYINT  NULL   -- deadline 是否仅日期（见 §9.1）
```

**未新增索引**（含计划曾提议的 `idx_job_nature_education`，本阶段延后）。

`jobs.source` 的取值注释同步修正：`manual`/`import`/`crawler` 是人工路径取值；
观测路径取值由 C++ allowlist `IsSupportedObservationSource`
（`service/src/job/source_observation.cpp`）决定。China 来源标识按 0014 Step 3
在实测登记后加入 allowlist，本阶段**不预先扩大** allowlist。

### 9.3 不变量

- 6 个新列（`education_required` / `experience_years_min` / `experience_years_max` /
  `job_nature` / `deadline` / `deadline_date_only`）全部可空、无默认值（`DEFAULT NULL`），
  既有行升级后全部 `NULL`，**无需回填**，旧代码继续可读。
  既有行的 `deadline_date_only = NULL` 语义是「类型未知」，响应输出 `null`
  （见 §9.1），**不回填**为 `0`/`1`。
- 枚举值域：`education_required ∈ 1..5`、`job_nature ∈ 1..3`；
  未枚举出的整数一律拒绝（不写 `1` 兜底）；来源未提供或无法确定时**省略该键**。
- `experience_years_min/max ∈ 0..32767`（`SMALLINT`），且
  `min <= max`（两者都存在时）。
- `deadline` 非空时必须通过 `IsValidDbTimestamp`
  （`YYYY-MM-DD HH:MM:SS`，1000..9999，月长/闰年校验）；`deadline_date_only`
  非空时 `deadline` 必须非空；`deadline_date_only = 1` 时必须满足 §9.1 的
  `04:00:00` 不变量。
- `uk_source_url` 仍是唯一幂等键；`status` / freshness 列语义不变。
- **人工采集胜出（F14）**：`POST /internal/jobs/source-sync` 不得覆盖
  `source='manual'` 的行——不更新其任何列、不改 `source`、不改生命周期字段、
  不替换其 `job_requirements`、不关闭它；该观测被跳过并计入可诊断计数
  （`docs/04` §5.5）。
- 观测路径对来源可缺省的新字段沿用 0012 的 `COALESCE(VALUES(x), x)` 保护：
  本次观测未带 `deadline`/`deadline_date_only` 时不得抹掉已观测值；人工路径保持
  整组覆盖语义。

### 9.4 既有库升级

`database/schema/*.sql` 使用 `CREATE TABLE IF NOT EXISTS`，对已存在的库不会自动
加列。既有自建库按下述**增量 ALTER** 升级；重复执行会报 duplicate column，属预期
行为，运维侧跳过即可：

```sql
ALTER TABLE jobs
    ADD COLUMN education_required   TINYINT  DEFAULT NULL COMMENT '最低学历:1不限/2大专/3本科/4硕士/5博士(0014)',
    ADD COLUMN experience_years_min SMALLINT DEFAULT NULL COMMENT '最低经验年限(0014)',
    ADD COLUMN experience_years_max SMALLINT DEFAULT NULL COMMENT '最高经验年限(0014)',
    ADD COLUMN job_nature           TINYINT  DEFAULT NULL COMMENT '岗位性质:1校招/2社招/3实习(0014)',
    ADD COLUMN deadline             DATETIME DEFAULT NULL COMMENT '投递截止时间(UTC瞬时;格式约定见§9.1,0014)',
    ADD COLUMN deadline_date_only   TINYINT  DEFAULT NULL COMMENT 'deadline是否仅来源日期:1是/0否/NULL未知(0014)';
```

既有行无需回填；升级路径必须在**pre-0014 库**上实跑一次并复验 §3 形状断言
（0014 Step 4 的附加证据，非 CI 门禁）。

### 9.5 实施顺序

1. [x] 0014 Step 4：本契约（§9.1/§9.2/§9.3/§9.4）先行；列已写入 §3 DDL 与
   `database/schema/03_job.sql`。
2. 再让 C++ 两条写入路径（解析 → 校验 → upsert → 读回 → 响应）依赖本契约；
   Python 归一化器按 §9.1 产出 `deadline`/`deadline_date_only`。
3. 新字段的检索/排名消费属后续匹配阶段，**不在 0014**。

### 9.6 派生决定记录：为什么需要 `deadline_date_only`

F13 确认的第 3/4 条要求：(a) date-only 值按北京正午的 UTC 瞬时存储并能按日期
往返；(b) 正午只是存储约定，**不得被渲染成到期时刻**；(c) 来源给了具体时刻时
保留真实瞬时、绝不改写为正午。

只存一个 UTC 瞬时无法同时满足 (b) 与 (c)：读回时无法区分「date-only 的 04:00:00
存储约定」与「恰好落在 04:00:00 UTC 的真实时刻」，响应层也就无法在只给日期时
避免输出一个看似到期时刻的 12:00。因此 `deadline_date_only` 是 (b)/(c) 在
存储+响应链路上的**逻辑必需**，作为派生决定记录于此。若产品否决该派生，
可退回「只存瞬时」，但必须同时放弃 (b) 的区分能力。
