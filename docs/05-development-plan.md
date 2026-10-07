# JobPilot AI 开发计划 V2.0（诚实分阶段版）

> ⚠️ **历史资料 / Historical — Superseded**
>
> 本文档属于 ADR-0007 ~ ADR-0010 之前的旧设计方向，仅作历史参考，
> **不得作为新的实现规范**。现行架构见
> [architecture/open-source-mvp.md](architecture/open-source-mvp.md)，
> 现行决策见 [adr/README.md](adr/README.md)（ADR-0007 ~ ADR-0010）。
>
> Do not use this document for new implementation decisions.

> ⚠️ **Historical / Superseded，不作为当前 MVP 实现依据**：本文档为完整版规划
> （约 26 周），其阶段编号与排期已被现行架构、ADR 与 ExecPlan 体系取代。
> 现行依据见 [architecture/open-source-mvp.md](architecture/open-source-mvp.md)、
> [adr/README.md](adr/README.md)（ADR-0007 ~ ADR-0010）与
> [exec-plans/](exec-plans/README.md)。

> 本计划依据架构 V3.0 与 ADR-0001~0005 编写，但**按学生业余时间与面试价值重新排序**。它不是乐观排期，而是"一个人晚上+周末、约 6 个月、每阶段都能演示"的真实路径。

## 文档说明

| 项 | 值 |
|---|---|
| 排期基准 | 学生业余时间（工作日晚上 + 周末），按每天约 2-3 小时、每周约 20 小时估算 |
| 总工期 | 约 26 周（6 个月），预留缓冲可到 7 个月 |
| 适用 | 明年秋招（有 6-12 个月窗口） |
| 铁律 | ① 顺序 = 面试价值排序，禁止跳步；② 每阶段有止损线；③ 任何阶段必须可运行可演示；④ Deferred 清单默认砍掉 |

## 关键理念

**顺序即优先级。** 全部时间的 60-70% 花在"面试最常问、最能深挖"的 C++ 网络层与业务闭环上；AI、MQ、爬虫、前端都是"做完核心之后的加分项"。任何时间不够，优先保证前四个阶段完整。

**目标架构与实现顺序分离。** ADR-004/005 的 RabbitMQ、Agent 是**最终目标形态**，不是第一批实现。先以最简单可靠的方式打通异步闭环（`ai_tasks` 轮询），再升级，这是工程常识而非偷工减料。

---

# 阶段总览

| 阶段 | 主题 | 时间 | 面试价值 | 可演示点 |
|---|---|---|---|---|
| Phase 0 | 工程地基 | 第 1-2 周 | ⭐⭐ | 可构建、可跑 mysql/redis |
| **Phase 1** | **C++ 网络核心** | 第 3-6 周 | ⭐⭐⭐⭐⭐ | curl 能打自研 HTTP server |
| **Phase 2** | **业务闭环** | 第 7-11 周 | ⭐⭐⭐⭐⭐ | 注册→投递全链路走通 |
| **Phase 3** | **AI 岗位匹配（异步）** | 第 12-15 周 | ⭐⭐⭐⭐ | 发起→轮询→返回匹配结果 |
| Phase 4 | 消息队列升级 | 第 16-18 周 | ⭐⭐⭐ | 队列解耦演示 |
| Phase 5 | 数据采集 | 第 19-21 周 | ⭐⭐ | 一键导入岗位入库 |
| Phase 6 | 前端 + 部署 + CI | 第 22-26 周 | ⭐⭐⭐ | docker compose 一键演示 |
| Phase 7 | 加分项 | 弹性 | ⭐ | 面试助手/简历优化/Agent |

---

# Phase 0 — 工程地基（第 1-2 周）

**目标**：一个能构建、能测试、基础设施能跑的空工程。

