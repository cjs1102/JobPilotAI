# JobPilot AI 执行手册 V1.1（跟着做就能跑）

> 本文档是**唯一执行依据**：它整合了 00–07 全部设计文档，裁决了它们之间的所有冲突，并按「写什么文件 / 干什么 / 怎么干 / 不会的补什么 / 怎么验收 / 怎么交给我检查」组织成可执行的阶段清单。
>
> 参考文档：00 总览 · 01 需求 · 02 架构 · 03 数据库 · 04 API · 05 开发计划 · 06 编码规范 · 07 部署 · project-structure · adr/
>
> 状态：Ready For Development · 配套修正：根目录总纲已标 Superseded，本文档为唯一执行权威
>
> **V1.1 修正说明**（本版相对 V1.0 的改动）：① nginx `limit_req_zone` 位置修正并豁免 GET 轮询；② config.yaml 去掉 `${}` 插值写法，改为"yaml 存非敏感默认值 + 环境变量覆盖 + 密码 fail-fast"；③ 依赖选型定稿（`hiredis` 走 Conan、`amqp-cpp` 包名修正、`libbcrypt` 改 vendored）；④ 统一构建入口为 `service/`（删除 backend 独立 conan 构建）；⑤ docker-compose 移到仓库根目录，`.env` 同步在根；⑥ P0/P1 HTTP Server 演进关系明确（P0 占位 `health_stub.cc`，P1 完成 HttpServer 后删除）；⑦ AI 任务抢占/超时补偿、RabbitMQ ACK/NACK/重试上限/死信/发布失败补偿机制全部给最终方案；⑧ MySQL 连接池、配置读取、环境变量表、CI 全部细化定稿；⑨ 01/02/00/05/07/project-structure/adr 同步修正。

---

# 1. 如何使用本手册（先读这一章）

## 1.1 工作流（重要）

```
每完成一个 Phase
   ↓
按该 Phase 的「检查点提交模板」整理提交物
   ↓
发给我（agent）检查
   ↓
我按四维度 review：① 是否符合本文档契约 ② 是否符合 06 编码规范 ③ 验收标准是否达成 ④ 有无隐藏问题
   ↓
返回「通过」或「整改清单」
   ↓
通过后才进入下一个 Phase
```

**铁律**：
1. **顺序不可跳**：P0→P1→P2→P3→P4→P5，每阶段都有止损线，超线先冻结进下一步。
2. **契约不可改**：表结构（03）、API 契约（04）以及本文档的裁决，改动必须先找我评审，禁止边写边改。
3. **文档同步**：任何设计变化，先改 docs 再改代码。
4. **每阶段可演示**：任何阶段中断，都要能跑、能演示。

## 1.2 检查点一览

| 检查点 | Phase | 内容 | 提交物摘要 |
|---|---|---|---|
| **C0** | P0 工程骨架 | 仓库/构建/配置/基础设施可跑，`/health` 可访问（占位实现） | 目录结构 + `cmake --build` + `ctest` 输出 + `docker compose ps` + `/health` 响应 |
| **C1** | P1 网络核心 | 自研 HTTP Server 可 curl 可压测，占位已删除 | curl/wrk 输出 + ctest 结果 + 文件清单 |
| **C2** | P2 业务闭环 | 注册→投递全链路走通 | 全链路 curl 脚本输出 + 状态机校验 + DAO/事务说明 |
| **C3** | P3 异步 AI | 发起→轮询→匹配结果 | 任务 JSON + 缓存命中 + 降级验证 |
| **C4** | P4 MQ 升级 | RabbitMQ 队列流转 | 队列日志 + 幂等验证 + ACK/NACK 行为 + 补偿验证 |
| **C5** | P5 部署+开源 | docker compose 一键演示 + GitHub | compose 日志 + 截图 + 仓库链接 + README + CI |

## 1.3 提交模板（每个检查点都按这个格式发给我）

```
【检查点 Cx - Phase x 提交】
1. 新增/修改文件清单：<列表>
2. 构建与测试输出：<粘贴 cmake/build/ctest 关键输出>
3. 验收标准核对：<逐条写"达成/未达成 + 证据">
4. 遇到的问题与你的决策：<没有就写"无">
5. 与本文档的偏差：<没有就写"无">
```

---

# 2. 方向与范围裁决

## 2.1 项目方向（已裁决）

**保留 JobPilot AI 项目本身**。理由：时间窗口（2026 秋招，1-3 个月）不允许推倒重来；本项目文档体系成熟，网络核心+业务闭环正好是你最需要展示、也最需要补强的部分。

## 2.2 范围表（已裁决）

| 保留（必做） | 砍掉（不做/后置） | 理由 |
|---|---|---|
| P1 网络核心：epoll+Reactor+HTTP/1.1 子集+线程池 | 爬虫（仅手工录入+seed 导入） | 网络层是面试压舱石 |
| P2 业务闭环：注册/登录/画像/技能/简历/岗位/投递/统计 | 面试助手/简历优化（加分项） | 核心闭环优先 |
| P3 异步 AI：ai_tasks 轮询版 + 规则分→DeepSeek + 缓存 + 降级 | Agent/LangChain | 成本低、有差异化，但 Agent 是时间黑洞 |
| P4 RabbitMQ 升级：ai.task.job_match 全链路 | ECharts 华丽大屏（**保留 1 张投递统计图**，V1.1 修正口径）、对象存储、ES | MQ 是面试高频考点，保留（用户指定） |
| P5 前端 4 页 + docker compose + GitHub 开源 | CI 完整化（只留 build+test） | 可演示 + 可开源是交付闭环 |

## 2.3 与既有文档的冲突裁决表（以此为准，不用再纠结）

| # | 冲突点 | 裁决（以谁为准） | 说明 |
|---|---|---|---|
| R1 | AI 回写数据：02 架构图画 AI 直连 MySQL，project-structure 说禁止 ai 直连业务库 | **MVP 允许 AI 服务直连 MySQL 读写 `ai_tasks`（AI 域表）并直读 `jobs`/`resume_versions` 内容**，结果缓存 Redis；正式形态走内部 HTTP | 见 ADR-0006（本文档 §8.7）；02 已同步标注 |
| R2 | 登录路径：02 写 `/api/v1/auth/login`，04 写 `/api/v1/users/login` | **`/api/v1/users/register`、`/api/v1/users/login`（04 为准）** | 02 时序图已修正 |
| R3 | 岗位接口鉴权：04 §1.3 说全鉴权，§9 说 jobs 公开 | **岗位搜索/详情公开，其余接口鉴权** | 演示友好，04 §1.3 已修正 |
| R4 | 简历格式：01 写 PDF/DOCX，04 写 PDF/TXT/MD | **PDF/TXT/MD**（DOCX 解析砍掉） | 01 已修正 |
| R5 | 两套排期：05 是 Phase 0-7 / 26 周，project-structure §10 是 A-F | **本文档 P0-P5（1-3 个月）为准**；05 已加 banner 标注"执行以 08 为准"，project-structure §10 已标注废弃（V1.1 修正） | 本文档整合了 05 并砍掉加分项 |
| R6 | 根目录总纲 vs docs 体系 | **docs/ 为准**，根目录总纲已标 Superseded | 面试引用时用 docs/02 架构 V3.0 |
| R7 | AI 队列形态：P3 轮询 vs P4 MQ | **P3 先轮询打通闭环，P4 再升级 MQ** | 目标架构与实现顺序分离（05 理念） |
| R8 | 技术栈口径：00 列 ECharts，V1.0 砍"ECharts 大屏" | **保留 ECharts 组件，只画 1 张投递统计图，不做大屏**（V1.1 修正） | 与 00 一致，见 §10.2 ⑤ |

## 2.4 依赖选型定稿（P0 就用，不用再选型）（V1.1 修正：bcrypt 改 vendored、amqpcpp 包名修正、hiredis 定稿）

| 用途 | 选型 | 获取方式 | CMake 链接 target（V1.1 修正：补全） |
|---|---|---|---|
| 日志 | spdlog | Conan `spdlog/1.14.1` | `spdlog::spdlog` |
| 配置 | yaml-cpp | Conan `yaml-cpp/0.8.0` | `yaml-cpp::yaml-cpp` |
| JSON | nlohmann_json | Conan `nlohmann_json/3.11.3` | `nlohmann_json::nlohmann_json` |
| 测试 | gtest | Conan `gtest/1.14.0` | `GTest::gtest` / `GTest::gtest_main` |
| MySQL 客户端 | libmysqlclient（C API） | 系统 apt `libmysqlclient-dev` | `pkg_check_modules(MYSQLCLIENT REQUIRED IMPORTED_TARGET mysqlclient)` → `PkgConfig::MYSQLCLIENT` |
| Redis 客户端 | hiredis | Conan `hiredis/1.2.0` | `hiredis::hiredis` |
| 密码哈希 | **libbcrypt vendored**（`third_party/libbcrypt/bcrypt.c` + `bcrypt.h`，CC0 许可） | **不依赖 Conan**（Conan Center 无稳定官方包，V1.1 修正） | 直接编译进 `jobpilot_core`，无需 target |
| RabbitMQ 客户端 | **amqp-cpp**（V1.1 修正：Conan Center 包名是 `amqp-cpp`，不是 amqpcpp；自动带 Boost.Asio 依赖） | Conan `amqp-cpp/4.3.x`（P4 才加入 conanfile） | `amqp-cpp::amqp-cpp` |

**Conan 包名与 target 对照（V1.1 修正：新增，避免查错）**：

| conanfile 里的名字 | find_package 名字 | CMake target |
|---|---|---|
| `spdlog/1.14.1` | `find_package(spdlog CONFIG REQUIRED)` | `spdlog::spdlog` |
| `yaml-cpp/0.8.0` | `find_package(yaml-cpp CONFIG REQUIRED)` | `yaml-cpp::yaml-cpp` |
| `nlohmann_json/3.11.3` | `find_package(nlohmann_json CONFIG REQUIRED)` | `nlohmann_json::nlohmann_json` |
| `gtest/1.14.0` | `find_package(GTest CONFIG REQUIRED)` | `GTest::gtest` / `GTest::gtest_main` |
| `hiredis/1.2.0` | `find_package(hiredis CONFIG REQUIRED)` | `hiredis::hiredis` |
| `amqp-cpp/4.3.1`（P4 加入） | `find_package(amqp-cpp CONFIG REQUIRED)` | `amqp-cpp::amqp-cpp` |

> Conan 拉包失败一律降级：CMake `FetchContent`（05 文档预案），不纠缠工具链。

---

# 3. 环境准备（Windows + Ubuntu 虚拟机）

## 3.1 布局

```
Windows 主机          Ubuntu 虚拟机（开发+运行）
  ├─ VS Code              ├─ 源码（git clone / 或共享目录）
  ├─ 浏览器（演示用）      ├─ gcc/g++/cmake/git/docker
  └─ DeepSeek 控制台       └─ docker compose（mysql/redis/rabbitmq/服务）
```

- 编辑在 Windows 侧（VS Code），编译/运行在 VM 内（VS Code Remote-SSH 连进 VM）。
- Docker Engine 装在 **VM 内**（不用 Docker Desktop），避免 Windows 侧网络坑。

## 3.2 虚拟机里安装（一条条执行）

