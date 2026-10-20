# JobPilot AI API 接口设计文档 V2.0

> 依据架构 V3.0 编写。本文档是当前 API contract authority（接口契约权威依据），
> 前端/后端/AI 三方以此为准。
> 当前 MVP 的 `/api/v1/*` 由 C++ Backend `HttpServer` 直接提供；
> Gateway/Nginx 属于 Future Work，不是当前运行时的前置依赖。

## 1. 通用约定

### 1.1 基础路径

```
/api/v1/{resource}
```

### 1.2 统一响应格式（所有接口）

```json
{
  "code": 0,
  "message": "success",
  "data": { }
}
```

- `code == 0` 成功；非 0 失败，见错误码表
- `data` 缺省为 `null`（无需返回数据时）

### 1.3 鉴权

- 除 `register` / `login` / `health` 及**岗位检索与详情**（`GET /api/v1/jobs`、`GET /api/v1/jobs/{id}`）外，所有接口需携带：

```
Authorization: Bearer <token>
```

- Token 由登录接口下发，Redis 存 `session:{token}`，TTL 24h
- Token 无效/过期返回 `20001`（无效）或 `20002`（过期）

### 1.4 分页约定

```
GET /api/v1/{resource}?page=1&page_size=20
```

返回 `data` 结构：

```json
{
  "list": [],
  "total": 100,
  "page": 1,
  "page_size": 20
}
```

### 1.5 异步 AI 任务约定

AI 接口均为异步：发起返回 `task_id`，客户端轮询 `GET /api/v1/ai/tasks/{task_id}`。

```
发起: POST /api/v1/ai/match   -> data: {"task_id": 1024}
轮询: GET  /api/v1/ai/tasks/1024
```

任务状态：`0 pending · 1 processing · 2 success · 3 failed`（与 `ai_tasks.status` 一致）。

## 2. 错误码表（锁定）

| 区间 | 含义 |
|---|---|
| 0 | 成功 |
| 1000x | 通用参数 |
| 2000x | 认证 |
| 3000x | 用户域 |
| 4000x | 资源域 |
| 5000x | AI 域 |
| 6000x | 服务端 |

| code | message | 场景 |
|---|---|---|
| 0 | success | 成功 |
| 10001 | invalid parameter | 参数校验失败 |
| 10002 | malformed request body | 请求体格式错误 |
| 20001 | unauthorized | 未登录 / Token 无效 |
| 20002 | token expired | Token 过期 |
| 20003 | forbidden | 无权限 |
| 30001 | email already exists | 邮箱已注册 |
| 30002 | user not found | 用户不存在 |
| 30003 | invalid password | 密码错误 |
| 40001 | job not found | 岗位不存在 |
| 40002 | resume not found | 简历不存在 |
| 40003 | application not found | 投递记录不存在 |
| 40004 | duplicate application | 重复投递 |
| 40005 | invalid status transition | 非法状态迁移 |
| 50001 | ai task not found | AI 任务不存在 |
| 50002 | ai service busy | AI 服务繁忙/超时 |
| 50003 | ai analysis failed | AI 分析失败 |
| 60001 | internal error | 服务端内部错误 |
| 60002 | database error | 数据库错误 |

---

## 3. 用户接口

### 3.1 注册

```
POST /api/v1/users/register
```

```json
{
  "username": "tom",
  "email": "tom@qq.com",
  "password": "123456"
}
```

```json
{
  "code": 0,
  "message": "success",
  "data": { "user_id": 10001 }
}
```

### 3.2 登录

```
POST /api/v1/users/login
```

```json
{
  "email": "tom@qq.com",
  "password": "123456"
}
```

```json
{
  "code": 0,
  "message": "success",
  "data": { "token": "xxx", "user_id": 10001 }
}
```

### 3.3 获取当前用户

```
GET /api/v1/users/me
```

```json
{
  "code": 0,
  "message": "success",
  "data": {
    "id": 10001,
    "username": "tom",
    "email": "tom@qq.com",
    "avatar": null,
    "created_at": "2026-08-09 10:00:00"
  }
}
```

### 3.4 获取/更新画像

```
GET /api/v1/users/profile
PUT /api/v1/users/profile
```

```json
{
  "target_position": "C++后端开发",
  "experience": "应届生",
  "location": "深圳",
  "salary_min": 15,
  "salary_max": 25,
  "summary": "熟悉C++、Linux、网络编程",
  "education_level": 3,
  "graduation_year": 2026,
  "campus_status": 2
}
```

0015 A2 增量键（全部**可选**；省略或显式 `null` = 未知）：

- `education_level`：整数 `1..5`（`1不限/2大专/3本科/4硕士/5博士`），与岗位侧
  `education_required` **同一枚举**；越界（含 0、6、负数、非整数）→ `400`（`code=10001`）。
- `graduation_year`：整数 `1990..2100`（毕业**日历年**，不是时间戳，不参与 deadline 语义）；
  越界（含 1989、2101、负数、非整数）→ `400`。
- `campus_status`：整数 `1非应届/2应届/3在校`；越界（含 0、4、负数、非整数）→ `400`。
- **NULL 语义**：三键省略或为 `null` 表示「未知」，服务端存 `NULL` 并**原样返回 `null`**；
  不得用 0、空串或任何默认档位代替（`docs/03` §10.2）。响应中三键始终出现，无值即 `null`。
- `GET /api/v1/users/profile` 与 `PUT /api/v1/users/profile` 使用同一组键；更新为整组语义
  （未提供的键写 `NULL`，与既有 6 个键一致）。

### 3.5 技能管理

```
POST   /api/v1/users/skills       body: {"name":"C++","level":3}
DELETE /api/v1/users/skills/{id}
GET    /api/v1/users/skills
```

---

## 4. 简历接口

### 4.1 上传简历（创建简历 + 首版本）

```
POST /api/v1/resumes/upload
```

`multipart/form-data`

| 字段 | 说明 |
|---|---|
| file | 文件（PDF/TXT/MD） |
| title | 简历名称，如 "C++后端版" |

```json
{
  "code": 0,
  "message": "success",
  "data": {
    "resume_id": 30001,
    "version": 1,
    "text_extracted": false,
    "text_status": "pdf_no_text"
  }
}
```

**0015 A3 文本状态契约（决策 4 = 方案 b，零新依赖的显式降级）**：PDF 简历**不做文本抽取**
（`resume_versions.content` 保持 `NULL`），但**绝不静默**——每个版本响应都带机器可读状态：

| `text_status` | `text_extracted` | 含义 |
|---|---|---|
| `ok` | `true` | 文本可用（`.txt`/`.md` 上传路径，或 `POST .../versions` 传入的非空白文本） |
| `text_empty` | `false` | 文本路径执行了，但结果是空白（能区分「没抽取」与「抽取了但为空」） |
| `pdf_no_text` | `false` | PDF 原样接收，本单元不抽取（`content = NULL`，契约明确） |
| `text_unavailable` | `false` | `content` 为 `NULL` 且不属于上述情况——兜底值，保证**没有静默 NULL 路径** |

`text_extracted` 是 `text_status == "ok"` 的派生布尔，仅为调用方便。该状态是**派生字段**：
`resume_versions` 不新增列、不做回填（`content` 与 `file_path` 足以判定），因此
`docs/03` 无对应迁移。同一组键出现在 §4.3（新建版本）与 §4.4（版本列表）的响应中。

### 4.2 简历列表

```
GET /api/v1/resumes
```

```json
{
  "code": 0,
  "message": "success",
  "data": {
    "list": [
      { "id": 30001, "title": "C++后端版", "version": 3, "updated_at": "..." }
    ],
    "total": 1,
    "page": 1,
    "page_size": 20
  }
}
```

### 4.3 新建简历版本

```
POST /api/v1/resumes/{id}/versions
```

```json
{ "content": "简历文本内容..." }
```

```json
{
  "code": 0,
  "message": "success",
  "data": {
    "version_id": 30012,
    "version": 4,
    "text_extracted": true,
    "text_status": "ok"
  }
}
```

`content` 为空或全空白 → `400`（`content` 为空串）／`text_status = "text_empty"`（全空白）；
不带 `content` 键 → `400`。语义见 §4.1 的状态表。