**任务**
- [ ] GitHub 仓库 + README + LICENSE + .gitignore
- [ ] CMake + Conan 骨架（依赖选型定稿，见下）
- [ ] `config.yaml` 配置模块（读取 DB/Redis/端口；**密码只走环境变量**）
- [ ] 统一错误码 `error.h`
- [ ] docker-compose（mysql + redis）起来
- [ ] 目录按 `project-structure.md` 建立 backend/service 空壳

**依赖选型定稿（无需再选型，见 08 执行手册 §2.4）**

| 用途 | 选型 | 获取方式 |
|---|---|---|
| 日志/配置/JSON/测试 | spdlog / yaml-cpp / nlohmann_json / gtest | Conan |
| MySQL 客户端 | libmysqlclient（C API） | 系统 apt `libmysqlclient-dev` |
| Redis 客户端 | hiredis | Conan |
| 密码哈希 | libbcrypt | Conan（失败降级 OpenSSL PBKDF2） |
| RabbitMQ 客户端 | amqp-cpp（P4 才引入；V1.1：Conan 包名为 amqp-cpp） | Conan |
| OpenSSL | libssl-dev | 系统 apt |

**验收标准**
- `cmake --build && ctest` 通过
- `docker compose up` 后 mysql/redis 可连接

**止损线**（2 周内必须进 Phase 1）
- Conan 在某环境卡住 > 2 天 → 改用 CMake `FetchContent`，不纠缠工具链
- 不要在这阶段做任何业务代码

---

# Phase 1 — C++ 网络核心（第 3-6 周）⭐⭐⭐⭐⭐

**目标**：自研 HTTP Server 能接收 JSON 请求并返回响应。这是全项目最值钱的部分，给足时间。

**任务（严格按依赖顺序）**
- [ ] `EventLoop`：epoll 封装、Wakeup、RunInLoop
- [ ] `Buffer`：读写缓冲，处理半包/粘包
- [ ] `TcpConnection`：连接生命周期、关闭回调
- [ ] `TcpServer`：监听、accept、分发到线程池
- [ ] HTTP 解析状态机：请求行/头/体、keep-alive、JSON body
- [ ] `Router`：`RegisterRoute(method, path, handler)`
- [ ] 线程池接入，worker 处理业务回调
- [ ] 网络层单元测试（GoogleTest）

**验收标准**
- `curl -X POST http://localhost:8080/hello` 返回 JSON
- `wrk` 压测 QPS > 1 万（本机即可）
- 单测覆盖 Buffer/HTTP 解析/EventLoop 基础路径

**止损线**
- 只实现 HTTP/1.1 子集：GET/POST + JSON。**不做 HTTPS、不做 chunked（可后续）、不做 websocket**
- multipart 文件上传**不在本阶段**，放到 Phase 2 单独限定实现
- 网络层卡住超 1 周 → 冻结缺陷，先进 Phase 2（Phase 2 业务在已有基础上继续，网络层可并行打磨）

---

# Phase 2 — 核心业务闭环（第 7-11 周）⭐⭐⭐⭐⭐

**目标**：注册/登录/简历/岗位/投递全链路能跑通，项目从"框架"变成"产品"。

**任务**
- [ ] MySQL 连接池（获取/归还/超时/最大连接）
- [ ] Redis 客户端 + Session 管理
- [ ] 用户认证：bcrypt 密码哈希、Token（JWT 或随机串）、登录态校验中间件
- [ ] user 模块：注册/登录/画像/技能
- [ ] resume 模块：上传/版本/文件存储
- [ ] job 模块：岗位 CRUD/搜索/技能要求
- [ ] application 模块：投递/状态机/统计
- [ ] DAO 层 + 事务（关键写入用事务）
- [ ] 建表 SQL 按 `database/schema/` 落地

**验收标准**
- Postman/curl 全接口可调：注册→登录→建简历→传岗位→投递→改状态→统计
- 非法状态迁移被拒绝（如 Reject 后再转 Offer）
- Service/DAO 单测覆盖核心路径

**止损线**
- 状态机只做正向迁移 + 基本校验，不做复杂权限
- multipart 解析：**限定 2 天内完成**，只支持 PDF/单一文件，超时就改 base64 上传（面试不问这个细节）
- 前端这阶段**只做最小验证页**，不做正式 UI

