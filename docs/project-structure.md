# JobPilot AI 项目目录结构与模块职责

> 依据 ADR-0001 ~ 0005 与 `02-system-architecture.md` 生成。目录即架构：依赖只能从上层流向下层，禁止反向引用。

---

# 1. 顶层目录

```
JobPilotAI/
├── backend/      # C++ 核心框架层（静态库，业务无关）
├── service/      # C++ 业务服务层（模块化单体，产出 jobpilot_server 可执行文件）
├── gateway/      # API 网关（Nginx 配置 + 网关规则说明）
├── ai_service/   # Python AI Service（FastAPI + MQ 消费者；目录名为 ai_service，V1.1 与 07/08 统一）
├── crawler/      # Python 爬虫服务（已从 MVP 范围移除，见 08 §2.2）
├── database/     # SQL 建表脚本、初始化数据、ER 图
├── frontend/     # Vue3 前端
├── docs/         # 文档体系（含 adr/）
├── docker/       # Dockerfile 与 docker-compose
├── dataset/      # 示例岗位数据集（公开数据/用户导入样本）
├── tests/        # 跨服务集成测试
└── README.md
```

## 1.1 六大核心目录职责速览

| 目录 | 语言 | 产物 | 核心职责 |
|---|---|---|---|
| `backend/` | C++ | `libjobpilot_core.a` | 网络/HTTP/数据库连接池/缓存/日志/配置 —— 业务无关的“引擎” |
| `service/` | C++ | `jobpilot_server` | 用户/简历/岗位/投递/AI任务 五个业务模块 —— 业务本身 |
| `gateway/` | Nginx | 配置文件 | 路由/限流/HTTPS/前端静态托管/鉴权前置校验 |
| `ai_service/` | Python | FastAPI 服务 | 岗位匹配/简历优化/面试题/Agent 编排 |
| `crawler/` | Python | 独立采集进程 | 岗位抓取/解析/清洗/去重/入队 |
| `database/` | SQL | 建表脚本 | schema、索引、初始化数据、ER 文档 |

> **backend 与 service 的边界（最重要）**：`backend/` 不含任何业务概念（不认识“岗位”“简历”），`service/` 不含任何网络/socket 细节。二者在同一进程内，`service` 链接 `backend` 静态库。此边界保证未来可将任意业务模块抽出为独立进程而无需改动引擎。

---

# 2. backend/ —— C++ 核心框架层

```
backend/
├── include/jobpilot/core/    # 对外公共头文件（小接口）
│   ├── logger.h
│   ├── config.h
│   ├── error.h               # 统一错误码
│   └── ...
├── src/
│   ├── network/              # epoll + Reactor
│   │   ├── event_loop.cc
│   │   ├── event_loop.h
│   │   ├── timer.h
│   │   ├── channel.h
│   │   ├── poller_epoll.h
│   │   ├── buffer.h
│   │   ├── tcp_connection.h
│   │   └── tcp_server.h
│   ├── http/                 # HTTP/1.1 服务端
│   │   ├── http_request.h    # 请求解析（含半包/粘包）
│   │   ├── http_response.h
│   │   ├── http_router.h     # 路由注册
│   │   ├── http_context.h
│   │   └── http_server.h
│   ├── database/             # MySQL 连接池
│   │   ├── mysql_pool.h
│   │   └── sql_result.h
│   ├── cache/                # Redis 客户端 + Session
│   │   ├── redis_client.h
│   │   └── session_manager.h
│   ├── core/                 # 日志/配置/工具/线程池
│   │   ├── logger.cc
│   │   ├── config.cc
│   │   ├── thread_pool.h
│   │   └── string_util.h
│   └── mq/                   # RabbitMQ 客户端封装
│       ├── mq_producer.h
│       └── mq_consumer.h
├── tests/                    # 引擎层单测（GoogleTest）
├── CMakeLists.txt
└── conanfile.txt
```

## 2.1 深模块设计（codebase-design）