### 4.4 简历版本列表

```
GET /api/v1/resumes/{id}/versions
```

```json
{
  "code": 0,
  "message": "success",
  "data": {
    "list": [
      {
        "id": 30012,
        "version": 4,
        "created_at": "...",
        "text_extracted": true,
        "text_status": "ok"
      }
    ]
  }
}
```

列表**每一项**都必须带 `text_status`/`text_extracted`（PDF 版本为 `pdf_no_text`）：
「`content = NULL` 且无标记」是被禁止的失败模式（0015 §5.1 A.degraded）。
响应不返回存储的文本正文本身。

---

## 5. 岗位接口

### 5.1 岗位搜索

```
GET /api/v1/jobs
  ?keyword=C%2B%2B
  &location=深圳
  &salary_min=20
  &salary_max=35
  &skill=Redis
  &include_stale=true
  &posted_within_days=30
  &education_max=3
  &job_nature=1
  &deadline_before=2026-11-15%2000:00:00
  &deadline_after=2026-09-01%2000:00:00
  &open_only=true
  &sort=deadline_asc
  &page=1
  &page_size=20
```

```json
{
  "code": 0,
  "message": "success",
  "data": {
    "list": [
      {
        "id": 20001,
        "company": "某公司",
        "title": "C++服务器开发",
        "location": "深圳",
        "salary_min": 20,
        "salary_max": 35,
        "source": "import",
        "source_url": "https://example.invalid/jobs/20001",
        "apply_url": "https://example.invalid/apply/20001",
        "posted_at": "2026-09-20 08:00:00",
        "first_seen_at": "2026-09-21 10:00:00",
        "last_seen_at": "2026-10-08 12:00:00",
        "last_verified_at": "2026-10-08 12:00:00",
        "freshness": "fresh",
        "source_status": "ok",
        "education_required": 3,
        "experience_years_min": 0,
        "experience_years_max": 3,
        "job_nature": 1,
        "deadline": "2026-10-31",
        "deadline_date_only": true
      }
    ],
    "total": 42,
    "page": 1,
    "page_size": 20
  }
}
```

- `salary_min/salary_max` 表示用户可接受薪资区间，岗位薪资区间与之有交集才返回。
- 使用薪资过滤时，未提供薪资区间的岗位不返回。

**0015 阶段 B 增量契约（B0，全部可选、默认关闭）**

**落地状态（避免文档与代码分歧）**：`with_match` 与 `strict` **均已落地**：
- `with_match=true`：每行追加下表的匹配键；缺省时**不追加这些匹配键**，且既有键
  逐字节不变。0020 起基础载荷**始终**多一个 `apply_url` 键（见下），因此“与今天
  逐字节一致”的旧表述专指 0015B 的匹配键，不再覆盖 0020 新增键；测试按此更新。
- `strict=true`：在**与列表完全相同排序**的候选窗口上先排除 deal-breaker 行再分页，`total` 是
  **过滤后**可完整遍历的行数（不是未过滤的总数）；窗口受 `kMaxRankingCandidates = 2000` 封顶时，
  响应带 `"truncated": true`（未封顶或未使用 `strict` 时为 `false`），**绝不**返回与可遍历行数
  不一致的 `total`；
- 两者都需要会话（解释/排除要用画像，与 `/recommended` 同规则；无 token → `401`）；
- 两个参数只接受 `true|false`，其它值或重复出现 → `400`；
- 使用 `strict=true` 时响应额外带 `truncated` 键（`with_match`/`strict` 都缺省时
  不追加 0015B 匹配键；0020 的 `apply_url` 仍始终存在）。

| 参数 | 默认 | 语义 |
|---|---|---|
| `with_match` | `false` | `false`：不出现下列任何匹配键；既有键逐字节不变，0020 的 `apply_url` 仍始终存在。`true`：每个列表项追加 `score` / `score_version` / `matched_skills` / `missing_skills` / `reasons[]` / `deal_breakers[]` / `deadline_urgency` |
| `strict` | `false` | `false`（默认）：`deal_breakers[]` 只是**数据**，**不排除任何行**。`true`（显式 opt-in）：仅按文档化的 deal-breaker 码排除对应行；其它行不受影响 |

- **权威声明（契约）**：`rule_v1` 是唯一权威的公开排序与分数来源；LLM/增强 worker 的结果
  **永不**出现在 `/api/v1/jobs` 或 `/recommended` 的排序、分数或理由中（ADR-0009 修正 0013）。
  0015 阶段 B **不重新加权** `rule_v1`（决策 5）：学历/应届/薪资兼容性以**判定数据**形式
  返回，不进入加权和，因此分数边界不变。
- `score` / `score_version`（`"rule_v1"`）语义与 `/recommended` 完全一致；`matched_skills` /
  `missing_skills` 是既有字段。
- `reasons[]`：`{"code": "<稳定码>", "text": "<中文句子>", "evidence": "<tag>"}`，
  `evidence ∈ {skills, job_requirements, profile, resume_text, deadline}`（`deadline` 为 0015 新增）。
  稳定码集合（B1）：`skills_matched` / `skills_missing` / `education_met` / `education_unmet` /
  `education_unknown` / `campus_unknown` / `salary_unknown` / `location_match` /
  `location_mismatch` / `deadline_expired` / `deadline_urgent` / `deadline_soon` /
  `deadline_normal`。只陈述已交付事实，不推测。
- `deal_breakers[]`：`{"code", "text"}`，稳定码
  `education_unmet` / `campus_incompatible` / `salary_no_intersection` / `deadline_expired`。
  **NULL 规则**：岗位侧字段为 NULL（或画像侧字段未填）**永不**产生 deal-breaker，也**不惩罚**
  候选；缺失的一侧改为 advisory reason（如 `education_unknown`）。
- `deadline_urgency`：`expired` / `urgent`（≤7 天）/ `soon`（≤30 天）/ `normal` / `unknown`，
  由存储的 UTC 瞬时 + `deadline_date_only` 在 API 边界推导（`date_only` 表示该北京日结束时刻）。
  **NULL 一律 `unknown`，永不视为 `expired`**。它**只**是展示元数据：不关闭岗位、不写
  `status`、不改 freshness、不改 `review_status`（F13 不变量）。
- `skill` 为精确技能过滤，匹配 `job_requirements.skill`。
- 公共搜索结果按 `create_time DESC, id DESC` 稳定排序；`sort=deadline_asc` /
  `education_asc` 覆盖该排序（见下）。

0015A 增量（item 3，查询过滤与排序，与 `docs/03` §9.2 的索引决定同批落地）：

- `education_max=1..5`：`education_required IS NULL OR education_required <= ?`
  —— **学历未知（NULL）的岗位仍然可见**。
- `job_nature=1|2|3`：精确匹配（`1校招/2实习` 等按 §5.1 既有枚举）。
- `deadline_before` / `deadline_after`：UTC `"YYYY-MM-DD HH:MM:SS"` 区间；
  **没有 `deadline` 的岗位两个边界都不满足**（与 `open_only` 区分）。
- `open_only=true`：`deadline IS NULL OR deadline >= NOW()`——**只作筛选**，
  不关闭岗位、不改 `status`、不写 freshness（F13 不变量）。
- `sort=created_desc|deadline_asc|education_asc`（默认 `created_desc`）：
  - `deadline_asc` = `ORDER BY deadline ASC, id ASC`，即 **NULL（未知截止）排在最前**。
    这是索引可取的方向（`idx_deadline`，见 `docs/03` §9.2 的 EXPLAIN 证据）；若要
    NULL 排最后则必须用表达式排序，会退回 filesort 且不使用该索引，故不采用。
  - `education_asc` = `ORDER BY education_required ASC, id ASC`（同样 NULL 在前）；
    该字段**没有索引**（证据见 `docs/03` §9.2）。
  - `sort` 只作用于 `GET /api/v1/jobs`；`GET /api/v1/jobs/recommended` 始终按
    `rule_v1` 分数排序（其过滤条件与 `/jobs` 一致，但排序不变）。
- 非法值（`education_max>5`、`job_nature>3`、时间格式错误、未知 `sort`）→ `400`。