```bash
# 1. 基础工具
sudo apt update && sudo apt install -y \
  build-essential cmake ninja-build git python3 python3-pip python3-venv \
  libssl-dev libmysqlclient-dev pkg-config wrk

# 2. Docker Engine + compose 插件
curl -fsSL https://get.docker.com | sh
sudo usermod -aG docker $USER
newgrp docker
docker compose version   # 应显示 v2.x

# 3. Conan
pip install conan
conan profile detect

# 4. 验证
g++ --version && cmake --version && docker --version
```

> **V1.1 修正**：环境安装补 `wrk`（P1 压测验收需要，V1.0 遗漏）。

## 3.3 验收（C0 环境部分）

- `g++ --version` ≥ 11；`cmake --version` ≥ 3.22；`docker compose version` 正常
- 若 `get.docker.com` 网络不通：改用 `apt install docker.io docker-compose-plugin`（备用）

> **Windows 原生不能跑 epoll 代码**，所有编译/运行必须在 VM 内。这是硬约束，不要尝试在 Windows 直接编译网络层。

---

# 4. 技术栈查漏补缺表

> 规则：**动手写之前，先花时间补对应知识点**；每项都给了"学到什么程度够用"，不要过度学习。资源是中文优先、B 站/书为主。

## 4.1 C++ 语言与现代特性（贯穿 P0-P2）

| 要会什么 | 学到什么程度够用 | 推荐资源 |
|---|---|---|
| 智能指针（unique_ptr/shared_ptr） | 能正确管理连接/缓冲生命期，不用裸 new | 《Effective Modern C++》第 4 章 / B 站搜"unique_ptr 讲解" |
| 移动语义 + 右值引用 | 能写 `Buffer` 的移动构造/赋值 | 《Effective Modern C++》第 5 章 |
| 多线程：std::thread/mutex/condition_variable/atomic | 能写线程池、连接池、EventLoop 唤醒 | 《C++并发编程实战》第 2-4 章 |
| lambda + std::function | 回调注册（Router/EventLoop 大量用） | 任何现代 C++ 教程 |

## 4.2 Linux 网络编程（P1 前必须补）

| 要会什么 | 学到什么程度够用 | 推荐资源 |
|---|---|---|
| socket 基础（bind/listen/accept/读写） | 能写 echo server | 游双《Linux高性能服务器编程》第 5-6 章 |
| **epoll**（LT/ET、水平触发区别） | 能说清 epoll_wait 返回、就绪事件处理 | 游双 第 9 章 / B 站"epoll 原理" |
| **Reactor 模式** | 能画出"事件循环→分发→回调"图，讲清为什么单线程+非阻塞 | 陈硕《Linux多线程服务端编程》第 8 章 / B 站搜"Reactor 模型" |
| 阻塞/非阻塞/IO 多路复用区别 | 面试必问，能对比 | 游双 第 8 章 |

## 4.3 HTTP/1.1 协议（P1 前）

| 要会什么 | 学到什么程度够用 | 推荐资源 |
|---|---|---|
| 请求行/请求头/请求体格式 | 能手写解析状态机 | 《图解HTTP》第 2 章 |
| keep-alive、Content-Length、chunked | 知道 Content-Length 是分帧依据；chunked **不做**（止损线） | RFC 7230 要点 / 图解HTTP |
| 状态码语义 | 200/400/401/404/500 等 | 图解HTTP |

## 4.4 构建工具（P0 用）

| 要会什么 | 学到什么程度够用 | 推荐资源 |
|---|---|---|
| 现代 CMake（target-based） | 能写 `add_library/add_executable/target_link_libraries` | 《Modern CMake》中文版 / B 站"CMake 教程" |
| Conan 2 | 能 `conan install . -of build` + CMakeDeps 集成 | 官方文档 quickstart / B 站搜"conan2" |
| 编译选项 | `-std=c++17 -Wall -Wextra -Werror` 含义 | 见 06 规范 |

## 4.5 MySQL（P2 前）

| 要会什么 | 学到什么程度够用 | 推荐资源 |
|---|---|---|
| SQL 增删改查 + 预处理语句 | 能写参数化 DAO | 任何 SQL 教程 |
| **事务**（ACID、隔离级别） | 能讲连接池里事务边界怎么控制 | 《高性能MySQL》第 1/7 章要点 |
| 索引原理（B+树、联合索引、覆盖索引） | 能解释 03 文档的索引设计 | 《高性能MySQL》第 5 章 |
| 连接池思想 | 能讲获取/归还/超时/最大连接 | B 站搜"数据库连接池原理" |

## 4.6 Redis（P2 前）

| 要会什么 | 学到什么程度够用 | 推荐资源 |
|---|---|---|
| 常用命令（SET/GET/EXPIRE/DEL/INCR） | 能写 session 与缓存 | redis 官方命令文档 |
| 缓存模式 Cache-Aside | 能讲读缓存→回填→失效 | B 站搜"缓存一致性" |

## 4.7 认证与安全（P2 前）

| 要会什么 | 学到什么程度够用 | 推荐资源 |
|---|---|---|
| bcrypt 原理（盐+慢哈希） | 能讲为什么不能明文/单次 MD5 | 搜"bcrypt 原理"（vendored libbcrypt 用法见其 README） |
| Token/Session 方案 | 能讲"登录→Redis 存 token→请求带 token→校验"链路 | 搜"session token 区别" |

## 4.8 RabbitMQ/AMQP（P4 前补，**面试重点**）

| 要会什么 | 学到什么程度够用 | 推荐资源 |
|---|---|---|
| 核心概念：exchange/queue/binding/routing key | 能画出 P4 拓扑图 | RabbitMQ 官方 tutorial（中文版）第 1-3 课 |
| **消息可靠性**：durable/手动 ACK/NACK/requeue | 能回答"消息会不会丢、消费失败怎么办" | 官方 tutorial 第 2 课 + 搜"RabbitMQ 消息可靠" |
| **幂等消费** | 能讲"重复投递不重复处理"（ai_tasks 状态机） | 搜"MQ 幂等消费" |
| **死信队列 DLX + 重试上限** | 能讲"重试超限进死信、补偿扫描"（本手册 §9.4 ②） | 搜"RabbitMQ 死信队列" |
| 削峰/prefetch | 能讲 QoS prefetch=1 | 官方文档 consumer prefetch |

## 4.9 Docker / Compose（P5 前）

| 要会什么 | 学到什么程度够用 | 推荐资源 |
|---|---|---|
| Dockerfile 多阶段构建 | 能写 backend（构建态→运行态） | 官方文档 + 07 部署文档 |
| compose 服务编排 + healthcheck + depends_on condition | 能写全组件 compose | 官方文档 / B 站搜"docker compose 教程" |

## 4.10 Python / FastAPI / DeepSeek（P3 前）

| 要会什么 | 学到什么程度够用 | 推荐资源 |
|---|---|---|
| Python 基础（类/装饰器/异步） | 能写 FastAPI 服务 | 廖雪峰 Python 教程 |
| FastAPI + uvicorn + lifespan | 能起服务 + 后台线程 | FastAPI 官方教程（中文） |
| OpenAI 兼容 API 调用（DeepSeek） | 能 `requests`/`openai` 库调 chat completions | DeepSeek 官方文档（openai 兼容） |
| sentence-transformers（可选） | 能加载 bge-small-zh 算 embedding；失败用 TF-IDF 兜底 | 官方 README |

## 4.11 Vue3 最小集（P5 前）

| 要会什么 | 学到什么程度够用 | 推荐资源 |
|---|---|---|
| Vue3 组合式 API（setup/ref/reactive） | 能写 4 个页面 | Vue3 官方文档（中文） |
| Element Plus 基础组件 | 表单/表格/弹窗 | Element Plus 官方文档 |
| Axios 封装 + token 拦截器 | 能统一响应处理 | B 站搜"axios 拦截器" |

---

# 5. Phase 0 — 工程骨架（3-5 天）→ 检查点 C0

## 5.1 目标

一个能构建、能测试、基础设施能跑、`/health` 能访问的空工程。**这阶段不写任何业务代码，不写网络层**。

## 5.2 写什么文件（V1.1 修正：目录结构调整——compose 移根目录、删 backend/conanfile.txt、加 third_party 与 health_stub）

```
JobPilotAI/
├── .gitignore                 # 忽略 build/ .venv/ storage/ .env 等
├── .env.example               # V1.1 修正：移到仓库根（compose 在根自动读取）
├── LICENSE                    # MIT（开源用）
├── README.md                  # 占位：项目名 + 一句话简介 + 架构图占位
├── docker-compose.yml         # V1.1 修正：移到仓库根（context: . 即根目录，与 Dockerfile 路径一致）
├── backend/
│   ├── CMakeLists.txt         # 静态库 jobpilot_core + add_subdirectory(tests)
│   ├── include/jobpilot/core/
│   │   ├── error.h            # ErrorCode 枚举（对齐 04 错误码表）
│   │   ├── logger.h           # spdlog 封装
│   │   └── config.h           # 配置读取（yaml-cpp + 环境变量覆盖，见 5.3）
│   ├── src/core/
│   │   ├── error.cc
│   │   ├── logger.cc
│   │   └── config.cc
│   └── tests/
│       ├── CMakeLists.txt
│       └── test_smoke.cc      # 冒烟测试（gtest，编译+跑通即可）
├── service/
│   ├── conanfile.txt          # 唯一依赖声明处（V1.1 修正：backend 不再有 conanfile）
│   ├── CMakeLists.txt         # 顶层：enable_testing + add_subdirectory(../backend backend)
│   ├── config/config.yaml     # 见 5.3（密码不写在此文件）
│   ├── src/
│   │   ├── main.cc            # 入口：加载配置/日志，启动 server
│   │   ├── health_stub.cc     # V1.1 修正：P0 占位 HTTP 响应（P1 完成 HttpServer 后删除！）
│   │   └── router.h / router.cc  # V1.1 修正：P0 只留空壳声明，路由注册从 P1 开始
│   └── tests/CMakeLists.txt
├── third_party/
│   └── libbcrypt/             # V1.1 修正：vendored bcrypt（bcrypt.c + bcrypt.h + LICENSE），CC0 许可
├── database/
│   └── schema/                # canonical DDL；P0 已落地，后续迁移按版本扩展
├── docker/
│   └── backend/  ai/  gateway/   # Dockerfile（P4/P5 阶段填充）
└── docs/                      # 已有，不动
```

## 5.3 怎么做

### ① GitHub 建仓库

public 仓库 `JobPilotAI`，`git init` 后首次 commit。

### ② service/conanfile.txt（定稿，唯一依赖声明处）（V1.1 修正：hiredis 提前加入、backend 不再单独声明）

```
[requires]
spdlog/1.14.1
yaml-cpp/0.8.0
nlohmann_json/3.11.3
gtest/1.14.0
hiredis/1.2.0

[generators]
CMakeToolchain
CMakeDeps
```

> - `backend/` **不建 conanfile.txt**：依赖统一由 `service/conanfile.txt` 声明，`conan install` 只在 service 执行一次。
> - `amqp-cpp` 到 P4 再加入本文件（P4 §9.3 会给出追加后的完整版）。
> - vendored libbcrypt 不经过 Conan。

### ③ CMake 定稿（V1.1 修正：target 与测试注册明确化）