每个引擎组件对外是小接口、内部是深实现，调用方只学接口即可获得全部能力：

| 模块 | 小接口（外部可见） | 深实现（对外隐藏） |
|---|---|---|
| `EventLoop` | `Run()` / `Wakeup()` / `RunInLoop(cb)` | epoll 多路复用、定时器、wakeup pipe、任务队列、线程安全 |
| `HttpServer` | `RegisterRoute(method, path, Handler)` / `Start(port)` | 连接生命周期、HTTP 解析状态机、超时回收、并发模型 |
| `ConnectionPool` | `Query(sql) -> Result` / `Execute(sql)` | 连接获取/归还/健康检查/最大连接数/超时等待 |
| `RedisClient` | `Get/Set/SetEx/Del/Incr` | 连接池、命令组包、超时重连 |
| `MQProducer` | `Publish(queue, message)` | 连接管理、自动重连、失败回调 |

**关键接口示例**（`HttpServer`）：

```cpp
class HttpServer {
 public:
  void RegisterRoute(HttpMethod method, const std::string& path,
                     HttpHandler handler);      // handler: (const HttpRequest&) -> HttpResponse
  void Start(uint16_t port, int worker_threads);
};
```

> service 层业务代码只依赖上述窄接口，不感知 epoll/Reactor 细节 —— 这就是“深模块”带来的调用方杠杆（leverage）与维护方局部性（locality）。

---

# 3. service/ —— C++ 业务服务层

```
service/
├── include/jobpilot/service/     # 模块间接口 + 数据对象
│   ├── user/user_service.h
│   ├── resume/resume_service.h
│   ├── job/job_service.h
│   ├── application/application_service.h
│   └── ai/ai_task_service.h
├── src/
│   ├── user/                    # Controller / Service / DAO
│   │   ├── user_controller.cc
│   │   ├── user_service.cc
│   │   ├── user_dao.cc          # 实现层（依赖 backend 连接池）
│   │   └── user_dao_interface.h # 缝（seam）：测试可注入 mock
│   ├── resume/                  # 简历 + 版本 + 文件存储
│   ├── job/                     # 岗位 + 技能要求 + 搜索
│   ├── application/             # 投递 + 状态机 + 统计
│   ├── ai/                      # AI 任务编排
│   │   ├── ai_task_service.cc   # 建任务/入队/查状态
│   │   └── ai_task_dao.cc
│   ├── router.cc                # 装配：把模块 Controller 挂到 HttpServer 路由
│   └── main.cc                  # 组装：加载配置/日志/连接池/启动
├── tests/                       # Service 层单测 + 集成测试
├── CMakeLists.txt
└── config/config.yaml
```

## 3.1 分层规范（每模块固定三层）

```
Controller  解析 HTTP 请求/参数 → 调用 Service → 组装统一响应
    │
Service     业务规则：状态机校验、事务编排、跨模块调用
    │
DAO         仅数据库访问；只依赖 IxxxDAO 接口（缝）
    │
backend 引擎（连接池/Redis/MQ）
```

**模块间通信**：通过 `xxx_service.h` 接口，禁止 `#include` 其它模块实现。示例：`ai_task_service` 需要岗位/简历数据时调 `job_service`、`resume_service` 接口，不直查对方表。

## 3.2 业务模块职责

| 模块 | 职责 | 边界（不允许做的事） |
|---|---|---|
| user | 注册/登录/Token/画像/技能 | 不碰岗位、投递表 |
| resume | 上传/版本/解析/文件存储 | 不关心投递状态 |
| job | 岗位增删查/搜索/技能要求 | 不感知用户投递行为 |
| application | 投递状态机/统计 | 不直查简历文件内容 |
| ai | 任务创建/MQ入队/结果读取/缓存 | 不直接调 LLM（那是 ai/ 的事） |

---

# 4. gateway/ —— API 网关