0015A 增量（Step 1，**只新增响应键，不改既有键**）：

- `source_url`：原始岗位链接（与 `GET /api/v1/jobs/{id}` 详情返回**同一个值**；
  没有值时返回 `null`，不返回空字符串）。列表因此不再需要为每一行再取一次详情才能
  打开原始岗位。该键同样出现在 `GET /api/v1/jobs/recommended` 的条目里。

0020 增量（只新增一个响应键，既有键不变）：

- `apply_url`：**实际申请链接**（string|null，**始终存在**）。与 `source_url` 的
  区别：`source_url` 是原始公告/详情页；`apply_url` 是来源**显式提供**的申请入口。
  没有显式值时为 `null`，**不从 `source_url` 推导/拼接**。只接受绝对 `http(s)`
  URL，最大 **512 字符**（UTF-8 码点，与 `VARCHAR(512)` 一致），非法/超长不写入。
  该键同样出现在 `/jobs/recommended` 与 `/jobs/{id}` 的响应里。
- 0015A 的查询过滤/排序（`education_max` / `job_nature` / `deadline_before|after` /
  `open_only` / `sort`）已在 Step 6/3 落地，`docs/03` §9.2 的索引注记与本节同步
  （ExecPlan 0015A §5.6）。
- **公共可见性（0015A item 4；2026-10-11 收紧到全部公共读取路径）**：默认只有
  `review_status = 0`（clean）的岗位出现在公共面，而且这条规则覆盖**所有**公共读取，
  不只是列表：`GET /api/v1/jobs`、`GET /api/v1/jobs/recommended`、
  `GET /api/v1/jobs/{id}`（含随详情返回的 `requirements`）、`POST /api/v1/ai/match`
  的任务创建、`POST /api/v1/resumes/artifacts`（简历定制引用岗位时）以及创建申请
  （`POST /api/v1/applications`）时的岗位校验。未获准公开的岗位在这些路径上一律按
  "不存在"处理（详情 404、code 40001 / `kJobNotFound`），因此接口不会泄漏该岗位是否
  存在，列表也不会泄漏它。
  `needs_review`/`quarantined` 仅能通过受保护的内部端点访问（`docs/03` §9.3）：审核
  队列、审核决定，以及 AI worker 的 `GET /internal/ai/tasks/{id}/input` 不受限制——
  否则被路由到审核的岗位将无法审核、已排队的任务也无法处理。
  回滚开关 `JOB_PUBLIC_INCLUDE_NEEDS_REVIEW=true` 只把上述**公共**读取恢复到 0015A 之前
  的可见性，不改变任何数据，也不影响内部端点。
  实现上 DAO 用两个语义明确的方法区分两类读取：`FindPublicJobById`（公共，带可见性
  谓词）与 `FindReviewableJobById`（内部/worker，只要求岗位处于 active），申请校验用
  `PublicJobExists`；不存在一个"查 Active Job"同时服务两边的模糊入口。

0014 增量字段（Step 4，**只新增响应键，不改既有键**；本阶段不新增请求参数）：

- `education_required`（`1..5`，无值为 `null`）、`experience_years_min/max`
  （非负整年，无值为 `null`）、`job_nature`（`1校招/2社招/3实习`，无值为 `null`）。
- `deadline`：**北京本地时间（Asia/Shanghai, UTC+8）**；
  来源只给日期时输出裸日期 `"YYYY-MM-DD"`，来源给了具体时刻（或类型未知）时输出
  `"YYYY-MM-DD HH:MM:SS"`；无值为 `null`。
  同时输出 `deadline_date_only`（`true` = 仅日期 / `false` = 具体时刻 /
  `null` = 无 `deadline` 或类型未知，见 [`docs/03` §9.1](03-database-design.md)）。
- **`deadline` 只是展示字段**：本阶段不参与可见性、排名，也不触发任何自动关闭。
  因此已过 `deadline` 但仍 `status=1` 的岗位**仍然可见**——这是本阶段已知的
  可见性限制，过期待核实提示/自动关闭属后续阶段。
- 上述新增键同样出现在 `GET /api/v1/jobs/recommended` 的条目里（5.3）。

0012 Freshness 可见性与字段（Step 6 已生效）：

- 默认可见性：`status = 1` **且** `closed_at IS NULL` **且**非 stale。
- `stale` 判定：`last_seen_at` 早于 7 天（`fresh <= 24h`、`aging <= 7d`、`stale > 7d`）。
- **没有观测记录的行（`last_seen_at IS NULL`，即 0012 之前的手工/导入岗位）不视为 stale，仍然可见**，
  否则 0012 会让既有数据整批消失。
- `include_stale=true` 时额外返回 stale 岗位；**已关闭岗位（`closed_at` 非空）始终不返回**。
- `posted_within_days=N`（`1..3650`）按来源发布时间过滤：只返回
  `published_at >= NOW() - N 天` 的岗位，**没有可靠 `published_at` 的岗位被排除**。
- 新增响应字段：`posted_at`、`first_seen_at`（= `create_time`）、`last_seen_at`、
  `last_verified_at`、`freshness`（`fresh`/`aging`/`stale`/`closed`，无观测时为 `null`）、
  `source_status`。
- `freshness` 与阈值都是**计算值，不落库**；`rule_v1` 排名公式不变，freshness 只作可见性信号。
- 相同过滤与可见性规则同样作用于 `GET /api/v1/jobs/recommended`。

### 5.2 岗位详情

```
GET /api/v1/jobs/{id}
```

```json
{
  "code": 0,
  "message": "success",
  "data": {
    "id": 20001,
    "company": "某公司",
    "title": "C++服务器开发",
    "location": "深圳",
    "salary_min": 20,
    "salary_max": 35,
    "salary_currency": "CNY",
    "description": "负责...",
    "requirements": [
      { "skill": "C++", "level": 3 },
      { "skill": "Linux", "level": 2 },
      { "skill": "Redis", "level": 2 }
    ],
    "source_url": "https://...",
    "apply_url": "https://.../apply",
    "created_at": "2026-08-09 10:00:00"
  }
}
```

- 详情响应与 5.1 一致地包含 0012 freshness 字段（`posted_at`、`first_seen_at`、
  `last_seen_at`、`last_verified_at`、`freshness`、`source_status`）。
- 详情响应同样包含 5.1 的 0014 增量字段（`education_required`、
  `experience_years_min/max`、`job_nature`、`deadline`、`deadline_date_only`）。
- 详情响应包含 0020 的 `apply_url`（string|null，始终存在），语义与 5.1 相同。

### 5.3 个性化岗位推荐

```
GET /api/v1/jobs/recommended
Authorization: Bearer <token>
```

支持与 `GET /api/v1/jobs` 相同的 Hard Filter 参数。

```json
{
  "code": 0,
  "message": "success",
  "data": {
    "list": [
      {
        "id": 20001,
        "title": "C++服务器开发",
        "company": "某公司",
        "score": 82.5,
        "score_version": "rule_v1",
        "score_breakdown": {
          "skill": 0.91,
          "title": 0.75,
          "salary": 1.0,
          "location": 1.0,
          "first_seen": 0.8
        },
        "matched_skills": ["C++", "Redis"],
        "missing_skills": ["Kubernetes"]
      }
    ],
    "total": 120,
    "ranked_total": 120,
    "truncated": false,
    "page": 1,
    "page_size": 20
  }
}
```

规则：

- Hard Filter 决定岗位是否进入结果集。
- Soft Ranking 只对已通过 Hard Filter 的岗位计分。
- 已被 Hard Filter 使用的属性会从评分中移除并重新归一化权重，避免重复惩罚。
- `rule_v1` 权重：skill 55%、title 15%、salary 15%、location 10%、first_seen 5%。
- `score` 取值 `0-100`：每个分量被限制在 `0..1`，技能等级高于要求不会把总分推过
  100（该上界由 0013 修复并加回归断言）。0013 的 `match_rule_v1`（见 §7）复用同一
  分量集但去掉 `first_seen` 后重新归一化，且分数取整为 `0-100` 整数。