**service/CMakeLists.txt**（顶层）：

```cmake
cmake_minimum_required(VERSION 3.22)
project(jobpilot_server CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

enable_testing()                      # 收集 backend/tests 与 service/tests 的全部测试

add_subdirectory(../backend backend)  # 引擎子工程（binary dir 为 build/backend）
add_subdirectory(tests)

add_executable(jobpilot_server
  src/main.cc
  src/health_stub.cc                  # P1 完成后删除此文件及其引用
  src/router.cc
)
target_link_libraries(jobpilot_server PRIVATE jobpilot_core)
target_compile_options(jobpilot_server PRIVATE -Wall -Wextra -Werror)
```

**backend/CMakeLists.txt**：

```cmake
find_package(spdlog CONFIG REQUIRED)
find_package(yaml-cpp CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(hiredis CONFIG REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(MYSQLCLIENT REQUIRED IMPORTED_TARGET mysqlclient)

add_library(jobpilot_core STATIC
  src/core/logger.cc
  src/core/config.cc
  src/core/error.cc
)
target_include_directories(jobpilot_core PUBLIC include)
target_link_libraries(jobpilot_core PUBLIC
  spdlog::spdlog yaml-cpp::yaml-cpp nlohmann_json::nlohmann_json
  hiredis::hiredis PkgConfig::MYSQLCLIENT
)
target_compile_options(jobpilot_core PUBLIC -Wall -Wextra)
target_compile_options(jobpilot_core PRIVATE -Werror)   # 库代码强制无警告

add_subdirectory(tests)
```

**backend/tests/CMakeLists.txt**（P0 阶段只有冒烟）：

```cmake
include(GoogleTest)
find_package(GTest CONFIG REQUIRED)

add_executable(test_smoke test_smoke.cc)
target_link_libraries(test_smoke PRIVATE jobpilot_core GTest::gtest GTest::gtest_main)
gtest_discover_tests(test_smoke)
```

**CMake target 命名总表（V1.1 修正：新增，全项目统一）**：

| target | 内容 | 阶段 |
|---|---|---|
| `jobpilot_core` | backend 静态库（引擎全部代码） | P0 起 |
| `jobpilot_server` | service 可执行 | P0 起 |
| `test_smoke` | P0 冒烟 | P0 |
| `test_buffer` / `test_http_request` | 网络层单测 | P1 |
| `test_user_service` 等 | 业务层单测 | P2 |

### ④ config.yaml 最终方案（V1.1 修正：去掉 `${}` 插值——yaml-cpp 不做插值，改为"yaml 存非敏感默认值 + 环境变量覆盖 + 密码 fail-fast"）

**service/config/config.yaml**：

```yaml
server:
  port: 8080
  worker_threads: 4
  log_file: "storage/logs/jobpilot.log"
  log_level: "info"

mysql:
  host: "127.0.0.1"     # 本地默认；Docker 内由环境变量 MYSQL_HOST 覆盖（见下）
  port: 3306
  user: "root"
  database: "jobpilot"  # 密码不写在此文件！必须由环境变量 MYSQL_PASSWORD 提供
  max_conn: 10

redis:
  host: "127.0.0.1"     # Docker 内由环境变量 REDIS_HOST 覆盖
  port: 6379
```

**配置读取规则（Config::Load 的实现契约，最终方案）**：

```
1. yaml-cpp 读取 config.yaml → 得到全部默认值
2. 环境变量覆盖（存在则覆盖 yaml 值）：
   MYSQL_HOST / MYSQL_PORT / MYSQL_USER / MYSQL_DATABASE
   REDIS_HOST / REDIS_PORT
   RABBITMQ_HOST / RABBITMQ_PORT
3. 密码只从环境变量读取：MYSQL_PASSWORD
4. MYSQL_PASSWORD 未设置 → LOG_ERROR("MYSQL_PASSWORD not set") + exit(1)
   （fail-fast：禁止任何默认密码，宁可不启动）
```

> 这是硬性契约：**yaml 里永远不会出现密码**；本机调试时 `export MYSQL_PASSWORD=xxx` 或写进 shell 脚本（脚本不入 git）。

### ⑤ 环境变量总表（V1.1 修正：新增，全项目统一命名）

| 变量 | 用途 | 本地（VM）值 | Docker compose 注入值 |
|---|---|---|---|
| `MYSQL_HOST` | MySQL 主机 | 不设（yaml 默认 127.0.0.1） | `mysql` |
| `MYSQL_PORT` | MySQL 端口 | 不设（默认 3306） | 不设 |
| `MYSQL_USER` | MySQL 用户 | 不设（默认 root） | `root` |
| `MYSQL_PASSWORD` | MySQL 密码 | **必须 export**（与 .env 的 MYSQL_ROOT_PASSWORD 相同值） | `${MYSQL_PASSWORD}`（compose 从根 .env 读） |
| `MYSQL_DATABASE` | 库名 | 不设（默认 jobpilot） | `jobpilot` |
| `REDIS_HOST` / `REDIS_PORT` | Redis | 不设（默认 127.0.0.1:6379） | `redis` / 不设 |
| `RABBITMQ_HOST` / `RABBITMQ_PORT` | RabbitMQ（P4 用） | 不设 | `rabbitmq` / 不设 |
| `LLM_API_KEY` | DeepSeek Key（P3 用） | export | `${LLM_API_KEY}` |
| `LLM_BASE_URL` | LLM API 地址 | 不设（默认 https://api.deepseek.com） | `${LLM_BASE_URL:-https://api.deepseek.com}` |
| `LLM_MODEL` | 模型名 | 不设（默认 deepseek-chat） | `${LLM_MODEL:-deepseek-chat}` |
| `MYSQL_ROOT_PASSWORD` | 仅 compose 的 mysql 容器用 | 写进根 `.env` | `mysql` 服务 environment |

> **本地 vs Docker 的唯一差异 = 主机名**（127.0.0.1 vs 服务名）。代码只读环境变量与 yaml，不感知运行环境。

### ⑥ MySQL 初始化最终方案（明确"只在数据卷为空时执行"与幂等要求）

- 初始化脚本目录：`database/schema/`，挂载到容器 `/docker-entrypoint-initdb.d`。
- **执行时机**：MySQL 镜像**只在数据卷为空（首次启动）时执行 init 脚本**；之后 `docker compose down` 不会重跑，需要重置就 `docker compose down -v`。
- 文件名按字母序执行：`01_user.sql`、`02_resume.sql`、`03_job.sql`、`04_application.sql`、`05_ai.sql`、`06_notification.sql`，全部使用 `CREATE TABLE IF NOT EXISTS`。
- 库由 compose 的 `MYSQL_DATABASE` 自动创建；脚本作用于该数据库，不硬编码实例库名。

### ⑦ docker-compose.yml（仓库根，P0 版只有 mysql + redis）（V1.1 修正：移到根目录，context 与 dockerfile 路径自洽；P4/P5 追加服务的完整版见 §9.3 / §10.3）

```yaml
services:
  mysql:
    image: mysql:8.0
    environment:
      MYSQL_ROOT_PASSWORD: ${MYSQL_ROOT_PASSWORD:?set MYSQL_ROOT_PASSWORD in .env}
      MYSQL_DATABASE: jobpilot
    ports: ["3306:3306"]
    volumes:
      - mysql_data:/var/lib/mysql
      - ./database/schema:/docker-entrypoint-initdb.d:ro
    healthcheck:
      test: ["CMD", "mysqladmin", "ping", "-h", "localhost"]
      interval: 10s
      timeout: 5s
      retries: 5

  redis:
    image: redis:7-alpine
    ports: ["6379:6379"]
    volumes: [redis_data:/data]
    healthcheck:
      test: ["CMD", "redis-cli", "ping"]
      interval: 10s
      timeout: 5s
      retries: 5

volumes:
  mysql_data: {}
  redis_data: {}
```

**.env.example（仓库根）**：

```bash
# docker compose 读取（仓库根 .env，复制为 .env 后填写）
MYSQL_ROOT_PASSWORD=change_me
# 以下 P3/P4 使用
LLM_API_KEY=sk-xxxx
# 本地跑 C++ 服务时 export MYSQL_PASSWORD=$MYSQL_ROOT_PASSWORD
```

### ⑧ health_stub.cc —— P0 占位 HTTP 响应（V1.1 修正：新增，明确 P0/P1 演进）

**目的**：P0 验收要求 `/health` 可访问，但 P1 才实现 HttpServer。本文件是**临时占位**：约 40 行，单线程 `socket + accept`，收到任何请求直接回写固定 JSON，不解析请求。

```cpp
// health_stub.cc —— P0 临时占位，P1 完成 HttpServer 后【必须删除】！
// 禁止在此文件里扩展任何路由/业务逻辑。
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cstring>
#include <string>

namespace jobpilot::service {

int RunHealthStub(int port) {
  int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(static_cast<uint16_t>(port));
  int reuse = 1;
  ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
  ::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
  ::listen(fd, 16);
  const std::string resp =
      "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
      "Content-Length: 46\r\nConnection: close\r\n\r\n"
      "{\"code\":0,\"message\":\"success\",\"data\":{\"status\":\"ok\"}}";
  for (;;) {
    int cfd = ::accept(fd, nullptr, nullptr);
    if (cfd < 0) continue;
    char buf[1024];
    ::recv(cfd, buf, sizeof(buf), 0);   // 收到即可，不解析
    ::send(cfd, resp.data(), resp.size(), 0);
    ::close(cfd);
  }
}

}  // namespace jobpilot::service
```

**P0/P1 演进关系（最终方案，V1.1 修正）**：

```
P0: main.cc 启动 RunHealthStub(port)  → /health 可 curl
P1: 完成 HttpServer + Router 后：
    ① router.cc 注册 GET /health（正式实现）
    ② main.cc 改用 HttpServer
    ③ 删除 health_stub.cc + CMakeLists 移除引用
    ④ P1 验收确认：项目里只剩一套 HTTP Server（HttpServer）
```

**main.cc（P0 版）**：

```cpp
#include "jobpilot/core/config.h"
#include "jobpilot/core/logger.h"
#include "health_stub.h"

int main(int argc, char* argv[]) {
  jobpilot::core::Config config;
  config.Load("config/config.yaml");          // 内部：MYSQL_PASSWORD 未设则 exit(1)
  jobpilot::core::Logger::Init(config);       // spdlog 初始化
  LOG_INFO("jobpilot server (stub) listening on :" + std::to_string(config.server.port));
  return jobpilot::service::RunHealthStub(config.server.port);
}
```

> 头文件 `health_stub.h` 声明 `int RunHealthStub(int port);`。`router.h/router.cc` 本阶段只建空壳（P1 填充）。

### ⑨ 构建命令（唯一入口，全部在 service 目录执行）（V1.1 修正：删除 backend 独立构建流程）

```bash
cd service
conan install . -of build --build=missing
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure
```

### ⑩ 起基础设施 + 验证 /health

```bash
cd <仓库根>
cp .env.example .env        # 编辑 .env 填 MYSQL_ROOT_PASSWORD
docker compose up -d mysql redis
docker compose ps           # 两个服务 healthy

# 终端 A：起后端（占位）
export MYSQL_PASSWORD=<与 .env 的 MYSQL_ROOT_PASSWORD 相同>
cd service && ./build/bin/jobpilot_server --config=config/config.yaml

# 终端 B：验证
curl http://localhost:8080/health
# → {"code":0,"message":"success","data":{"status":"ok"}}
```