```
gateway/
├── nginx.conf                 # 核心配置：路由、限流、gzip、静态资源
├── conf.d/
│   ├── upstream_backend.conf  # backend 上游
│   ├── rate_limit.conf        # 限流规则（配合 Redis 或 nginx 自带）
│   └── ssl.conf               # HTTPS（可选，本地 http）
├── docker/                    # gateway 容器
└── README.md                  # 网关规则说明与路由表
```

## 4.1 网关职责

| 能力 | 实现 |
|---|---|
| 路由 | `/api/*` → backend；`/` 静态 → frontend 产物 |
| 限流 | nginx limit_req / 配合 Redis 精确限流 |
| HTTPS | 证书终止（可选，生产开启） |
| 静态托管 | 前端构建产物（SPA history 回退） |
| 鉴权前置 | JWT 过期校验快速失败（可选，业务层为权威） |

> 网关**不做业务**，仅做接入面控制；业务鉴权仍以 Service 层为准。

---

# 5. ai_service/ —— Python AI Service（V1.1 修正：目录名由 ai/ 统一为 ai_service/，与 07/08 一致）

```
ai/
├── app/
│   ├── main.py                # FastAPI 入口（健康检查 + 内网接口）
│   ├── config.py
│   ├── consumers/             # RabbitMQ 消费者
│   │   ├── job_match_consumer.py
│   │   ├── resume_optimize_consumer.py
│   │   └── interview_gen_consumer.py
│   ├── services/              # 业务逻辑
│   │   ├── matcher.py         # 简历匹配三层管线
│   │   ├── optimizer.py
│   │   ├── interview.py
│   │   └── agent.py           # LangChain Agent 编排
│   ├── tools/                 # Agent 工具
│   │   ├── job_search_tool.py
│   │   ├── job_analyze_tool.py
│   │   ├── resume_optimize_tool.py
│   │   └── interview_gen_tool.py
│   ├── llm/                   # LLM/Embedding 客户端
│   │   ├── llm_client.py      # DeepSeek/Qwen（OpenAI 兼容）
│   │   └── embedder.py        # bge-small-zh
│   └── repos/                 # 结果回写（内部HTTP or 直连 ai_tasks）
├── tests/
├── requirements.txt
├── Dockerfile
└── README.md
```

## 5.1 职责

- 消费 `ai.task.*` 队列，执行匹配/优化/面试生成
- 通过**内部 HTTP**向 C++ Backend 取岗位/简历数据（不直连业务库）
- 结果回写 `ai_tasks`（状态 + result JSON）并缓存 Redis
- Agent 工具编排求职工作流

---

# 6. crawler/ —— Python 爬虫服务

```
crawler/
├── scheduler.py               # APScheduler 定时任务
├── spiders/                   # 站点适配
│   ├── base_spider.py
│   └── company_site.py        # 企业官网适配（P2 扩展）
├── parsers/                   # HTML → 结构化 Job
│   └── job_parser.py
├── pipelines/                 # 清洗/规范/去重预检
│   ├── cleaner.py
│   └── dedup.py
├── producers/                 # 发布到 crawler.job.raw
│   └── mq_producer.py
├── config/                    # 站点配置/限速/robots 策略
├── tests/
├── requirements.txt
├── Dockerfile
└── README.md
```

## 6.1 职责

- 定时采集岗位 → 解析 → 清洗 → 发布 `crawler.job.raw`
- 遵守 robots.txt 与限速，**不做登录态采集**
- 数据导入通道（P1）：直接读取 `dataset/` 公开数据发布入队，作为默认路径

---

# 7. database/ —— SQL 与初始化

```
database/
├── schema/                    # 建表脚本（按模块分文件）
│   ├── 01_user.sql
│   ├── 02_resume.sql
│   ├── 03_job.sql
│   ├── 04_application.sql
│   ├── 05_ai.sql
│   └── 06_notification.sql
├── init/                      # docker 首次启动初始化数据
│   ├── init.sql
│   └── seed_jobs.sql          # 示例岗位
├── er.md                      # ER 图与关系说明（同 03 文档摘要）
└── README.md
```