- 最多加载 2000 个候选岗位；超出时返回 `truncated=true`。
- 不调用 LLM 或 Embedding。
- 可见性规则与 5.1 相同：默认排除 `closed` 与 `stale`，支持 `include_stale` /
  `posted_within_days`，响应条目同样带 `freshness` 等 0012 字段。
- 推荐条目与 5.1 同形，因此也包含 `source_url` 与 0020 的 `apply_url`
  （string|null，始终存在）。

### 5.4 岗位内部导入

该接口不属于公共 `/api/v1/*` API，用于手工录入、数据集导入与测试数据准备。
它不携带来源观测语义（`complete` / `source_scope` / `observed_at`），
独立于 freshness 生命周期；Source Adapter 统一使用 5.5 的 source-sync。
示例请求见 [`examples/job-import.json`](examples/job-import.json)。

```
POST /internal/jobs/import
X-Internal-Token: <environment secret>
```

请求：

```json
{
  "jobs": [
    {
      "company": "某公司",
      "title": "C++后端开发",
      "location": "深圳",
      "salary_min": 20,
      "salary_max": 35,
      "salary_currency": "CNY",
      "description": "...",
      "source": "import",
      "source_url": "https://example.com/job/1",
      "apply_url": "https://example.com/apply/1",
      "education_required": 3,
      "experience_years_min": 0,
      "experience_years_max": 3,
      "job_nature": 1,
      "deadline": "2026-10-08 04:00:00",
      "deadline_date_only": true,
      "published_at": "2026-09-20 00:00:00",
      "requirements": [
        { "skill": "C++", "level": 3 },
        { "skill": "Linux", "level": 2 }
      ]
    }
  ]
}
```

`published_at` 为 0017A 新增的**可选**字段：UTC 文本 `YYYY-MM-DD HH:MM:SS`，与
`POST /internal/jobs/source-sync` 的 `published_at` 同形（没有 kind 标志）。它写入
`jobs.published_at`，公开详情的 `posted_at` 即来自该列；请求省略该字段时不改写已有值
（upsert 用 `COALESCE(VALUES(published_at), published_at)`，绝不隐式清空）。

`apply_url` 为 0020 新增的**可选**字段：实际申请链接，空值或绝对 `http(s)` URL，
最多 **512 字符**（UTF-8 码点）。它写入 `jobs.apply_url`，公开列表/推荐/详情的
`apply_url` 即来自该列；省略时同样用
`COALESCE(VALUES(apply_url), apply_url)`，不清空已有值。相对路径、`javascript:`、
`data:` 或超过 512 字符的值使该 item 被拒绝并可诊断（绝不截断）。

响应：

```json
{
  "created": 1,
  "updated": 0,
  "total": 1,
  "items": [
    {
      "index": 0,
      "source_url": "https://example.com/job/1",
      "review_status": 0,
      "rejected": false,
      "findings": []
    }
  ],
  "needs_review": 0,
  "rejected": 0,
  "clean": 1
}
```

0015A 预检质量（quality gate，判定规则见 `docs/03` §9.3）：

- 每个 item 追加一个 `items[]` 条目：`{index, source_url, review_status, rejected,
  findings:[{code, reason, outcome, review_status}]}`，`code` 是稳定机器码、
  `reason` 是人类可读句子、`outcome ∈ {rejected, needs_review, accepted_advisory}`。
- 顶层计数器 `needs_review` / `rejected` / `clean` 与 `items[]` 对应。
- `reject`（`company_missing`、字节上限、`source_url_unusable`、`description_too_large`）
  **使整个请求返回 `400`（`code=10001`）且不写入任何行**；`data` 里带
  `diagnostic: {code, field, reason}`（同时保留 `items[]` 与计数器），
  例如 `{"code":"source_url_unusable","field":"source_url","reason":"..."}`。
- `needs_review`：写入 `review_status = 1` 并登记 `job_review_items`，公共搜索/推荐不可见。
- 仅 advisory（如 `source_url_shape_unrecognised`、`description_marker_minor`）保持
  `review_status = 0`，行仍然可见。
- 判定是 service 层的纯函数（`EvaluateJobQuality` + `MarkDuplicateContentInBatch`），
  无 I/O、无时钟、无随机；`source_url` 必须是绝对 `http(s)` URL。
- **`employer_is_platform`（0015A option B）**：只有**两个确定性信号同时成立**才判定 ——
  规范化 `company` 精确命中注册表平台名，且 `source_url` 主机名经注册表 `platform_hosts:`
  映射出**同一个**平台。主机名未知或指向别的平台时**不下结论**，只返回 advisory
  `platform_host_unverified`，行仍然可见。真实雇主不从标题等自由文本推断。
- **`company_missing`**：缺失雇主是**可修复**问题 → `needs_review`（存库、隐藏、进复核队列），
  不再整单 `400`；`reject`（`400` + 零写入）只用于无法安全处理的输入。
- **`description` 契约（权威）**：可选，缺失不是异常；上限为 **49,152 字节（48 KiB）
  UTF-8 字节**，按字节判定（C++ `std::string::size()`、Python
  `len(value.encode("utf-8"))`），**从不截断**：超限返回 `400` + `description_too_large`，
  `data.diagnostic` 带 `limit_bytes`/`actual_bytes`。`jobs.description` 仍为 `TEXT`
  （65,535 字节容量，列不变），该容量只是存储上限，不是应用上限。示例：

```json
{"code":"description_too_large","field":"description",
 "reason":"description is 49153 bytes, over the 49152 byte cap (not truncated)",
 "limit_bytes":49152,"actual_bytes":49153}
```

- **平台名注册表**：唯一来源是
  `source_adapters/jobpilot_sources/data/platform_names.yaml`，Python 与 C++ 共读同一文件；
  C++ 侧路径解析顺序为环境变量 `JOB_QUALITY_PLATFORM_NAMES_FILE` → 仓库相对默认路径
  （Python 侧另有打包副本回退，见 `source_adapters`）。规范化 = ASCII 去首尾空白 +
  A–Z 折叠 + 反复剥离后缀 `招聘网/人才网/官网/招聘/网`；未登记的名称一律 fail-open
  （不触发复核）。客户端/pre-flight 检查只是 UX 提示，**服务端判定才是权威**。

- `jobs` 数量必须为 `1..100`（手工批次上限；来源观测批次见 5.5）。
- `source_url` 必填，并以唯一索引做幂等键。
- 同一 `source_url` 再次导入时更新已有岗位，不创建重复岗位。
- `requirements` 采用整组替换，旧数据不会残留。
- 整个 batch 使用单一事务，任一条失败时全部回滚。
- `INTERNAL_API_TOKEN` 仅从环境变量读取。
- 当前没有 Gateway，该 token 只是应用层保护，不等同于网络隔离。
- 错误响应继续使用统一 `{code,message,data}` 格式。
- Greenhouse / Lever / Ashby 等 Source Adapter 不使用本接口，统一走 5.5 的
  `/internal/jobs/source-sync`；任何适配器都不允许直接写业务数据库。

0014 增量请求键（可选；**人工采集路径用 `source="manual"`**）：

- `education_required`：整数 `1..5`（`1不限/2大专/3本科/4硕士/5博士`）。
- `experience_years_min` / `experience_years_max`：非负整数（`SMALLINT` 上限
  `32767`），两者同时存在时必须 `min <= max`。
- `job_nature`：整数 `1..3`（`1校招/2社招/3实习`）。
- `deadline`：`"YYYY-MM-DD HH:MM:SS"`，**UTC 瞬时**；
  `deadline_date_only`：布尔，`true` 表示来源只给了日期。
  来源只给日期时，调用方按 [`docs/03` §9.1](03-database-design.md) 存该日期的
  北京本地正午（`04:00:00` UTC）；`deadline_date_only=true` 时 `deadline` 的
  UTC 时间部分必须是 `04:00:00`。来源给了具体时刻则保留真实瞬时、
  `deadline_date_only=false`（或不传，缺省等价于 `false`）。
  省略 `deadline` 时 `deadline_date_only` 也必须省略/为 `null`。
- 非法值（枚举越界、负年限、`min > max`、非法 `deadline`、`deadline_date_only`
  与 `deadline` 不一致）一律返回 `400`（`code=10001`），
  **不静默丢弃、不钳制、不截断**；未提供或无法确定的键一律省略（不写 `1` 兜底）。