## 5.4 验收标准（C0）（V1.1 修正：验收命令与 5.3 ⑨⑩ 对齐）

- [ ] `cmake --build build` 成功，无 warning（-Werror 强制，测试除外）
- [ ] `ctest --test-dir build --output-on-failure` 全绿（test_smoke 通过）
- [ ] `docker compose ps` 显示 mysql/redis 均 healthy
- [ ] `curl http://localhost:8080/health` 返回 `{"code":0,"message":"success","data":{"status":"ok"}}`
- [ ] 不设置 `MYSQL_PASSWORD` 启动 → 进程立即退出并打印明确错误（fail-fast 验证）
- [ ] git 已提交，仓库结构清晰；`.env` 未被提交（`.gitignore` 生效）

## 5.5 常见坑

| 坑 | 解法 |
|---|---|
| Conan 拉包慢/失败 | 换国内源：`conan remote add conan-center https://center.conan.io` 或配置镜像；仍失败 → FetchContent |
| gtest 找不到 | 确认 conan install 生成 CMakeDeps，CMake 里 `find_package(GTest CONFIG REQUIRED)` |
| `-Werror` 卡死 | 先看具体 warning，修代码而不是关 -Werror（测试代码除外） |
| docker 权限 | `sudo usermod -aG docker $USER && newgrp docker` |
| 端口被占 | `ss -lntp | grep 8080` 排查 |
| 启动即退出且日志有 MYSQL_PASSWORD | 这是 fail-fast 特性，不是 bug：export MYSQL_PASSWORD 后重试 |
| libmysqlclient 链接失败 | `pkg-config --libs mysqlclient` 验证系统包；确认装过 libmysqlclient-dev |

## 5.6 止损线

- Conan 卡 > 2 天 → 直接 FetchContent，不纠缠工具链。
- 本阶段**不写任何业务代码、不写网络层**（/health 用占位 stub，P1 替换）。

---

# 6. Phase 1 — 网络核心（2-3 周）→ 检查点 C1 ⭐⭐⭐⭐⭐

> 这是全项目最值钱的部分，**面试 60% 的问题都在这**。严格按依赖顺序做，每个子任务完成后先自测再往下。

## 6.1 子任务顺序与文件清单

```
backend/include/jobpilot/network/
├── event_loop.h        # ① EventLoop：epoll 封装 + RunInLoop + Wakeup
├── poller.h            #    Poller 抽象
├── epoll_poller.h      #    epoll 实现（LT 起步）
├── channel.h           #    Channel：fd+事件+回调
├── buffer.h            # ② Buffer：读写缓冲（解决半包/粘包）
├── tcp_connection.h    # ③ TcpConnection：连接对象
└── tcp_server.h        # ④ TcpServer：accept + 分发

backend/include/jobpilot/http/
├── http_request.h      # ⑤ HTTP 解析结果对象
├── http_response.h     #    响应对象
├── http_router.h       # ⑥ Router：RegisterRoute(method, path, handler)
└── http_server.h       #    把 TcpServer + Router 组合成 HTTP Server

backend/src/network/*.cc        # 与头文件一一对应
backend/src/http/*.cc
backend/tests/test_buffer.cc    # → target test_buffer
backend/tests/test_http_request.cc  # → target test_http_request
service/src/main.cc             # 装配：HttpServer + Router 注册 /health /hello
service/src/router.cc           # 填充正式路由
service/src/health_stub.cc      # ★ P1 完成后删除（见 6.2 ⑧）
```

## 6.2 怎么做（每个子任务的验收命令）

### ① EventLoop + Poller + Channel（先做，最核心）

- **干什么**：一个线程一个 EventLoop；`Loop()` 里 `epoll_wait` 拿到就绪 fd → 找到 Channel → 调回调。
- **关键设计**：
  - `Channel` 持有 fd + `ReadCallback/WriteCallback/CloseCallback` + 关注事件集合。
  - `EventLoop::RunInLoop(cb)`：本线程直接执行；跨线程 push 到 pendingFunctors_ + `Wakeup()`（用 eventfd 或 pipe 写一字节唤醒 epoll）。
  - `UpdateChannel/RemoveChannel` 接口给上层用。
- **自测**：写一个简单测试——注册一个 timer（或 eventfd）触发回调，`Loop()` 跑几轮退出。
- **面试点**：为什么跨线程唤醒要写一字节？为什么 Channel 要记录上次事件？LT vs ET 区别？

### ② Buffer

- **干什么**：非阻塞读写需要用户态缓冲。实现 `readFd`（配合栈上 extrabuf 处理 EAGAIN）、`writeFd`、`ReadableBytes/WritableBytes/Retrieve`。
- **自测**：test_buffer.cc 覆盖：写入→读取→扩容→跨两次 read 拼装。
- **面试点**：为什么 Buffer 能解决半包/粘包？`readv` 配合栈缓冲的妙处。

### ③ TcpConnection

- **干什么**：封装一个连接：Buffer 输入输出 + 状态（connecting/connected/disconnecting/disconnected）+ 事件回调（消息到达/关闭/可写）。
- **关键**：处理**对端关闭**（read 返回 0 → CloseCallback）与**写半关闭**（shutdown）。
- **自测**：本阶段用 TcpServer 一起测。

### ④ TcpServer

- **干什么**：`listen` + `accept4`（非阻塞 + CLOEXEC）→ 新连接建 TcpConnection → 分发回调。
- **线程模型（本阶段简化）**：**单线程 EventLoop 先跑通**，线程池在 ⑦ 加。先单线程把全链路通了，再上线程池，这是最稳的路径。
- **自测**：`curl http://localhost:8080/hello` 返回 JSON；`wrk -t2 -c100 -d5s http://localhost:8080/hello` 看 QPS。

### ⑤ HTTP 解析状态机

- **干什么**：把 Buffer 里的字节流解析成 HttpRequest（方法/路径/版本/头/体）。状态：`kParseRequestLine → kParseHeaders → kParseBody → kParseComplete`。
- **关键**：
  - 用 `Content-Length` 判断 body 边界；`Connection: keep-alive` 处理连接复用。
  - 只支持 GET/POST + JSON body；**不支持 chunked / HTTPS / websocket**（止损线）。
  - 解析完把剩余字节留在 Buffer 里（管道中可能有下个请求）。
- **自测**：test_http_request.cc 覆盖：正常请求、带 body 的 POST、半包分两次喂、粘包两个请求连发、非法请求返回 400。
- **面试点**：状态机每个状态干什么、粘包半包怎么处理、Content-Length 缺失怎么办。

### ⑥ Router

- **干什么**：`RegisterRoute(HttpMethod, path, handler)`；路径匹配支持精确匹配 + `{id}` 参数（如 `/api/v1/jobs/{id}`）。**静态路径（如 `/statistics`）必须优先于参数路径匹配**（注册顺序中静态在前）。
- **自测**：注册 `/health`、`/hello`、`/api/v1/jobs/{id}`，curl 分别验证。

### ⑦ 线程池接入

- **干什么**：EventLoop 收连接 → 业务回调放进线程池执行（`ThreadPool::Submit(cb)`），写响应时回到连接。本阶段最简单的做法：**把 Router 的 handler 丢进线程池**，handler 内构造 HttpResponse，通过回调回写。
- **注意**：响应回写要线程安全——用 `RunInLoop` 把"写响应"动作投回 IO 线程，避免多线程同时写一个 fd。
- **自测**：wrk 压测 QPS 提升；并发下无崩溃、无乱序响应。

### ⑧ 删除 health_stub（V1.1 修正：明确收尾动作）

- 完成 HttpServer 且 /health 由 Router 正式提供后：**删除 `service/src/health_stub.cc`、`health_stub.h`，并从 service/CMakeLists.txt 移除引用**。
- 验收确认：`grep -r "RunHealthStub" service/` 无结果；全项目只有一套 HTTP Server（HttpServer）。

## 6.3 验收标准（C1）

- [ ] `curl -X POST http://localhost:8080/hello -d '{"a":1}'` 返回 JSON（非 404）
- [ ] `wrk -t2 -c100 -d5s http://localhost:8080/hello` QPS > 1 万（本机回环即可；不行先别纠结，记录数字）
- [ ] `ctest --test-dir build --output-on-failure` 全绿：test_buffer（半包/粘包）、test_http_request（解析状态机）全过
- [ ] 压测 100 并发 30 秒无崩溃、无内存暴涨（`ps` 观察）
- [ ] `grep -r "RunHealthStub" service/` 无结果；/health 由 HttpServer 提供（V1.1 修正：新增验收项）
- [ ] 代码无 warning（-Werror）、符合 06 规范（命名/分层）

## 6.4 常见坑

| 坑 | 解法 |
|---|---|
| ET 模式漏读（EAGAIN 未处理完） | 本阶段用 LT 起步，简单可靠；ET 留到有把握再切 |
| accept 惊群 | 单线程 accept 无此问题；多线程时用 SO_REUSEPORT 或锁，本阶段不管 |
| 连接不关闭/句柄泄漏 | TcpConnection 析构 close(fd)，测试后 `ss -lntp` 检查 |
| 压测 QPS 上不去 | 先确认 keep-alive 生效（wrk 依赖），再查是否每请求新建连接 |
| 响应乱序 | 响应写回必须回到 IO 线程（RunInLoop），见 ⑦ |
| 半包没处理完就解析 | 解析函数返回"还需要更多数据"，等下一次可读事件 |
| 忘删 health_stub | P1 收尾动作 ⑧ 必须做，否则两套 Server 并存（面试被问会很尴尬） |

## 6.5 止损线

- 只做 HTTP/1.1 子集：GET/POST + JSON。**不做 HTTPS、chunked、websocket**。
- 网络层卡 > 1 周 → 冻结缺陷，先进 P2（P2 业务在已有基础上继续，网络层并行打磨）。
- wrk QPS 不达标**不是**卡点：先记录数据，P5 再优化（面试讲"我知道瓶颈在哪"即可）。

---

# 7. Phase 2 — 业务闭环（2-3 周）→ 检查点 C2 ⭐⭐⭐⭐⭐

## 7.1 目标

注册/登录/简历/岗位/投递全链路能跑通。**先有"产品"，再谈 AI**。

## 7.2 子任务顺序与文件清单

```
backend/include/jobpilot/database/mysql_pool.h     # ① MySQL 连接池
backend/include/jobpilot/cache/redis_client.h      # ② Redis 客户端
backend/include/jobpilot/cache/session_manager.h   #     Session 管理
service/include/jobpilot/service/                  # ③④⑤⑥ 模块接口（IxxxService）
service/src/user/user_controller.cc + user_service.cc + user_dao.cc + user_dao_interface.h
service/src/resume/resume_controller.cc + resume_service.cc + resume_dao.cc + ...
service/src/job/job_controller.cc + job_service.cc + job_dao.cc + ...
service/src/application/application_controller.cc + application_service.cc + application_dao.cc + ...
service/src/router.cc    # 挂载全部路由
database/schema/*.sql    # 03 文档 DDL 已按域拆分落地；种子数据后续单独纳入
service/tests/test_*.cc  # Service/DAO 单测（mock 或内存实现）
```

