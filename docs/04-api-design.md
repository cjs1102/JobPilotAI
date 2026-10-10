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
  "summary": "熟悉C++、Linux、网络编程"
}
```

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
  "data": { "resume_id": 30001, "version": 1 }
}
```

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
  "data": { "version_id": 30012, "version": 4 }
}
```

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
      { "id": 30012, "version": 4, "created_at": "..." }
    ]
  }
}
```

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
- `skill` 为精确技能过滤，匹配 `job_requirements.skill`。
- 公共搜索结果按 `create_time DESC, id DESC` 稳定排序。

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
    "created_at": "2026-08-09 10:00:00"
  }
}
```

- 详情响应与 5.1 一致地包含 0012 freshness 字段（`posted_at`、`first_seen_at`、
  `last_seen_at`、`last_verified_at`、`freshness`、`source_status`）。
- 详情响应同样包含 5.1 的 0014 增量字段（`education_required`、
  `experience_years_min/max`、`job_nature`、`deadline`、`deadline_date_only`）。

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
      "education_required": 3,
      "experience_years_min": 0,
      "experience_years_max": 3,
      "job_nature": 1,
      "deadline": "2026-10-08 04:00:00",
      "deadline_date_only": true,
      "requirements": [
        { "skill": "C++", "level": 3 },
        { "skill": "Linux", "level": 2 }
      ]
    }
  ]
}
```

响应：

```json
{
  "created": 1,
  "updated": 0,
  "total": 1
}
```

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
  `IsSupportedObservationSource` 内（当前 `greenhouse` / `lever` / `ashby`；
  0014 的 China 来源标识按计划 Step 3 在实测登记后加入，本阶段不预先扩大）。
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
  "skipped_manual_urls": ["https://example.com/job/1"]
}
```

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

实现说明：适配器代码位于 `source_adapters/`（Greenhouse / Lever / Ashby，
fixture 驱动，可选 live smoke）；配置、运行方式与归一化规则见
[`source_adapters/README.md`](../source_adapters/README.md)。

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