- 人工路径**整组覆盖**：再次导入同一 `source_url` 时新字段按本次请求写入
  （未提供即为 `NULL`），沿用 5.4 既有的整组替换语义。
- 人工采集的记录在冲突中**胜出**：同一 `source_url` 已被来源观测过时，
  人工导入照常覆盖该行（含把 `source` 改为 `manual`），
  此后 5.5 的来源观测不得再覆盖它（F14，见 5.5）。

### 5.5 岗位来源同步（Source Sync，0012）

该接口不属于公共 `/api/v1/*` API，仅供可信 Source Adapter 上报一次来源观测：

```
POST /internal/jobs/source-sync
X-Internal-Token: <environment secret>
```

请求：

```json
{
  "source": "greenhouse",
  "source_scope": "example-company",
  "complete": true,
  "observed_at": "2026-10-08 12:00:00",
  "jobs": []
}
```

- `source` 必填，来源类型标识，必须落在 C++ allowlist
  `IsSupportedObservationSource` 内（当前 `greenhouse` / `lever` / `ashby` /
  `nowcoder`；0018 按 2026-10-19 的实测登记加入 `nowcoder`，其余 China 来源仍未注册）。
- `source_scope` 必填，board / scope 标识，用于隔离不同 board 的缺失判定。
- `complete=true` 表示适配器能证明本次已观测该 scope 的完整岗位集合。
- `observed_at` 必填，本次观测时间。
- `jobs` 为归一化岗位数组，单个岗位对象字段与 5.4 一致。

响应：

```json
{
  "created": 10,
  "updated": 2,
  "seen": 12,
  "closed": 0,
  "complete": true,
  "skipped_manual": 1,
  "skipped_manual_urls": ["https://example.com/job/1"],
  "items": [
    {
      "index": 0,
      "source_url": "https://example.com/job/1",
      "review_status": 0,
      "rejected": false,
      "findings": []
    }
  ],
  "needs_review": 0,
  "rejected": 0,
  "clean": 12
}
```

- 0015A：本接口与 5.4 使用**同一个**确定性判定函数与同一组响应字段
  （`items[]` / `findings` / `needs_review` / `rejected` / `clean`），
  `reject` 同样返回 `400` + `data.diagnostic{code,field,reason}` 且不写入任何行。
  判定在写库前完成：被拒绝的观测不会打开事务，因此也不会刷新 freshness 或关闭岗位。
  结构化适配器（`greenhouse`/`lever`/`ashby`/`nowcoder`）只可能命中硬缺陷码，见 `docs/03` §9.3。
- 0020：job 对象与 5.4 同形，接受可选的 `apply_url`；校验与
  `COALESCE(VALUES(apply_url), apply_url)` 语义同 5.4。provider 未拿到显式申请链接时
  省略该键，服务端保留已有值。

批量与完整性约束：

- `jobs` 数量：`complete=true` 时允许 `0..200`（`0` 表示空 board）；`complete=false` 时允许 `1..200`。
- 这是完整 board 观测批次，与 5.4 手工批次的 `1..100` 是两套独立规则。
- `complete=false` 时服务端只更新实际观测到的岗位：不递增缺失计数、不关闭任何岗位。
- board 超过 200 个岗位时，适配器必须发送 `complete=false` 并分批上报，同样不得关闭缺失岗位。

生命周期语义（依据 ADR-0010 与 ExecPlan 0012）：

- 完整观测且岗位存在：刷新 `last_seen_at` / `last_verified_at`，`missing_count=0`，
  `source_status='ok'`，`status=1`，重开时清空 `closed_at`。
- 完整观测且岗位缺失：`missing_count += 1`，`source_status='missing'`；仅当
  `missing_count >= 2` 或超过来源宽限期（ATS 默认 48h）才关闭
  （`closed_at` 置位、`status=0`、`source_status='closed'`）。
- 不完整观测（分页截断 / 限流 / 超时 / 结构不确定）：不递增缺失计数，不关闭任何岗位。
- 一次观测使用单一事务，任一条失败时全部回滚。
- `source_url` 仍是唯一幂等键，本接口不改变 `uk_source_url`。
- `INTERNAL_API_TOKEN` 仅从环境变量读取；当前无 Gateway，该 token 只是应用层保护。
- 错误响应继续使用统一 `{code,message,data}` 格式。

0014 增量（F13 deadline 语义 + F14 人工记录胜出）：

- 观测的每个 job 对象与 5.4 同一结构，因此同样接受 `education_required`、
  `experience_years_min/max`、`job_nature`、`deadline`、`deadline_date_only`；
  `deadline` 的格式与时区权威约定见 [`docs/03` §9.1](03-database-design.md)
  （UTC 存储、Beijing 语义、date-only 用北京正午存储约定），非法值返回 `400`。
- 观测路径对本次未提供的源侧新字段使用 `COALESCE(VALUES(x), x)` 保护：
  本次观测没有带 `deadline` 时**不得抹掉**该行已观测到的 `deadline` 与
  `deadline_date_only`；人工路径（5.4）相反，是整组覆盖。
- **F14 人工记录胜出**：若某个观测 job 的 `source_url` 命中一行
  `source='manual'` 的记录，服务端**跳过**该观测（不更新任何列、不改 `source`、
  不动 freshness/`closed_at`/`missing_count`、不替换其 `job_requirements`），
  并在响应里记为 `skipped_manual` 计数 + `skipped_manual_urls` 明细
  （同时写一条服务端日志）。该行因此**不会**进入本次观测的
  `(source, source_scope)` 缺失判定，也不会被
  `CloseUnseenForScope` 关闭。反向方向不变：人工导入（5.4）可以覆盖观测行，
  这是「人工胜出」的另一半。
- `deadline` 不参与生命周期：过期与否都不触发关闭（见 5.1 的可见性限制）。

实现说明：适配器代码位于 `source_adapters/`（Greenhouse / Lever / Ashby / nowcoder，
fixture 驱动，可选 live smoke）；配置、运行方式与归一化规则见
[`source_adapters/README.md`](../source_adapters/README.md)。

#### 5.5.1 `nowcoder`（0018，中国校招公开列表）

- **来源**：`https://www.nowcoder.com/jobs/school/jobs?search=<关键词>&city=<中文城市>`；
  页面是免登录 SSR，内嵌 `window.__INITIAL_STATE__`，岗位数组在递归找到的
  `jobListData`（模块号随构建变化，不得硬编码）。`/jobs/*` 未被 robots.txt 禁止；
  `/search`、`/nccommon`、`/ab/ab-test-flow` 被禁止。
- **board 规格**：`<keyword>` 或 `<keyword>|<city>`（如 `后端`、`C++|深圳`）。
  `source_scope` 就是该 board 字符串；多个 board 可在 `sources:` 列表中并列配置，
  一次 run 顺序处理，provider 级最小间隔 5 秒；每 board 只发一次列表请求，不翻页、
  不请求详情。建议一次 run 不超过 3 个 board。
- **完整性**：一次查询最多返回 20 条，且没有可用翻页；结果顺序在两次请求间可能抖动。
  因此 `complete` **必须为 `false`**。payload、文档、运行输出三处必须一致；不得把
  一次 20 条窗口描述为“全量扫描”。`complete=false` 时服务端不关闭任何岗位。
- **空结果**：`jobListData: []` 是合法空结果；适配器不发布（`complete=false` 且
  `jobs` 为空时没有合法 payload）。
- **两种记录形态**：
  - 原生岗：id 为 5–6 位；`jobName` / `companyNameText` / `jobCity` / `salaryText`；
    `ext` 是 JSON 字符串，含 `requirements` + `infos`（JD）。
  - 内推聚合岗：id 为 12+ 位；`jobTitle` / `companyName` / `salary` / `description`
    （HTML 内联 JD）；`platform` 可能是 `moka` 等；`city` 可能为空。