## 7.3 怎么做

### ① MySQL 连接池（backend 引擎层，最终方案 V1.1 修正：实现细节定稿）

**接口**：`ConnectionPool::Get()` / `Return(MYSQL*)` / `Query(sql, params) -> Result` / `Execute(sql, params)` / `Transaction`。

**实现契约（照此实现，不再有"或"）**：

```
数据结构：
  - std::deque<MYSQL*> idle_           // 空闲连接
  - std::mutex mtx_ + std::condition_variable cv_
  - size_t total_                      // 已创建连接数（≤ max_conn）
  - size_t max_conn_                   // 从 config.mysql.max_conn 读（默认 10）

Get(timeout_ms=5000)：
  1. lock
  2. idle_ 非空 → 取出队首 MYSQL*，先 mysql_ping 健康检查：
        ping 通过 → return
        ping 失败 → mysql_close + total_-- → 走第 3 步重新获取
  3. idle_ 空：
        total_ < max_conn_ → mysql_init + mysql_real_connect（失败则 total_ 不加，抛错）
        total_ >= max_conn_ → cv_.wait_for(5s)：
            被唤醒 → 回到 2
            超时 → 抛"connection pool timeout"（返回错误码 60001）
Return(MYSQL*)：
  1. lock
  2. mysql_ping 通过 → push 回 idle_；失败 → mysql_close + total_--
  3. cv_.notify_one()

RAII 封装（QueryGuard / ExecuteGuard）：构造 Get，析构 Return；异常路径也必须归还。
预处理语句（防注入）：mysql_stmt_prepare + 参数绑定 + mysql_stmt_execute；禁止字符串拼接 SQL。
Transaction：BEGIN → 多次 Execute → COMMIT/ROLLBACK（同一连接的连续操作）。
```

**验收**：能连上 docker mysql；小测试循环 Query 100 次不泄漏（`valgrind` 可选）；并发 8 线程同时 Query 不崩溃。

### ② Redis 客户端 + Session（backend 引擎层）

- **接口**：`Get/Set/SetEx/Del/Incr`（hiredis）。
- **Session**：登录成功 → `SETEX session:{token} 86400 userId`；鉴权中间件 → `GET session:{token}`。
- **验收**：`SETEX` 后 `GET` 命中，过期后消失（TTL 24h 对齐 04）。

### ③ user 模块（注册/登录/画像/技能）

- **接口**（对齐 04 §3）：`POST /users/register`、`POST /users/login`、`GET /users/me`、`GET|PUT /users/profile`、`GET|POST /users/skills`、`DELETE /users/skills/{id}`。
- **实现要点**：bcrypt 哈希（**vendored libbcrypt**：`third_party/libbcrypt/bcrypt.h` 的 `bcrypt_hashpw`，V1.1 修正：明确用法）存 `password_hash`；注册查 `email` 唯一；登录校验后发 token 存 Redis；画像 1:1 表；技能 `(user_id,name)` 唯一。
- **验收**：注册→登录→me；重复注册返回 30001；密码库里不是明文。

### ④ resume 模块（上传/版本）

- **接口**（对齐 04 §4）：`POST /resumes/upload`（multipart）、`GET /resumes`、`POST /resumes/{id}/versions`、`GET /resumes/{id}/versions`。
- **实现要点**：**multipart 解析限时 2 天**，只支持单文件 PDF/TXT/MD；文件落盘 `<storage.root>/{user_id}/resumes/{resume_id}/v{version}.{ext}`；`resume_versions` 插记录；文本内容存 `content` 列（供 AI 用）。
- **止损**：multipart 2 天搞不定 → 改 **base64 JSON 上传**（面试不问这个细节）。
- **验收**：上传→列表→新版本→版本列表；下载文件与上传一致（base64 方案则验 content）。

### ⑤ job 模块（岗位 CRUD/搜索）

- **接口**（对齐 04 §5）：`GET /jobs`（keyword/location/技能过滤+分页）、`GET /jobs/{id}`、`POST /jobs`（手工录入，管理员）。
- **实现要点**：LIKE 搜索初期够用（03 §5 注明）；`job_requirements` 技能拆分；`source_url` 唯一（手工录入可不填→NULL 不冲突）。
- **验收**：搜索分页正确；重复 source_url 插入被拒。

### ⑥ application 模块（投递/状态机/统计）

- **接口**（对齐 04 §6）：`POST /applications`、`GET /applications?status=`、`PUT /applications/{id}/status`、`GET /applications/statistics`。
- **状态机**（02 §5.4，Service 层校验）：

```
0 收藏 → 1 待投递 → 2 已投递 → 3 笔试 → 4 一面 → 5 二面 → 6 Offer
                        ↘                     ↘
                         7 拒绝（任意前置状态可进入，终态）
```

- **实现要点**：`(user_id,job_id)` 唯一防重复投递；状态迁移校验表（`canTransition(from,to)` 纯函数）；统计按状态/时间分组。
- **验收**：重复创建投递返回 40004；`6 Offer` 后再转 `4 一面` 被拒（40005）；统计数字正确。

### ⑦ schema SQL 落地（database/schema/）

- `database/schema/01_user.sql` 至 `database/schema/06_notification.sql`：按 03 文档 DDL 分域建表（users/profiles/skills/resumes/resume_versions/jobs/job_requirements/applications/ai_tasks/interview_records/notifications），**全部 `CREATE TABLE IF NOT EXISTS`**。
- 示例用户 + 示例岗位种子数据尚未纳入当前 P0；后续加入时必须使用 `INSERT IGNORE`。
- 幂等可重复执行；**重置数据**：`docker compose down -v && docker compose up -d mysql`（V1.1 修正：明确重置命令）。

## 7.4 验收标准（C2）

- [ ] 全链路 curl 脚本跑通：注册→登录→建画像→加技能→传简历→手工录岗位→投递→改状态（2→3→4→5→6）→统计
- [ ] 非法状态迁移被拒（如 6 Offer 后转 4）
- [ ] 重复投递被拒（40004）；重复邮箱注册被拒（30001）
- [ ] 未登录访问受保护接口返回 401（20001）
- [ ] 用户 A 不能访问用户 B 的资源（越权校验，NFR-S05）
- [ ] SQL 全部参数化（无字符串拼接）；关键写入在事务里
- [ ] Service/DAO 单测覆盖核心路径（状态机、唯一键冲突）→ target `test_user_service` 等
- [ ] `ctest --test-dir build --output-on-failure` 全绿

## 7.5 常见坑

| 坑 | 解法 |
|---|---|
| 连接池在多线程下崩溃 | 连接获取/归还必须加锁；连接不能跨线程共用（libmysqlclient 非线程安全），池内加锁保证同一时刻一个连接只被一个线程使用 |
| bcrypt 链接失败 | vendored libbcrypt 直接编进 jobpilot_core，无第三方链接；确认 include 路径加入 `third_party/libbcrypt` |
| multipart 解析 | 限时 2 天；超时改 base64（止损线） |
| 状态机漏校验 | 把所有合法迁移画成表，写 `canTransition` 纯函数 + 单测 |
| 越权 | 每个 DAO 查询强制带 `user_id` 条件；Controller 从 token 取 userId 而不是信 body |
| 重置数据库 | `docker compose down -v && docker compose up -d mysql`（init 脚本只在空卷执行） |

## 7.6 止损线

- 前端本阶段**只做最小验证页**（可选，甚至用 curl 演示即可）。
- 状态机只做正向迁移 + 基本校验，不做复杂权限系统。
- 不做文件预览/PDF 解析成结构化（只存 content 原文，解析是 AI 的事）。

---

# 8. Phase 3 — 异步 AI（1-1.5 周）→ 检查点 C3 ⭐⭐⭐⭐

## 8.1 目标

**用 ai_tasks 表 + 轮询打通异步闭环，暂不引入 RabbitMQ**（P4 再升级）。发起匹配 → 轮询 → 返回结构化结果。

## 8.2 写什么文件

```
database/schema/05_ai.sql          # ai_tasks + interview_records（canonical schema fragment，见 7.3 ⑦）
service/src/ai/ai_task_service.cc  # C++ 侧：建任务/查状态/轮询接口
service/src/ai/ai_task_dao.cc
service/src/ai/ai_task_controller.cc
ai_service/
├── app/
│   ├── main.py            # FastAPI 入口：/internal/ai/health + lifespan 启动后台轮询线程
│   ├── config.py          # 读环境变量（DB/Redis/DeepSeek key）
│   ├── poller.py          # 后台线程：CAS 抢单（见 8.3 ③）→ 处理 → 回写
│   ├── services/
│   │   ├── rule_score.py  # ① 规则分（技能命中/年限/学历/地点）
│   │   └── matcher.py     # 三层管线组装
│   └── llm/
│       ├── llm_client.py  # DeepSeek（OpenAI 兼容）调用
│       └── embedder.py    # bge-small-zh（可选，失败用 TF-IDF 兜底）
├── requirements.txt       # fastapi uvicorn pymysql redis openai (sentence-transformers 可选)
└── tests/test_matcher.py
```

## 8.3 怎么做

### ① 接口（对齐 04 §7）

- `POST /api/v1/ai/match` body `{"job_id":20001,"resume_id":30001}` → 立即返回 `{"task_id":1024}`
- `GET /api/v1/ai/tasks/{task_id}` → `{status: 0/1/2/3, result: {...}, error_msg}`

### ② C++ 侧

- 创建任务：INSERT `ai_tasks(user_id, task_type='job_match', job_id, resume_id, status=0)` → 返回 task_id（**接口不阻塞，立即返回**）。
- 查询任务：SELECT 状态 + result；`status=2` 时顺带读 Redis 缓存 `ai:match:{job}:{resume}`（命中直接返回，不查库）。
- **超时补偿线程（最终方案，V1.1 修正）**：C++ 启动一个后台线程（每 60s 跑一次）：

```
扫描：SELECT id FROM ai_tasks
      WHERE status=1 AND update_time < NOW() - INTERVAL 5 MINUTE
处理：retry_count < 3 → UPDATE status=0, retry_count=retry_count+1
      retry_count >= 3 → UPDATE status=3, error_msg='processing timeout'
      （P4 该线程还负责"重新发布 MQ"，见 9.4 ④）
```

### ③ AI 服务轮询线程（最终方案：CAS 抢单，V1.1 修正：不用 FOR UPDATE SKIP LOCKED）

- **lifespan 启动后台线程**，每 2s 执行一次：

```
1. 抢单（原子 CAS，两段式）：
   a. SELECT id FROM ai_tasks WHERE status=0 ORDER BY id LIMIT 1
   b. UPDATE ai_tasks SET status=1, update_time=NOW() WHERE id=? AND status=0
      → 受影响行数 = 1 表示抢到；= 0 说明被并发抢走，跳过
2. 抢到后：SELECT 完整任务（job_id/resume_id）→ 处理（三层管线）
3. 成功：UPDATE status=2, result=JSON + SETEX Redis 缓存（TTL 24h）
4. 失败：UPDATE status=3, error_msg=...（LLM 类失败按 8.3 ⑤ 降级，不置 3）
5. sleep 2s，回到 1
```