> 所有脚本幂等可重复执行；`init/` 挂载进 mysql 容器 `docker-entrypoint-initdb.d`。

---

# 8. 支撑目录（辅助）

| 目录 | 职责 |
|---|---|
| `frontend/` | Vue3 + Vite + Element Plus + ECharts：登录/岗位/投递/AI分析页面；Axios 封装统一响应与鉴权拦截 |
| `docker/` | 各服务 Dockerfile、`docker-compose.yml`、环境变量模板 `.env.example` |
| `docs/` | 文档体系（00 总览 / 02 架构 / 03 数据库 / 04 API / 05 计划 / 06 规范 / 07 部署 / adr/） |
| `dataset/` | 公开岗位数据集样本（JSON/CSV），供导入与演示 |
| `tests/` | 跨服务集成测试（API 冒烟、AI 链路冒烟） |

---

# 9. 依赖规则（防循环依赖）

```
frontend ──HTTP──► gateway ──HTTP──► service(业务模块)
                                        │ 链接
                                        ▼
                                  backend(引擎)
                                        │ 使用
                            ┌───────────┼───────────┐
                            ▼           ▼           ▼
                         MySQL        Redis      RabbitMQ
ai ──消费 ai.task.*──► RabbitMQ ◄──发布── crawler
ai ──内部HTTP──► service
```

- **禁止**：`backend` 依赖 `service`；`service` 业务模块互相 include 实现；`ai`/`crawler` 直连业务库
- 依赖方向唯一：`service → backend`，`ai/crawler → MQ/内部HTTP`

---

# 10. 分阶段构建计划

> ⚠️ **本节已废弃（Superseded）**：执行分期以 **[08-execution-guide.md](08-execution-guide.md)（P0-P5，1-3 个月窗口）** 为准。下表 A-F 分期为历史版本，保留仅作参考，请勿按此执行。

> 详细排期、止损线与验收见 **[05-development-plan.md](05-development-plan.md)**（诚实分阶段版，按学生业余时间估算，约 26 周）。本节为目录视角的填充顺序。

目录随阶段逐步填充，每一阶段结束时**可运行、可演示**。

| 阶段 | 时间 | 填充内容 | 阶段验收 |
|---|---|---|---|
| **A 引擎 + 地基** | 第 1-2 周 | `backend/` 网络/HTTP/core + `database/schema` + `docker/`(mysql/redis) | `curl` 命中 hello 路由；建表脚本可执行 |
| **B 核心业务** | 第 3-6 周 | `backend/` 网络层打磨至完整 + `service/` 四个业务模块 + Redis Session | 注册/登录/投递全链路走通 |
| **C AI 能力** | 第 7-11 周 | `service/ai`（ai_tasks 轮询版）+ `ai/` 服务（Embedding+LLM）+ 结果缓存 | 发起匹配 → 轮询 → 返回结果（暂不引入 MQ） |
| **D 异步升级** | 第 12-14 周 | RabbitMQ 接入（Producer/Consumer/死信）+ 采集入队 | 任务经队列流转、幂等消费 |
| **E 数据采集** | 第 15-17 周 | `crawler/` + `dataset/` 导入通道 | 一键导入示例岗位入库 |
| **F 前端 + 发布** | 第 18-22 周 | `frontend/`（4 页）+ `docker/` 一键部署 + CI + README | docker compose up 全链路演示；v1.0.0 |

> 原则与止损：先跑通业务闭环（A→B），再叠加异步与 AI（C→D），采集与发布收尾（E→F）。C 阶段若 MQ 客户端卡住则保持轮询版，任何阶段都能对外演示。

---

## 版本记录

| 版本 | 说明 | 日期 |
|---|---|---|
| V1.0 | 依据 ADR-0001~0005 与架构 V3.0 生成 | 2026-08-09 |