- **字段约定**：`source_url = https://www.nowcoder.com/jobs/detail/<id>`；
  `external_id = str(id)`；`source = nowcoder`；`source_scope = board`。
  聚合岗详情 SSR 里的 `jobDetailUrl`（外部 Moka 申请链接）**本阶段不落库**；
  `apply_url` 仍不存在。未知 `eduLevel` 编码、歧义薪资、缺失城市一律省略，不猜测。
- **去重**：同一 board 批次内按规范化 `title + company + location` 去重；跨 board /
  跨来源 canonical 去重不在本阶段（`uk_source_url` 仍只保证同 URL 幂等）。
- **频率与停止条件**：诚实 UA、低频、无并发；遇到 403/429/登录墙/验证码/结构异常
  停止，不绕过、不代理轮换、不复用登录 cookie。
- **合规边界**：robots 允许 + 免登录是工程前提，不是法律意见；ToS 接受由 owner 记录。

### 5.6 岗位复核（Review Queue，0015A）

三个**受保护**的内部端点，访问控制只复用既有的 `X-Internal-Token`
常时比较机制（无角色系统）；`reviewer` 只是审计标签，**不是认证身份**，token
永不写入日志（有 log-scan 测试证明）。错误沿用统一 `{code,message,data}` 信封。

```text
GET  /internal/jobs/review-queue?status=needs_review|quarantined|clean&page=&page_size=
POST /internal/jobs/review/{job_id}   {"decision":"approve"|"fix"|"reject","reviewer":"<label>","note":"...","fix":{...}}
POST /internal/jobs/review/backfill?mode=dry_run|apply|revert
```

队列响应（200）：

```json
{
  "items": [
    {"job_id": 1024, "title": "C++后端开发", "company": "某公司",
     "location": "深圳", "source_url": "https://example.com/job/1",
     "source": "manual", "description_excerpt": "…",
     "review_status": 1, "reason_code": "employer_is_platform",
     "reason_text": "employer equals a registered platform name",
     "outcome": "needs_review", "resolution": "open",
     "created_at": "2026-10-10 10:00:00"}
  ],
  "total": 1, "page": 1, "page_size": 20
}
```

复核响应（200）：`{"job_id":1024,"review_status":0,"decision":"approve",
"reviewer":"ops","note":"...","idempotent":false,"changed_fields":[]}`。

状态机（ExecPlan 0015A §6.2）：

- `approve` / `fix` → `review_status = 0`（公共可见）；`reject` → `2`（永久隐藏，**行保留**用于审计）。
- 重复同一决定 → 幂等 `200`（`idempotent=true`，不改变任何状态）；
  与已记录决定冲突 → `409`（`code=40005`）。
- `reject` 必须带非空 `note`；`fix` 的字段按 §5.5 重新校验（公司/标题 ≤128 字节且非空、
  `source_url` 为绝对 `http(s)` 且 ≤255 字节、`description` ≤49,152 字节、公司名不得
  规范化成注册表平台名）。校验失败 → `400` + `data.diagnostic{code,field,reason}`，
  **不改变任何状态**。
- `fix` 为每个变更字段写入 before/after 审计行
  （`job_review_items.reason_code='review_fix_<field>'`，`reason_text` 形如
  `company: 旧值 => 新值`，`resolution='fixed'`）；同一次决定会把该行所有未决发现
  标记为 `approved`/`fixed`/`rejected`，并记录 `resolved_by`/`resolved_at`。

历史分类（§5.4.2，可审计、可回滚）：

- `mode=dry_run` **只读**：返回 `active_jobs`、`scanned`、`would_flag`、
  `hidden_share_percent`、`per_reason`（按 reason code 计数）、有界 `sample`、
  `threshold_percent=5` / `threshold_rows=100` / `threshold_exceeded`，以及
  `before_needs_review` 与 `after_needs_review` 投影；**不写任何行**。
- 只分类当前 `review_status = 0` 的活跃行，因此**不会覆盖**已复核/已隔离的行，
  也使 `mode=revert` 能精确恢复 apply 前的分布。
- `mode=apply` 只在 owner 显式授权后执行；`mode=revert` 把 backfill 写入的未决行
  （`resolved_by='backfill'`、`resolution='open'`）恢复为 `review_status = 0` 并删除这些行。
  三种模式都不删除岗位行。

---

## 6. 投递接口

### 6.1 创建投递（收藏/待投递起点）

```
POST /api/v1/applications
```

```json
{ "job_id": 20001, "resume_version_id": 30012 }
```

```json
{
  "code": 0,
  "message": "success",
  "data": { "application_id": 40001, "status": 0 }
}
```

`resume_version_id` 为必填项，且对应版本必须属于当前登录用户；`job_id`
必须对应启用中的岗位。重复投递返回 `40004`。

### 6.2 投递列表

```
GET /api/v1/applications?status=2&page=1&page_size=20
```

```json
{
  "code": 0,
  "message": "success",
  "data": {
    "list": [
      {
        "id": 40001,
        "job_id": 20001,
        "job_title": "C++服务器开发",
        "company": "某公司",
        "status": 2,
        "apply_time": "2026-08-10 09:00:00",
        "remark": null
      }
    ],
    "total": 10,
    "page": 1,
    "page_size": 20
  }
}
```

`status` 可选；`page >= 1`；`1 <= page_size <= 100`。列表仅返回当前登录
用户的投递记录，并按 `update_time DESC, id DESC` 排序。

### 6.3 更新投递状态

```
PUT /api/v1/applications/{id}/status
```

```json
{ "status": 3 }
```

状态机：`0收藏 → 1待投递 → 2已投递 → 3笔试 → 4一面 → 5二面 → 6Offer`；任意非终态可到 `7拒绝`。非法迁移返回 `40005`。

### 6.4 投递统计

```
GET /api/v1/applications/statistics
```

```json
{
  "code": 0,
  "message": "success",
  "data": { "total": 50, "interview": 5, "offer": 1, "rejected": 12 }
}
```

统计范围仅限当前登录用户：`total` 为全部投递，`interview` 为状态
`3/4/5`，`offer` 为状态 `6`，`rejected` 为状态 `7`。

> 注：`statistics` 是静态路径，路由必须在 `{id}` 参数路由之前注册。

---

## 7. AI 接口（异步）

### 7.1 岗位匹配

```
POST /api/v1/ai/match
```

```json
{ "job_id": 20001, "resume_id": 30001 }
```

```json
{
  "code": 0,
  "message": "success",
  "data": { "task_id": 1024 }
}
```

### 7.2 简历优化 / 面试题生成

```
POST /api/v1/ai/resume/optimize   body: {"job_id":20001,"resume_id":30001}
POST /api/v1/ai/interview/generate body: {"job_id":20001,"skills":["C++","Linux"]}
```

均返回 `{ "task_id": N }`。

### 7.3 任务查询（轮询）

```
GET /api/v1/ai/tasks/{task_id}
```

```json
{
  "code": 0,
  "message": "success",
  "data": {
    "task_id": 1024,
    "task_type": "job_match",
    "status": 2,
    "result": {
      "score": 82,
      "strengths": ["熟悉C++/Linux", "网络编程经验"],
      "gaps": ["缺少分布式经验"],
      "suggestions": ["补充高并发项目"]
    },
    "error_msg": null,
    "created_at": "2026-08-09 10:00:00"
  }
}
```

- `status=0/1`：前端继续轮询（建议间隔 2s）
- `status=3`：展示 `error_msg`，可重试

AI Provider 采用 BYOK：

- 核心搜索、排序、岗位入库与投递跟踪不依赖 AI。
- 支持 OpenAI-compatible、DeepSeek、Qwen、Ollama。
- Provider key 不进入 Git。
- LLM 失败时先降级到 Rule / Embedding 结果，并明确标记 degraded。
- MVP 不要求 RabbitMQ；任务通过 C++ AI Task 状态机与 Python Worker 轮询。

---

### 7.4 0013 任务模型、结果字段与降级

`ai_tasks` 的 `status` 沿用 §1.5：`0 pending / 1 processing / 2 success / 3 failed`。
0013 起响应额外携带两个字段：

- `rule_result`：创建时写入的**确定性基线**（`match_rule_v1`，见 §5.3 的分量口径），
  永远不会被 AI 覆盖；客户端可先展示它。