- **为什么不用 `FOR UPDATE SKIP LOCKED`**：单消费者轮询场景下 CAS UPDATE 更简单、无事务开销、效果等价；面试可讲"原子 CAS 抢单"（V1.1 修正：明确最终方案）。

### ④ 三层匹配管线（02 §10，最终方案）

1. **规则分（0-100，可解释基线）**：读岗位 `job_requirements` 技能 + 简历 `content` 文本，算技能命中率/关键词；无 LLM 也能出分。
2. **Embedding（可选）**：简历全文 vs 岗位全文余弦相似度（bge-small-zh）；下载失败 → TF-IDF 兜底。
3. **LLM 综合分**：把基线分 + JD + 简历摘要发给 DeepSeek，要求输出 JSON `{"score":0-100,"strengths":[],"gaps":[],"suggestions":[]}`。

### ⑤ 降级（最终方案）

- LLM 调用失败/超时（重试 1 次后仍失败）→ **直接用规则分填充 result**（标注 `"degraded": true`），任务仍置 status=2，不崩溃。
- 只有"规则分都算不出来"（如简历 content 为空）才置 status=3 failed。
- **DeepSeek**：OpenAI 兼容协议，`base_url="https://api.deepseek.com"`，model `deepseek-chat`；Key 只走环境变量 `LLM_API_KEY`。

### ⑥ 前端轮询（P3 可先不做页面，用 curl 验证）

## 8.4 验收标准（C3）

- [ ] `POST /ai/match` 立即返回 task_id（<100ms）
- [ ] 轮询到 `status=2`，result 含 score/strengths/gaps/suggestions
- [ ] 同 job+resume 二次发起命中 Redis 缓存（不再调 LLM——看 AI 服务日志无新调用）
- [ ] 把 DeepSeek key 设为无效 → 任务仍 success，result 为规则分且 `degraded:true`
- [ ] ai_tasks 状态流转正确：0→1→2（或 0→1→3 failed 可重试）
- [ ] **超时补偿验证**：手动把一条任务置 status=1 且 update_time 改为 10 分钟前 → 60s 内被补偿线程重置为 status=0（V1.1 修正：新增验收项）

## 8.5 常见坑

| 坑 | 解法 |
|---|---|
| 后台线程与 uvicorn 同进程 | 用 FastAPI lifespan（`@asynccontextmanager`）启动 threading.Thread，避免阻塞事件循环 |
| 并发轮询重复处理 | CAS UPDATE（见 8.3 ③）：`UPDATE ... WHERE id=? AND status=0`，影响行数=1 才算抢到 |
| DeepSeek 返回非 JSON | prompt 明确要求"只输出 JSON"，解析失败重试 1 次后降级规则分 |
| embedding 模型下载 100MB 慢 | 可选；失败立即用 TF-IDF，不阻塞（止损线） |
| MySQL 直连凭据 | 走环境变量（AI 服务容器内注入 MYSQL_HOST/MYSQL_PASSWORD） |

## 8.6 止损线

- **禁止碰 LangChain/Agent**（Deferred 清单）。
- 匹配管线只保证"能出分、能解释"，**不追求准确率**。
- embedding 失败 → TF-IDF 顶替。

## 8.7 ADR-0006 摘要（写入本手册即生效，面试可引用）

> **决策**：MVP 阶段允许 AI 服务直连 MySQL 读写 `ai_tasks`（AI 域表）并直读 `jobs`/`resume_versions` 内容，结果缓存 Redis。**理由**：单人 MVP 下减少一层内部 HTTP 往返，交付最快；`ai_tasks` 属 AI 域，不违反"业务库归属"边界。**后果**：正式形态（多人/拆分）应改为经 C++ Backend 内部 HTTP `/internal/ai/*` 取数与回写，保持"AI 不直连业务库"。面试讲"先直连跑通，再切内部 HTTP 的演进路径"是加分项。
>
> **AI 服务数据访问边界（最终方案，V1.1 修正：新增访问矩阵）**：

| 表 | 读 | 写 | 说明 |
|---|---|---|---|
| `ai_tasks` | ✅ | ✅ | AI 域表，全权 |
| `jobs` | ✅ | ❌ | 只读 description/requirements，供匹配 |
| `resume_versions` | ✅ | ❌ | 只读 content（简历文本） |
| `users` / `profiles` / `applications` 等其余表 | ❌ | ❌ | 禁止访问 |

---

# 9. Phase 4 — RabbitMQ 升级（1.5-2 周）→ 检查点 C4 ⭐⭐⭐

## 9.1 目标

用 RabbitMQ 替换轮询作为主通道，展示完整异步架构（ADR-0004 目标形态）。**面试重点，认真做。**

## 9.2 拓扑（最终方案，V1.1 修正：含 DLX）

```
C++ Backend ──publish──► ai.task.job_match (durable, 带 DLX 参数)
                              │
                 ┌────────────┴────────────┐
                 ▼                         ▼
        AI 服务 pika Consumer      失败 requeue=true 重试（最多 3 次）
        （手动 ACK，prefetch=1）            │ 超限
                 │                   NACK requeue=false
                 ▼                         ▼
           ai_tasks 回写              ai.task.job_match.dlx（死信，留痕）
```

## 9.3 写什么文件

```
backend/include/jobpilot/mq/mq_producer.h   # ① C++ Producer（amqp-cpp）
backend/src/mq/mq_producer.cc
ai_service/app/consumers/job_match_consumer.py  # ② AI 消费端（pika）
ai_service/app/consumers/__init__.py
docker-compose.yml                           # ③ 追加 rabbitmq/backend/ai_service 服务
service/src/ai/ai_task_service.cc            # ④ 改造：建任务 → 发 MQ；补偿线程扩展
```

**service/conanfile.txt（P4 追加 amqp-cpp 后，完整版）**：

```
[requires]
spdlog/1.14.1
yaml-cpp/0.8.0
nlohmann_json/3.11.3
gtest/1.14.0
hiredis/1.2.0
amqp-cpp/4.3.1

[generators]
CMakeToolchain
CMakeDeps
```

> V1.1 修正：包名是 `amqp-cpp`（Conan Center 官方名，不是 amqpcpp）；CMake 里 `find_package(amqp-cpp CONFIG REQUIRED)` → 链接 `amqp-cpp::amqp-cpp`（其 Boost.Asio 依赖由 Conan 自动处理）。

## 9.4 怎么做（最终方案，V1.1 修正：ACK/NACK/重试/死信/补偿全部定稿）

### ① C++ Producer（amqp-cpp）

- 封装 `MQProducer::Publish(queue, message)`：连接管理 + 自动重连（指数退避：1s/2s/4s...上限 30s）+ 失败回调。
- **发布失败降级**：publish 异常 → 任务保持 `status=0` 留在表里（不返回错误给用户，task_id 照常返回），由补偿线程兜底（见 ④）。

### ② AI Consumer（pika，消费语义最终方案）

**队列声明**（消费端与生产端声明参数必须一致）：

```
exchange:    ai.dlx（direct, durable）                       # 死信 exchange
queue:       ai.task.job_match（durable）
             参数: x-dead-letter-exchange = ai.dlx
                   x-dead-letter-routing-key = job_match.dlx
queue:       ai.task.job_match.dlx（durable）                # 死信队列
             binding: exchange=ai.dlx, routing_key=job_match.dlx
```

**消费循环**（`basic_qos(prefetch_count=1)`，`auto_ack=False`）：

```
收到消息 → 解析出 task_id：
  1. 幂等检查：SELECT status FROM ai_tasks WHERE id=?
     → status IN (2,3) 表示已处理过（重复投递）→ basic_ack 跳过
  2. 抢占：UPDATE ai_tasks SET status=1, update_time=NOW() WHERE id=? AND status=0
     → 影响行数=0 说明被兜底轮询抢走 → basic_ack 跳过（该任务会由兜底完成）
  3. 处理（三层管线，同 8.3 ④）
  4. 成功 → UPDATE status=2, result + Redis 缓存 → basic_ack
  5. 失败：
     retry_count < 3（MAX_RETRY=3）：
       UPDATE retry_count=retry_count+1, status=1
       sleep 退避（第 n 次失败 sleep 2^n 秒：1s/2s/4s）
       → basic_nack(requeue=True)          # 回队重试
     retry_count >= 3：
       UPDATE status=3, error_msg=...
       → basic_nack(requeue=False)         # 进 DLX 死信队列留痕
  6. 消费者崩溃（未 ACK）→ RabbitMQ 自动重新投递 → 由幂等检查兜底
```

**重试上限定稿**：`MAX_RETRY = 3`（ai_tasks.retry_count 累计，业务侧与队列侧一致）。

### ③ RabbitMQ 服务（compose 追加）

```yaml
  rabbitmq:
    image: rabbitmq:3-management
    ports: ["5672:5672", "15672:15672"]
    volumes: [rabbitmq_data:/var/lib/rabbitmq]
    healthcheck:
      test: ["CMD", "rabbitmq-diagnostics", "-q", "ping"]
      interval: 10s
      timeout: 5s
      retries: 5
```

### ④ C++ 侧改造 + 补偿机制（最终方案）

- `POST /ai/match` 流程改为：INSERT ai_tasks(status=0) → **publish `ai.task.job_match`**（消息体含 task_id/user_id/job_id/resume_id）→ 返回 task_id。publish 失败不报错（走补偿）。
- **C++ 补偿扫描线程扩展**（每 60s，与 P3 超时补偿合并为一个线程）：

```
① 超时重置：status=1 AND update_time < NOW()-5min
   → retry_count<3 ? 置 status=0 : 置 status=3('processing timeout')
② 重新发布：status=0 AND create_time < NOW()-2min   # 说明 MQ 消费没跟上或消息丢了
   → 重新 publish（若 publish 失败忽略，下轮再试；AI 兜底轮询也会捞到）
```

- **AI 兜底轮询线程（保留，与 MQ 并行，最终方案）**：每 5s 执行 CAS 抢单，但**只抢"老任务"**：

```
UPDATE ai_tasks SET status=1, update_time=NOW()
WHERE status=0 AND update_time < NOW() - INTERVAL 2 MINUTE
ORDER BY id LIMIT 1
```

> 原理：正常 MQ 流转的任务 status 很快变 1/2，不会被兜底抢到；只有 MQ 挂了/消息丢了/发布失败的任务（停留 status=0 超过 2 分钟）才被兜底捞走。**双通道互不冲突，靠 CAS + 时间窗保证**（面试讲这个设计是加分项）。

### ⑤ 幂等验证方法

- 手动向队列投同一条消息 2 次（管理界面 15672 的 Publish 或脚本）→ 只处理 1 次。

## 9.5 验收标准（C4）

- [ ] RabbitMQ 管理界面（15672）能看到队列与消息流转
- [ ] 发起匹配 → 队列消费 → 回写 ai_tasks → 前端轮询拿到结果（全链路）
- [ ] **幂等验证**：手动往队列投同一条消息 2 次 → 只处理 1 次
- [ ] **失败重试**：停掉 AI 服务 → 消息留在队列（未 ACK）→ 重启 AI → 消息被消费
- [ ] **重试上限 + 死信**：让处理持续抛错 4 次 → 消息进 `ai.task.job_match.dlx`，ai_tasks 置 status=3
- [ ] **降级验证**：停掉 RabbitMQ → 发匹配请求 → 2 分钟内任务仍被兜底轮询完成（不崩溃）
- [ ] **补偿验证**：手动把一条 status=0 的旧任务 create_time 改为 10 分钟前 → 60s 内被重新 publish 或被兜底抢走