---

# Phase 3 — AI 岗位匹配（异步，第 12-15 周）⭐⭐⭐⭐

**目标**：发起匹配 → 轮询 → 返回结构化结果。**用 ai_tasks 表 + 轮询打通异步闭环，暂不引入 RabbitMQ。**

**任务**
- [ ] `ai_tasks` 表 + 任务创建/查询接口（状态机：pending→processing→success/failed）
- [ ] C++ 侧任务编排：创建任务、可选线程池内消费或暴露内部接口
- [ ] Python AI 服务（FastAPI）：健康检查 + 消费任务
- [ ] Worker 方式：AI 服务轮询 `ai_tasks` 表（最简单）或 C++ 内起消费线程
- [ ] Embedding：`bge-small-zh`（本地，约 100MB）
- [ ] 三层匹配管线：规则分 → Embedding 相似度 → LLM 综合评分
- [ ] 结果回写 `ai_tasks.result`（JSON）+ Redis 缓存 `ai:match:{job}:{resume}`（TTL 24h）
- [ ] LLM 调用（DeepSeek/Qwen，OpenAI 兼容协议）失败重试上限 + 降级返回规则分

**验收标准**
- 全链路演示：发起 → 轮询 → 返回 {score, strengths, gaps, suggestions}
- 二次请求命中 Redis 缓存，不再调 LLM
- LLM 挂掉时降级返回规则分，不崩溃

**止损线**
- **本阶段禁止碰 LangChain/Agent**（见 Deferred 清单）
- embedding 模型下载失败 → 先用简单 TF-IDF/关键词相似度顶替，不阻塞
- 匹配管线只保证"能出分、能解释"，不追求准确率

---

# Phase 4 — 消息队列升级（第 16-18 周）⭐⭐⭐

**目标**：用 RabbitMQ 替换轮询，展示完整异步架构（ADR-004 的目标形态）。

**任务**
- [ ] docker 部署 RabbitMQ
- [ ] C++ MQ Producer（ai_task 发布）或先由 AI 服务负责生产
- [ ] AI 服务 MQ Consumer（ACK/NACK/requeue）
- [ ] 幂等消费：消费端仍以 ai_tasks 状态机为准
- [ ] 死信 + 重试上限
- [ ] 采集队列 `crawler.job.raw`（若 Phase 5 未开始可后置）

**验收标准**
- 任务经队列流转，消费失败可重试，最终状态正确
- 重复投递消息不产生重复结果（幂等验证）

**止损线（重要）**
- C++ 集成 MQ 客户端卡 > 1 周 → **放弃 MQ，保持 Phase 3 轮询版本**。面试照样能讲"异步状态机 + 队列解耦"的设计，只是实现上用表轮询代替队列——这是可接受的降级，不丢人

---

# Phase 5 — 数据采集（第 19-21 周）⭐⭐

**目标**：岗位数据能批量导入，爬虫作为加分。

**任务**
- [ ] `dataset/` 公开数据（JSON/CSV）导入通道：读取→清洗→去重（source_url）→入库
- [ ] Scheduler（APScheduler）定时触发
- [ ] Spider → Parser → Pipeline 基础版（1-2 个公开站点，遵守 robots.txt）
- [ ] 合规说明写入 README（不做登录态采集）

**验收标准**
- 一键导入示例数据集入库；重复导入不产生重复岗位
- 定时任务可触发一次采集

**止损线**
- 只做公开静态页采集，**禁止反爬对抗**
- 爬虫卡住 → 砍掉，只保留数据集导入（Phase 5 最低要求 = 导入通道可用）

---

# Phase 6 — 前端 + 部署 + CI（第 22-26 周）⭐⭐⭐

**目标**：可一键演示的完整产品。