- `result`：最终结果；任务进入终态时才有值。

降级规则（核心不得依赖 AI）：

- `ai.enhancement_enabled=false`（默认）：发起即完成（`status=2`，`result == rule_result`），
  **不需要 Python worker**。
- `ai.enhancement_enabled=true`：创建为 `pending`，由 worker 认领并增强。
- provider 未配置、超时、限流、返回非法 JSON 时：worker 以基线完成并标记
  `ai.degraded=true` 与固定 `ai.reason`；越权载荷（试图改写分数）整体拒绝并同样降级。
- **权威分数**：AI 只能追加 `explanation` / `suggestions` / `ai.*`，不得覆盖
  `score`、`breakdown`、`matched_skills`、`missing_skills`。

幂等：同一 `(user_id, job_id, resume_id)` 已有非终态任务时直接返回既有 `task_id`。

错误码：岗位不存在/未启用 `40001`；简历不存在或不属于当前用户 `40002`；
任务不存在或不属于当前用户 `50001`（两种情况同码，避免泄露存在性）。

---

## 8. 健康检查

```
GET /health
```

```json
{ "code": 0, "message": "success", "data": { "status": "ok" } }
```

用于 docker 健康检查与负载均衡探活。

---

## 9. 接口清单汇总

| 方法 | 路径 | 鉴权 | 说明 |
|---|---|---|---|
| POST | /api/v1/users/register | 否 | 注册 |
| POST | /api/v1/users/login | 否 | 登录 |
| GET | /api/v1/users/me | 是 | 当前用户 |
| GET/PUT | /api/v1/users/profile | 是 | 画像 |
| GET/POST | /api/v1/users/skills | 是 | 技能 |
| DELETE | /api/v1/users/skills/{id} | 是 | 删除技能 |
| POST | /api/v1/resumes/upload | 是 | 上传简历 |
| GET | /api/v1/resumes | 是 | 简历列表 |
| POST | /api/v1/resumes/{id}/versions | 是 | 新版本 |
| GET | /api/v1/resumes/{id}/versions | 是 | 版本列表 |
| POST | /api/v1/resumes/artifacts | 是 | 生成定制简历草稿（D5） |
| GET | /api/v1/resumes/artifacts | 是 | 定制产物列表（D5） |
| GET | /api/v1/resumes/artifacts/{id} | 是 | 定制产物详情（D5） |
| POST | /api/v1/resumes/artifacts/{id}/confirm | 是 | 人工确认（仅状态变更，不发送） |
| GET | /api/v1/jobs | 否 | 岗位搜索 |
| GET | /api/v1/jobs/recommended | 是 | 个性化岗位推荐 |
| GET | /api/v1/jobs/{id} | 否 | 岗位详情 |
| POST | /api/v1/applications | 是 | 创建投递 |
| GET | /api/v1/applications | 是 | 投递列表 |
| PUT | /api/v1/applications/{id}/status | 是 | 更新状态 |
| GET | /api/v1/applications/statistics | 是 | 统计 |
| POST | /api/v1/ai/match | 是 | 岗位匹配(异步) |
| POST | /api/v1/ai/resume/optimize | 是 | 简历优化(异步) |
| POST | /api/v1/ai/interview/generate | 是 | 面试题(异步) |
| GET | /api/v1/ai/tasks/{task_id} | 是 | 任务轮询 |
| GET | /health | 否 | 健康检查 |
| POST | /internal/jobs/import | Internal Token | 可信岗位导入 |
| POST | /internal/jobs/source-sync | Internal Token | 来源观测同步（Source Adapter） |

> 内部接口（AI 服务取简历/岗位、Source Adapter 上报观测）为 `/internal/*`，仅内网可达，不对外暴露。

## 10. Open-source MVP 边界

以下接口不属于 MVP：

- AutoApply 或自动登录招聘平台。
- SaaS 计费 / 套餐 / platform-funded AI key。
- 多租户 RBAC / Admin 管理面。
- 通用浏览器 Crawler。
- RabbitMQ 管理接口。

后续如果增加岗位搜索筛选，只允许在当前 `GET /api/v1/jobs` 契约上评审扩展
`skill`、`salary_min`、`salary_max` 等过滤条件；`work_mode` 和
`employment_type` 需要先确认数据模型。

### 10.1 0011 搜索约定

- Hard Filter 与 Soft Ranking 分离。
- Hard Filter 只决定是否进入结果集。
- Soft Ranking 只对已通过 Hard Filter 的 Job 计分。
- 同一条件不得先作为硬排除、再在 score 中重复惩罚。
- 排名必须包含 `score_version` 和可解释 breakdown。
- 0011 不调用 LLM、Embedding 或 Search Engine。

### 10.2 Freshness 契约（0012）

写入路径是 5.5 的 `/internal/jobs/source-sync`；公开响应字段与可见性规则已在
0012 Step 6 对 `GET /api/v1/jobs` 与 `GET /api/v1/jobs/recommended` 生效
（字段、默认过滤与 `include_stale` / `posted_within_days` 见 5.1）：

```text
posted_at
first_seen_at
last_seen_at
last_verified_at
freshness
source_status
ghost_score
```

支持：

```text
include_stale=true
posted_within_days=N
```

`ghost_score` 只表达风险，不代表岗位关闭。

### 10.3 Internal AI API 预留

AI Worker 通过内部 API 操作任务，不直接访问业务表：

```text
POST /internal/ai/tasks/claim
POST /internal/ai/tasks/{id}/complete
POST /internal/ai/tasks/{id}/fail
GET  /internal/ai/tasks/{id}/input
```

0013 起正式定义如下（与 `/internal/jobs/source-sync` 同一信任边界：
`X-Internal-Token`，缺失或错误返回 `401`）：

```text
POST /internal/ai/tasks/claim         {"worker_id","lease_seconds"?}
  -> {"task":{"task_id","task_type","job_id","resume_id","retry_count"}|null,
      "requeued_stale":N,"expired_failed":N}
POST /internal/ai/tasks/{id}/complete {"worker_id","result":{...}}  -> {"status":2}
POST /internal/ai/tasks/{id}/fail     {"worker_id","error_msg","retryable":bool} -> {"status":N}
GET  /internal/ai/tasks/{id}/input?worker_id=...
  -> {"task":{...},"job":{...含 requirements...},"profile":{...},
      "skills":[...],"resume":{"resume_id","version","content"},"rule_result":{...}}
```

规则：

- `claim` 在**一个事务**内完成：先回收租约过期且未达尝试上限的任务，再把已达上限的
  过期任务置为 `failed`，然后原子取最旧的 `pending` 任务（`status=0` 中 `id` 最小），
  置为 `processing` 并写 `locked_by`、`lease_expire_at`、`retry_count+1`。
  两个并发 worker 不可能拿到同一任务。
- `complete` / `fail` / `input` 必须 owner 匹配且租约有效，否则 `50001`。
- `fail` 且 `retryable=true` 且未达 `ai.max_attempts` 时回到 `pending`；否则终态
  `failed` 并记录 `error_msg`。
- `retry_count` 表示**已尝试次数**，由 `claim` 递增；`fail` 只更新状态与
  `error_msg`，不再递增计数。
- worker **不得直接访问业务表**（ADR-0009）：其输入全部来自 `/input`。
- 默认 `lease_seconds=120`、`max_attempts=3`，由 `ai.lease_seconds` /
  `ai.max_attempts` 配置。

---

## 8. 只读岗位匹配报告（0015 阶段 C，决策 2 = C1）

**访问模型**：离线、自包含单文件报告；**不新增服务端接口**、不新增鉴权机制、不引入前端
构建工具、不新增依赖（仅 Python 标准库）：

```bash
# 离线模式（零网络 I/O、不需要会话）：渲染已抓取的响应体
python3 tools/job_report/render_report.py --input captured_jobs.json --output job_report.html

# 联网模式：用既有 Bearer token 只发一次只读 GET
python3 tools/job_report/render_report.py \
  --url 'http://127.0.0.1:8080/api/v1/jobs?with_match=true&page_size=50' \
  --token "$JOBPILOT_TOKEN" --output job_report.html
```