## 9.6 常见坑

| 坑 | 解法 |
|---|---|
| amqp-cpp 链接失败 | Conan 包名 `amqp-cpp`；CMake target `amqp-cpp::amqp-cpp`；Boost 依赖由 Conan 自动处理 |
| 手动 ACK 忘记调 | 不 ACK 消息会一直堆积，管理界面看得到；确认每分支都 ACK/NACK |
| requeue 无限循环 | retry_count 上限 3 次 + 退避 sleep；超限 NACK requeue=False 进死信 |
| 重复消费 | 幂等靠 ai_tasks 状态机（步骤 1），队列只负责投递——这是正确分工 |
| 重连风暴 | 连接断开用指数退避重连，别死循环 |
| 兜底与 MQ 双消费 | 兜底只捞 status=0 且超过 2 分钟的（见 9.4 ④），CAS 保证不冲突 |

## 9.7 止损线（重要）

- C++ 集成 amqp-cpp 卡 > 1 周 → **切双路径**：AI 服务用 pika 消费 + C++ 侧失败回退写表（补偿轮询兜底）——MQ 链路仍然真实存在并跑通，只是 C++ 不直接连 MQ。
- 再不行 → 保持 P3 轮询版；面试照样讲"异步状态机 + 队列解耦"的设计，这是可接受的降级，不丢人。

---

# 10. Phase 5 — 前端 + 部署 + GitHub 开源（1.5-2 周）→ 检查点 C5 = 最终验收

## 10.1 目标

可一键演示的完整产品 + 开源交付。**新机器 `git clone && docker compose up --build` 就能演示。**

## 10.2 写什么文件

```
frontend/
├── package.json / vite.config.js        # Vue3 + Vite
├── src/
│   ├── main.js / App.vue                # 路由 + 布局
│   ├── api/http.js                      # Axios 封装：统一响应 + token 拦截器
│   ├── views/Login.vue                  # ① 登录/注册
│   ├── views/Jobs.vue                   # ② 岗位列表/搜索/详情
│   ├── views/Applications.vue           # ③ 投递管理 + 状态流转 + 统计卡片（ECharts 1 张图）
│   └── views/AI.vue                     # ④ AI 匹配分析（发起→2s 轮询→结果展示）
docker/
├── backend/Dockerfile                   # 多阶段：conanio 构建 → ubuntu 运行
├── ai/Dockerfile                        # python:3.10-slim + uvicorn（消费线程由 lifespan 启动）
├── gateway/Dockerfile                   # node 构建前端 → nginx 托管 + 反代
└── nginx.conf                           # 最终方案见 10.3 ①（V1.1 修正）
.github/workflows/ci.yml                 # 最终方案见 10.3 ⑦（V1.1 修正）
README.md                                # 完整版：架构图/快速开始/截图/合规声明
```

## 10.3 怎么做

### ① nginx.conf（最终方案，V1.1 修正：`limit_req_zone` 移到 http 顶层 + map 豁免 GET/HEAD）

> nginx 语法约束：`limit_req_zone` 只能出现在 **http 上下文**（即 `server{}` 之外）。V1.0 误写在 server{} 内会导致 nginx 启动失败。
> 同时用 `map` 让 GET/HEAD 的 key 为空字符串（空 key 不限流），解决"AI.vue 每 2s 轮询 = 30 req/min 恰好触发限流 503"的问题（V1.1 修正）。

```nginx
# ===== http 上下文（顶层，绝不能放进 server{}）=====
limit_req_zone $binary_remote_addr zone=api:10m rate=30r/m;

# GET/HEAD（含 AI 轮询）不限流；POST/PUT/DELETE 按 IP 限 30r/m
map $request_method $rate_limit_key {
    default $binary_remote_addr;
    GET "";
    HEAD "";
}
limit_req_zone $rate_limit_key zone=api_write:10m rate=30r/m;

server {
    listen 80;

    # 前端静态（SPA，产物由 gateway 多阶段构建 COPY 进镜像）
    location / {
        root /usr/share/nginx/html;
        try_files $uri $uri/ /index.html;
    }

    # 后端 API（写操作限流 + 反代）
    location /api/ {
        limit_req zone=api_write burst=50 nodelay;
        proxy_pass http://backend:8080;
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
    }

    # 健康检查
    location /health {
        proxy_pass http://backend:8080/health;
    }
}
```

### ② gateway 镜像（前端产物进镜像）

```dockerfile
# docker/gateway/Dockerfile
FROM node:20-alpine AS fe-builder
WORKDIR /fe
COPY frontend/package*.json ./
RUN npm install
COPY frontend/ ./
RUN npm run build

FROM nginx:1.25-alpine
COPY --from=fe-builder /fe/dist /usr/share/nginx/html
COPY docker/nginx.conf /etc/nginx/conf.d/default.conf
```

### ③ backend Dockerfile（多阶段）

```dockerfile
# docker/backend/Dockerfile（构建上下文 = 仓库根，由根目录 docker-compose.yml 保证）
FROM conanio/gcc13-ubuntu22.04 AS builder
WORKDIR /src
COPY backend/ ./backend
COPY service/ ./service
COPY third_party/ ./third_party
WORKDIR /src/service
RUN apt-get update && apt-get install -y libmysqlclient-dev libssl-dev pkg-config \
 && conan install . -of build --build=missing \
 && cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
 && cmake --build build -j$(nproc)

FROM ubuntu:22.04
RUN apt-get update && apt-get install -y libssl3 libmysqlclient21 && rm -rf /var/lib/apt/lists/*
WORKDIR /app
COPY --from=builder /src/service/build/bin/jobpilot_server ./jobpilot_server
COPY service/config/config.yaml ./config/config.yaml
EXPOSE 8080
ENTRYPOINT ["./jobpilot_server", "--config=config/config.yaml"]
```

> 注意：运行镜像必须装 `libmysqlclient21`（libmysqlclient-dev 的运行时库），否则启动报找不到 `libmysqlclient.so`。

### ④ AI Dockerfile（单进程：uvicorn + 后台消费线程）

```dockerfile
FROM python:3.10-slim
WORKDIR /app
COPY ai_service/requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt
COPY ai_service/ .
CMD ["uvicorn", "app.main:app", "--host", "0.0.0.0", "--port", "8000"]
```

> AI 消费线程由 `main.py` lifespan 启动（P3 轮询 / P4 pika + 兜底轮询），一个容器搞定，不需要单独 consumer 容器。

### ⑤ 前端 4 页（最小集）

- **Login.vue**：登录/注册表单 → 存 token 到 localStorage。
- **Jobs.vue**：岗位列表（分页）+ 关键词搜索 + 详情抽屉。
- **Applications.vue**：投递列表 + 状态流转按钮（按状态机）+ **统计卡片（ECharts 1 张柱状图即可，不做大屏，V1.1 修正）**。
- **AI.vue**：选岗位+简历 → 发起匹配 → **2s 轮询**（GET 请求，nginx 已豁免限流，V1.1 修正）→ 展示 score/strengths/gaps/suggestions。
- **Axios 封装**：请求拦截器加 `Authorization: Bearer`；响应拦截器统一处理 `code!=0` 与 401 跳登录。

### ⑥ docker-compose.yml（根目录，最终完整版，V1.1 修正：移除 crawler、环境变量与 §3 表一致）

```yaml
services:
  mysql:
    image: mysql:8.0
    environment:
      MYSQL_ROOT_PASSWORD: ${MYSQL_ROOT_PASSWORD:?set MYSQL_ROOT_PASSWORD in .env}
      MYSQL_DATABASE: jobpilot
    ports: ["3306:3306"]
    volumes:
      - mysql_data:/var/lib/mysql
      - ./database/schema:/docker-entrypoint-initdb.d:ro
    healthcheck:
      test: ["CMD", "mysqladmin", "ping", "-h", "localhost"]
      interval: 10s
      timeout: 5s
      retries: 5

  redis:
    image: redis:7-alpine
    ports: ["6379:6379"]
    volumes: [redis_data:/data]
    healthcheck:
      test: ["CMD", "redis-cli", "ping"]
      interval: 10s
      timeout: 5s
      retries: 5

  rabbitmq:
    image: rabbitmq:3-management
    ports: ["5672:5672", "15672:15672"]
    volumes: [rabbitmq_data:/var/lib/rabbitmq]
    healthcheck:
      test: ["CMD", "rabbitmq-diagnostics", "-q", "ping"]
      interval: 10s
      timeout: 5s
      retries: 5

  backend:
    build:
      context: .
      dockerfile: docker/backend/Dockerfile
    ports: ["8080:8080"]
    environment:
      MYSQL_HOST: mysql
      MYSQL_USER: root
      MYSQL_PASSWORD: ${MYSQL_PASSWORD:?set MYSQL_PASSWORD in .env}
      MYSQL_DATABASE: jobpilot
      REDIS_HOST: redis
      RABBITMQ_HOST: rabbitmq
    depends_on:
      mysql: { condition: service_healthy }
      redis: { condition: service_healthy }
      rabbitmq: { condition: service_healthy }
    volumes: [storage:/app/storage]

  ai_service:
    build:
      context: .
      dockerfile: docker/ai/Dockerfile
    ports: ["8000:8000"]
    environment:
      MYSQL_HOST: mysql
      MYSQL_USER: root
      MYSQL_PASSWORD: ${MYSQL_PASSWORD}
      MYSQL_DATABASE: jobpilot
      REDIS_HOST: redis
      RABBITMQ_HOST: rabbitmq
      LLM_API_KEY: ${LLM_API_KEY:?set LLM_API_KEY in .env}
      LLM_BASE_URL: ${LLM_BASE_URL:-https://api.deepseek.com}
      LLM_MODEL: ${LLM_MODEL:-deepseek-chat}
    depends_on:
      mysql: { condition: service_healthy }
      redis: { condition: service_healthy }
      rabbitmq: { condition: service_healthy }

  gateway:
    build:
      context: .
      dockerfile: docker/gateway/Dockerfile
    ports: ["80:80"]
    depends_on: [backend]

volumes:
  mysql_data: {}
  redis_data: {}
  rabbitmq_data: {}
  storage: {}
```

> 服务名固定：`mysql` / `redis` / `rabbitmq` / `backend` / `ai_service` / `gateway`（全文档统一，V1.1 修正）。
> `.env` 放仓库根（compose 自动读取）：`MYSQL_ROOT_PASSWORD`、`MYSQL_PASSWORD`、`LLM_API_KEY`（`MYSQL_PASSWORD` 与 `MYSQL_ROOT_PASSWORD` 填相同值即可）。

### ⑦ CI（GitHub Actions，最终方案，V1.1 修正：单一构建入口）

```yaml
name: CI
on:
  push:
    branches: [main]
  pull_request:

jobs:
  build-and-test:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - name: Install deps
        run: |
          pip install conan
          sudo apt-get update && sudo apt-get install -y libssl-dev libmysqlclient-dev pkg-config
      - name: Configure (Conan)
        working-directory: service
        run: |
          conan profile detect
          conan install . -of build --build=missing
      - name: Build & Test (C++)
        working-directory: service
        run: |
          cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
          cmake --build build -j2
          ctest --test-dir build --output-on-failure
      - name: Test (AI Service)
        working-directory: ai_service
        run: |
          pip install -r requirements.txt
          python -m pytest -q
```