**任务**
- [ ] Vue3：登录、岗位列表、投递管理、AI 分析 **4 个页面即可**
- [ ] Axios 封装：统一响应 + token 拦截器
- [ ] ECharts：投递统计（1 张图即可）
- [ ] docker-compose 全组件一键启动（mysql/redis/backend/ai/frontend）
- [ ] GitHub Actions：build + 单测 + 镜像构建
- [ ] README 完善：架构图、快速开始、截图

**验收标准**
- 新机器 `docker compose up` 后全链路可演示
- CI 绿；文档与代码同步

**止损线**
- 前端砍到 4 页，**不做华丽大屏**
- 部署卡住 → 至少保证 README 有本地启动步骤，演示用本地跑

---

# Phase 7 — 加分项（时间允许，随时可砍）⭐

按价值排序，弹性排入：

| 项 | 价值 | 说明 |
|---|---|---|
| 面试助手 | ⭐⭐⭐ | 单次 LLM 调用即可，成本低，值得做 |
| 简历优化 | ⭐⭐⭐ | 同上，简单 |
| Agent 编排 | ⭐⭐ | 简单工具循环，能讲"Agent"故事 |
| 压测报告 | ⭐⭐ | 用 wrk 数据写性能报告，简历加分 |
| 对象存储 | ⭐ | 简历文件迁移 MinIO |
| ES 搜索 | ⭐ | 岗位全文搜索 |

---

# Deferred 清单（默认砍掉，别心疼）

| 项 | 原因 | 何时考虑 |
|---|---|---|
| LangChain / 完整 Agent 框架 | 时间黑洞，C++ 岗位面试价值低 | Phase 7 或之后 |
| ECharts 大屏/复杂前端 | 演示够用即可 | 不做 |
| 多站点爬虫 | 反爬风险 + 合规 | 公开数据集优先 |
| 微服务/RPC | ADR-0002 已否决 | 不做 |
| 向量数据库 RAG | ADR-0003/0005 已否决 | 数据量上来再说 |

---

# 风险登记表

| 风险 | 概率 | 影响 | 缓解 |
|---|---|---|---|
| 网络层开发超期 | 高 | 高 | 限定 HTTP 子集；止损线冻结 |
| C++ MQ 客户端集成失败 | 中 | 中 | Phase 3 轮询兜底，MQ 可放弃 |
| 完美主义/反复重写 | 高 | 高 | 每阶段止损线 + 可演示验收 |
| 学业/面试占用时间 | 高 | 中 | 计划按业余时间排；每阶段独立可交付 |
| LLM API 限流/欠费 | 中 | 低 | Redis 缓存 + 降级规则分 |
| embedding 模型下载/环境 | 低 | 低 | TF-IDF 兜底 |

---

# 面试叙事（每个阶段怎么讲）

| 阶段 | 面试一句话 |
|---|---|
| Phase 1 | "我自己用 epoll+Reactor 写了一个 HTTP 服务器，支持万级 QPS，讲清了 Reactor 模型和线程池分工" |
| Phase 2 | "业务闭环能跑，数据库表、索引、状态机、事务都是我自己设计的，投递状态迁移有校验" |
| Phase 3 | "AI 分析走异步任务状态机：发起→轮询→结果缓存，LLM 挂了还能降级出规则分" |
| Phase 4 | "用 RabbitMQ 把 AI/采集和业务解耦，讲了消息可靠性、幂等消费、死信" |
| Phase 5-6 | "一键 docker compose 起全栈，GitHub CI 自动构建测试" |

> 讲法原则：**优先讲你亲手写过、能深挖到代码层的部分**（网络层、连接池、状态机），AI/MQ 讲"为什么这么设计"即可，不需要背一堆 LLM 细节。

---

## 版本记录

| 版本 | 说明 | 日期 |
|---|---|---|
| V1.0 | 乐观 16 周计划（已废弃，未如实估算业余时间） | 2026-08-09 |
| V2.0 | 诚实分阶段版：按面试价值排序 + 止损线 + 业余时间估算 | 2026-08-09 |