- **数据来源**：`GET /api/v1/jobs?with_match=true`（§5.1 的增量键；`strict=true` 时用其过滤后
  的结果）或该响应的离线副本。报告**只渲染 API 已经返回的内容**——不重新打分、不重新排序、
  不补数据（C1 的唯一契约）。
- **确定性排序**：行的顺序 = 响应 `list` 的顺序（即 `sort=`/评分顺序），`rank` 为位置序号 1..N；
  生成器从不重排。可选 `--sort-basis "<文本>"` **只在报告头部记录**排序基准（默认
  `API 返回顺序（未重排）`），工具不解析也不应用它。
- **每行字段**：`rank`、`score` + `score_version`、`matched_skills`、`missing_skills`、
  `reasons[]`（`code`/`text`/`evidence`）、`deal_breakers[]`、`deadline_urgency`、
  **原始岗位链接 `source_url`**（§5.1 已暴露）、公司/职位/地点/`freshness`。
- **显式缺失语义**：无 `reasons[]` 的行渲染 `无匹配理由`（绝不空白）；无 `deal_breakers[]` 渲染
  `无`；无 `source_url` 渲染 `—`；空列表渲染显式空态。
- **自包含**：单个 HTML 文件，内联 CSS，**无外部资源、无脚本**。
- **只读**：`--input` 模式零网络 I/O；`--url` 模式仅一次 `GET`（无 POST/PUT/DELETE），
  不访问数据库，不改任何状态。缺少数据时报告显式留白，绝不触发写路径。
- 生成方式与内容说明见 `tools/job_report/README.md`；测试见
  `tools/job_report/tests/test_render_report.py`。

---

## 9. 简历定制产物（0015 阶段 D，决策 1 = 仅简历；决策 3 = 确定性草稿优先）

**生成契约（无 AI key 也能出草稿）**

- 草稿由 C++ 侧**确定性**产生：`generator='rule_v1'`、`source_version='tailor_rule_v1'`
  （ADR-0009：不能因为没有 API key 就没有产物）。
- 可选 worker/LLM 通道**只允许增加措辞**：不得新增/修改事实、不得影响分数或生命周期，
  规则草稿在 worker 失败时依然存在（0013 的 `rule_result` 同款约束）。
- 本阶段只产出**简历**（`artifact_type='resume'`），不做 cover letter。

**事实来源标注（每条断言必须可回溯）**

草稿中每一条被断言的事实都带来源标注，取值集合固定：

| 来源标注 | 含义 |
|---|---|
| `profile:<field>` | 画像字段（`target_position` / `location` / `salary` / `education_level` / `graduation_year` / `campus_status` / `summary` / `name` / `email`） |
| `skill:<name>` | 技能记录（`skills` 表） |
| `requirement:<skill>` | 岗位要求（`job_requirements`） |
| `job:<field>` | 岗位字段（`title` / `company`） |
| `resume_version:content` | 选定简历版本的正文（逐字引用） |

- **无法从来源导出的内容一律是占位符** `【请填写：<字段>】`，由人工补全；**绝不编造**
  （无姓名就写占位符，不生成假姓名/假电话/假年份）。
- 缺失的岗位要求技能以「岗位要求待补充」陈述，属**岗位要求**这一来源的事实，不是对候选人的否定。

**机械事实门（D4，已落地）**

`FactCheck(draft)` 是纯函数，判定规则机械且逐行可复现：

1. 每条 fact 的来源前缀必须在允许集合内，且其 `text` 必须**出现在草稿正文**中（否则为
   断裂的来源链）；
2. 草稿正文的每一行必须属于以下之一：空行、小节标题（`【…】`）、显式占位符
   （`【请填写：…】`）、**逐字引用块内部**（由 `原始简历文本（逐字引用）：` 开始、由
   `【原始简历文本结束】` 结束）、或**被来源事实完全覆盖**（先按文本长度从长到短擦除 fact 文本，
   仅允许剩余标签/分隔符）；
3. 每个记录的占位符必须在正文中渲染为对应 slot。

任何一条不成立即失败：`fact_check_status=2`，`fact_check_report` 记录
`unsourced_claims` / `unknown_sources` / `missing_facts` / `missing_placeholders`
（JSON 列，读回时按 JSON 语义取值，不做字节比较）。**失败阻止确认**（`Confirm` 的 SQL 带
`fact_check_status <> 2`），并有测试覆盖：把一条虚构断言追加到草稿 → 门失败 → 确认被阻止。
引用块的显式结束标记使「在引用块之后追加断言」的措辞润色也无法绕过门。

**人工确认边界（硬）**

- `confirmed_at IS NULL` = 草稿；确认后仍是「已关联（岗位/投递）」，**从未被发送**。
- 0015 **不新增任何提交/投递接口**：没有 POST 到外部、没有自动投递、没有后台代投。
- 确认只做状态变更，不触发任何发送行为。

**ATS 格式契约（中文）**

- UTF-8 纯文本优先；固定小节顺序与稳定标题（`【个人信息】`/`【求职意向】`/`【技能】`/
  `【教育背景】`/`【自我评价】`/`【职位匹配说明】`/`【原始简历文本】`）；
- 不使用表格排版、不使用制表符/竖线分隔、无图片内容；渲染版式（PDF/DOCX）属可选外部工具，
  不在核心内（Out-of-scope 9）。

**路由契约（D5 已落地，四个鉴权接口）**

| 方法 | 路径 | 说明 |
|---|---|---|
| POST | /api/v1/resumes/artifacts | 由选定简历版本生成定制草稿（不写 `resume_versions`） |
| GET | /api/v1/resumes/artifacts | 当前用户的产物列表 |
| GET | /api/v1/resumes/artifacts/{id} | 单个产物（含草稿正文与事实门报告） |
| POST | /api/v1/resumes/artifacts/{id}/confirm | 人工确认（仅状态变更，不发送） |

- `POST /api/v1/resumes/artifacts` 请求体：`base_version_id`（必填）、`job_id` /
  `application_id`（可选，均须属于当前用户）。整个生成过程在一个事务内完成：版本归属校验 →
  读取该版本正文 → 画像 + 技能 → **仅当给出 `job_id` 时**读取一次启用岗位及其要求 →
  确定性草稿 → 机械事实门 → 落库；产物固定 `generator='rule_v1'`、
  `source_version='tailor_rule_v1'`，`fact_check_status` 与 `fact_check_report` 原样持久化。
  该路径只**读** `resume_versions.content`，**从不写回**简历版本内容。
- 每个产物响应（创建 / 列表 / 单个 / 确认）恒含 `confirmed`（bool）、`confirmed_at`
  （`null` = 草稿）、`fact_check_status`（1 通过 / 2 失败）与 `fact_check_report`。
- 错误语义：非本人简历版本或产物 → `40002`（HTTP 404，不泄露存在性）；`job_id` 不存在或
  未启用 → `40001`；`application_id` 不属于当前用户 → `40003`（同为 404）；对
  `fact_check_status=2` 或已确认产物的再次确认 → `409`（`kInvalidStatusTransition`）；
  未带 token → `401`。
- **姓名/联系方式占位（如实记录，不是缺陷）**：`IUserProfileReader` 只暴露画像与技能，
  不含姓名、邮箱等联系方式字段（`profile:name` / `profile:email` 两个来源标注在当前接线中
  拿不到值）。因此草稿的姓名与联系方式一律渲染为 `【请填写：姓名】` /
  `【请填写：联系方式】`，由人工补全；**宁可留占位符也不编造候选人信息**。
- **确认边界**：`confirmed_at IS NULL` = 草稿，确认后仍是「已关联（岗位/投递）」，**从未被发送**；
  不存在 `POST /api/v1/resumes/artifacts/{id}/submit`（探针返回 404），0015 不新增任何提交/
  自动投递路径。

**当前落地状态**：D2 已落地为**纯函数** `BuildTailorDraft`
（`service/include/jobpilot/service/resume/tailor_rule.h`，测试 `test_tailor_rule`）；
持久化（D3，`resume_artifacts`，测试 `test_resume_artifact_dao`）、机械事实门（D4）、
确认边界与上述四个读取/确认路由（D5，测试 `test_resume_http`）均已落地。