### ⑧ README（开源门面）

- 项目简介 + 架构图（docs/02 §2 的 mermaid 或截图）
- 快速开始：`git clone` → `cp .env.example .env` → 填 MYSQL_ROOT_PASSWORD/MYSQL_PASSWORD/LLM_API_KEY → `docker compose up -d --build`
- 演示截图（4 个页面）
- 技术栈徽章 + 合规声明（不做自动投递、遵守 robots.txt）+ License
- 面试亮点段落（自研 HTTP Server / 异步 AI / MQ 解耦）

## 10.4 最终验收清单（C5 = 整项目验收）

- [ ] 在**干净的 Ubuntu 虚拟机**（或云服务器）上：`git clone` → `cp .env.example .env`（填 MYSQL_ROOT_PASSWORD/MYSQL_PASSWORD/LLM_API_KEY）→ `docker compose up -d --build` → 全部服务 healthy
- [ ] 浏览器访问 `http://<host>`：注册 → 登录 → 岗位列表（seed 数据可见）→ 投递 → AI 匹配 → 出结果
- [ ] RabbitMQ 管理界面可见消息流转
- [ ] `docker compose down && docker compose up -d` 数据不丢（volume 持久化）
- [ ] GitHub public 仓库：README 完整、CI 绿、LICENSE 存在
- [ ] 仓库无密钥：`git grep -i "sk-"` 无 LLM key；`.env` 在 `.gitignore`；`config.yaml` 无密码

## 10.5 常见坑

| 坑 | 解法 |
|---|---|
| backend 容器起不来：libmysqlclient.so 找不到 | 运行镜像装 `libmysqlclient21`（见 ③） |
| 前端 404 | nginx `try_files ... /index.html`；确认 gateway 镜像 COPY 了 dist |
| AI 容器连不上 MySQL | compose 里 `depends_on: mysql: condition: service_healthy` + 环境变量注入（§10.3 ⑥） |
| CORS | 同域部署（nginx 托管前端+反代 /api）则无 CORS；跨域再加 `add_header Access-Control-Allow-Origin` |
| docker 内存不足 | embedding 模型 100MB + 多个服务，建议 2C4G；内存不够就砍 embedding（TF-IDF 兜底） |
| nginx 起不来 | `nginx -t` 检查；`limit_req_zone` 必须在 http 顶层（V1.1 修正） |
| 演示时 AI 页 503 | 已修复：GET 轮询豁免限流（map 方案，V1.1 修正）；仍 503 就 `docker compose logs gateway` 看限流日志 |

## 10.6 止损线

- 前端砍到 4 页，**不做华丽大屏**。
- 部署卡住 → 至少保证 README 有本地启动步骤，演示用本地跑（VM 内），GitHub 只放代码+文档。
- CI 若因 Conan 缓存慢失败 → 先本地验证绿，CI 标"known issue"，不阻塞交付。

---

# 附录 A：检查点提交模板（复制即用）

```
【检查点 Cx - Phase x 提交】
1. 新增/修改文件清单：<列表>
2. 构建与测试输出：<粘贴关键输出>
3. 验收标准核对：<逐条"达成/未达成 + 证据">
4. 遇到的问题与决策：<没有写"无">
5. 与本文档的偏差：<没有写"无">
```

# 附录 B：常见坑速查（V1.1 修正：补充新坑）

| 类别 | 坑 | 一句话解法 |
|---|---|---|
| Conan | 拉包慢/失败 | 换镜像源；再失败 FetchContent |
| Conan | 包名写错 | `hiredis`、`amqp-cpp`（不是 amqpcpp）；target 见 §2.4 对照表 |
| 编译 | -Werror 过不去 | 修 warning，不关 -Werror |
| 编译 | 找不到 gtest/hiredis | 确认 `conan install` 在 service 目录执行过且 `find_package(... CONFIG REQUIRED)` 名字正确 |
| 网络 | 半包/粘包 | Buffer 累积 + 解析状态机返回"需要更多数据" |
| 网络 | 连接泄漏 | 连接对象 RAII 管理，析构 close |
| 网络 | 多线程写同一 fd | 写回动作 RunInLoop 回 IO 线程 |
| 网络 | 两套 HTTP Server | P1 收尾必须删除 health_stub（§6.2 ⑧） |
| DB | 连接池多线程崩溃 | 连接获取/归还加锁，连接不跨线程 |
| DB | 防注入 | 全部预处理语句，禁止拼 SQL |
| DB | init 脚本不生效 | 只在数据卷为空时执行；重置用 `docker compose down -v` |
| Redis | session 过期 | TTL 86400 对齐 04 契约 |
| 配置 | 启动即退（MYSQL_PASSWORD） | fail-fast 特性；export 后重试 |
| AI | 重复处理任务 | CAS UPDATE 抢单 + ai_tasks 状态机幂等（§8.3 ③） |
| AI | LLM 挂 | 降级规则分，标注 degraded |
| MQ | 忘 ACK | 每个分支都 ACK/NACK |
| MQ | requeue 无限循环 | retry_count 上限 3 + 退避 sleep；超限进死信 |
| MQ | 重复消费 | ai_tasks 状态机幂等（队列只负责投递） |
| MQ | 兜底与 MQ 抢任务 | 兜底只捞 status=0 且超 2 分钟的任务（§9.4 ④） |
| 部署 | libmysqlclient.so 缺失 | 运行镜像装 libmysqlclient21 |
| 部署 | nginx 起不来 / 限流 503 | `limit_req_zone` 在 http 顶层 + map 豁免 GET（§10.3 ①） |
| 部署 | compose context 错 | compose 在仓库根，context: . 即根目录 |

# 附录 C：面试问答预演表

## C.1 网络核心（P1）

| 问题 | 回答要点 |
|---|---|
| 讲讲你的 Reactor 模型 | 单线程 EventLoop + epoll 分发 → 业务丢线程池 → 写回回 IO 线程；画图讲 |
| LT 和 ET 区别 | LT 只要有数据就通知；ET 只在状态变化通知，必须循环读到 EAGAIN；本阶段用 LT 起步 |
| 半包/粘包怎么处理 | Buffer 累积 + Content-Length 分帧 + 状态机 |
| 为什么单线程 EventLoop 不阻塞 | 非阻塞 IO + epoll，回调里不做耗时操作，耗时操作丢线程池 |
| 线程池任务怎么回写 | RunInLoop 投回 IO 线程，避免多线程写 fd |

## C.2 业务与数据库（P2）

| 问题 | 回答要点 |
|---|---|
| 连接池怎么实现 | 空闲队列+锁+condition_variable+超时+健康检查（mysql_ping），细节见 §7.3 ① |
| 为什么不用外键 | 应用层保证 + 便于未来分库（03 文档决策） |
| 状态机怎么防非法迁移 | Service 层 canTransition 纯函数 + 单测 |
| 越权怎么防 | DAO 强制带 user_id；token 取 userId 不信 body |
| 密码怎么存 | bcrypt（vendored libbcrypt）加盐慢哈希，禁止明文/单次 MD5 |
| bcrypt 哪来的 | vendored 单文件库（third_party/libbcrypt，CC0）——面试可讲"为什么 vendoring：Conan 无稳定包、单文件零风险" |

## C.3 AI（P3）

| 问题 | 回答要点 |
|---|---|
| 为什么 AI 要异步 | LLM 秒级延迟，同步会阻塞连接池；异步+状态机可重试 |
| 三层评分是什么 | 规则分（可解释）→ embedding（语义）→ LLM（综合+建议）；每层都能解释 |
| 任务怎么防重复处理 | 原子 CAS 抢单（UPDATE ... WHERE status=0）+ ai_tasks 状态机；超时由补偿线程重置 |
| 缓存怎么做的 | Redis ai:match:{job}:{resume} TTL 24h，命中免二次调用 |
| LLM 挂了怎么办 | 降级规则分，标注 degraded，不崩溃 |
| 为什么不用向量库 | 数据量小、单点比较，检索无用武之地（ADR-0005） |
| 为什么 AI 直连 MySQL | ADR-0006：ai_tasks 属 AI 域，MVP 减少往返；正式形态切内部 HTTP；只读写 3 张表（§8.7 矩阵） |

## C.4 消息队列（P4，**重点准备**）

| 问题 | 回答要点 |
|---|---|
| 为什么引入 MQ | AI/采集是慢+易失败负载；解耦、削峰、可重试；业务 CRUD 不走 MQ |
| 消息会不会丢 | exchange/queue/message 全 durable + 手动 ACK |
| 消费失败怎么办 | NACK requeue=true + 退避（1s/2s/4s）+ 重试上限 3 次；超限 NACK requeue=false 进 DLX 死信 |
| 重试上限怎么数 | ai_tasks.retry_count 业务侧累计，与队列重试一致（MAX_RETRY=3） |
| 重复消费怎么处理 | ai_tasks 状态机幂等：已是 success/failed 直接 ACK 跳过（队列只负责投递） |
| MQ 挂了系统会崩吗 | 不会：publish 失败保持 status=0，C++ 补偿线程重新发布 + AI 兜底轮询捞老任务（双通道，§9.4 ④） |
| 兜底轮询会不会和 MQ 抢任务 | 不会：兜底只抢 status=0 且超过 2 分钟的任务，正常流转的任务早被 MQ 消费 |
| 为什么不用 Kafka | 吞吐用不上、运维重（KRaft/ZK），个人项目规模 RabbitMQ 语义更匹配（ADR-0004） |
| 死信队列干嘛的 | 重试超限的消息留痕，供人工核查；业务状态以 ai_tasks 为准 |

## C.5 部署与工程（P5）

| 问题 | 回答要点 |
|---|---|
| docker compose 里服务间怎么编排 | depends_on + healthcheck condition；数据卷持久化 |
| 多阶段构建解决了什么 | 构建态（conan+gcc）与运行态（精简 ubuntu）分离，镜像小 |
| CI 做了什么 | push/PR 触发 build + 单测，质量门禁（单一构建入口 service/） |
| 密钥怎么管理 | 环境变量注入；MYSQL_PASSWORD 未设 fail-fast；.env 不提交 git |
| nginx 限流怎么做 | limit_req_zone 在 http 顶层；map 豁免 GET（轮询接口不限流），写操作 30r/m |

---

## 版本记录

| 版本 | 说明 | 日期 |
|---|---|---|
| V1.0 | 整合 00-07 + ADR，裁决全部冲突，面向 2026 秋招 1-3 个月窗口的可执行手册（保留 RabbitMQ、GitHub 开源交付） | 2026-08-10 |
| V1.1 | 技术一致性修订：nginx 限流位置与 GET 豁免；config 环境变量最终方案；依赖选型定稿（hiredis/amqp-cpp/vendored bcrypt）；统一构建入口与 CMake target；compose 移根目录；P0/P1 HTTP 演进（health_stub）；AI CAS 抢单与超时补偿；MQ 重试/死信/补偿机制定稿；CI 定稿；01/02/00/05/07/project-structure/adr 同步修正 | 2026-08-11 |
