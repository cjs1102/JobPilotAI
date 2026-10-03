# JobPilot AI API 接口设计文档 V2.0

> 依据架构 V3.0 编写。本文档为**接口契约唯一权威依据**，前端/后端/AI 三方以此为准。所有接口走 Nginx 网关（`/api` 前缀）转发至 C++ Backend。

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
GET /api/v1/jobs?keyword=C%2B%2B&location=深圳&page=1&page_size=20
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
        "source": "import"
      }
    ],
    "total": 42,
    "page": 1,
    "page_size": 20
  }
}
```

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
    "requirements": ["C++", "Linux", "Redis"],
    "source_url": "https://...",
    "created_at": "2026-08-09 10:00:00"
  }
}
```

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

> 注：`statistics` 为静态路径，网关/路由需在 `{id}` 参数路由之前注册。

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

> 内部接口（AI 服务取简历/岗位、Crawler 入库）为 `/internal/*`，仅内网可达，不对外暴露。
