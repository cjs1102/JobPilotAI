# JobPilotAI GitHub 资源调研

> 调研日期：2026-10-04
>
> 调研范围：Codex CLI 与 AI Agent 工作流、C++ 工程规范、Linux 网络开发、CMake 与 Conan、Debug 与测试体系。
>
> 本文只沉淀思想、工程规范、文档结构和配置方式，不复制第三方项目代码。本文不改动 JobPilotAI 业务代码。

## 调研限制

- 已尝试读取 OpenAI 官方文档站点 `https://developers.openai.com/codex/`，当前环境连接超时。
- Codex 部分以官方 `openai/codex` GitHub 仓库内公开文档为依据，包括 `docs/agents_md.md`、`docs/config.md`、`docs/sandbox.md`、`docs/skills.md`、`docs/execpolicy.md`。
- GitHub 仓库结构和关键文件已通过 GitHub 连接器核验。仓库版本、目录和许可证在正式引入前仍需复核当前状态。

## 结论摘要

### 立即采用

| 资源 | 采用方式 | 目标 |
|---|---|---|
| `agentsmd/agents.md` + `CLIUtils/CLI11` | 建立根 `AGENTS.md`，按目录增加局部规则 | 让 Agent 稳定理解构建、测试、边界和验证命令 |
| `openai/codex` | 采用官方 CLI 的权限、沙箱、配置和 skill 思路 | 建立可审计、可复现、权限最小化的 Agent 工作流 |
| `github/spec-kit` + HumanLayer ACE | 复杂任务采用 research -> plan -> implement -> verify | 降低 Reactor、并发、生命周期问题的返工 |
| `isocpp/CppCoreGuidelines` + `google/eng-practices` | 转写为项目评审清单 | 强化资源所有权、并发和 Review 纪律 |
| `cpp-best-practices/cmake_template` + `conan-io/examples2` | 引入 Presets、Sanitizer、Conan 2 组织方式 | 建立 Debug、Release、ASan、TSan 构建矩阵 |
| `cpp-linter/cpp-linter-action` + `hendrikmuhs/ccache-action` | PR 格式与静态检查、CI 编译缓存 | 建立第一批自动化质量门禁 |

### 选择性采用

| 资源 | 采用方式 | 目标 |
|---|---|---|
| `chenshuo/muduo` | 只读设计参考 | 校验 `EventLoop`、`Poller`、`Channel`、连接生命周期 |
| `sogou/workflow` + `taskflow/taskflow` | 只读架构参考 | 为后续任务调度系统准备任务模型和 DAG 思路 |
| `apache/brpc` + `grpc/grpc` | 只读协议、并发和可观测性参考 | 为后续 RPC 阶段冻结技术选型前做评估 |
| `google/googletest` + `google/benchmark` | 现有测试体系升级 | 同时覆盖正确性、并发行为和性能回归 |

### 暂不直接引入

| 资源 | 原因 |
|---|---|
| `libevent/libevent` | JobPilotAI 当前是 Linux-only 自研 Reactor，直接替换会掩盖学习目标 |
| `sogou/workflow`、`taskflow/taskflow` | 当前阶段尚未出现任务调度系统的真实约束，过早引入会造成架构倒推 |
| `google/sanitizers` | 仓库已归档，应使用 LLVM 与当前编译器文档作为最新依据 |
| `google/oss-fuzz` | 当前优先做单元测试、Sanitizer 和压力测试，协议解析稳定后再接入持续 Fuzz |
| `Valgrind` | 适合本地诊断，不建议作为所有 CI 的默认门禁 |

## 评价标准

每个资源按以下维度判断：

1. 与当前 Reactor 网络库阶段的相关性。
2. 是否有助于后续 EventLoop、Poller、TCP Server、RPC、任务调度演进。
3. 是否直接改善 C++17、CMake、Conan、Docker 工程体系。
4. 是否强调测试、Sanitizer、可观测性和可复现构建。
5. 是否适合只借鉴结构、规范和工作流，而不是复制整套代码。
6. 引入成本和维护成本是否与单人 3 至 6 个月交付目标匹配。

## 资源总览

| 编号 | 资源 | 方向 | 决策 |
|---|---|---|---|
| A1 | OpenAI Codex CLI | AI Agent 工作流 | 立即采用 |
| A2 | AGENTS.md | AI Agent 配置 | 立即采用 |
| A3 | GitHub Spec Kit | 规格驱动开发 | 选择性采用 |
| A4 | Advanced Context Engineering | 长任务上下文治理 | 立即采用方法 |
| A5 | CLI11 AGENTS.md | C++ Agent 配置范例 | 立即借鉴 |
| B1 | C++ Core Guidelines | C++17 规范 | 立即采用 |
| B2 | Google C++ Style Guide | C++ 样式 | 选择性采用 |
| B3 | Google Engineering Practices | Code Review | 立即采用 |
| B4 | Include What You Use | 头文件依赖治理 | 后续启用 |
| C1 | Muduo | Reactor 网络库 | 核心参考 |
| C2 | libevent | 跨平台事件循环 | 仅参考 |
| C3 | Sogou C++ Workflow | 异步任务与 DAG | 后续参考 |
| C4 | Taskflow | C++ 任务调度 | 后续参考 |
| C5 | Apache bRPC | 工业级 RPC | 后续评估 |
| C6 | gRPC | 跨语言 RPC | 后续评估 |
| D1 | cpp-best-practices CMake Template | 现代 CMake 选项 | 立即借鉴 |
| D2 | cmake-init | CMake 项目生成规范 | 选择性采用 |
| D3 | ModernCppStarter | CMake 工程模板 | 选择性采用 |
| D4 | Conan 2 Examples | Conan 依赖管理 | 立即采用 |
| E1 | GoogleTest | 单元测试 | 继续使用并升级组织方式 |
| E2 | Google Benchmark | 微基准测试 | 后续引入 |
| E3 | Google Sanitizers | 动态分析 | 采用方法，不采用归档仓库 |
| E4 | OSS-Fuzz | 持续模糊测试 | 后续引入 |
| E5 | Valgrind | 动态诊断 | 本地工具 |
| E6 | cpp-linter-action | PR 静态检查 | 立即引入 |
| E7 | ccache-action | CI 编译缓存 | CI 建立后引入 |

---

## 1. Codex CLI 与 AI Agent 工作流

### A1. OpenAI Codex CLI

**项目名称：** OpenAI Codex CLI

**GitHub 地址：** https://github.com/openai/codex

**用途：** 开源 Codex CLI 实现。仓库包含 `agents_md.md`、`config.md`、`sandbox.md`、`skills.md`、`execpolicy.md` 等文档入口，可用于设计本地 AI 编码代理的配置、权限、命令执行和技能体系。

**适合 JobPilotAI 的原因：** JobPilotAI 是长期演进的 C++ 后端项目，需要让 Agent 明确构建、测试、系统调用、网络调试和 Docker 操作边界。Codex 的沙箱、审批、命令策略和多层指令机制，适合转成项目级安全规则。

**是否推荐引入：** 推荐立即采用其工作流和配置思想。不要把 Codex 内部实现复制进项目。

**如何整合：**

- 在根目录建立 `AGENTS.md`，只写项目事实和强制边界，不写泛泛的提示词。
- 记录构建、单测、Sanitizer、Docker 和网络测试命令。
- 明确禁止 Agent 擅自修改 `docs/03-database-design.md`、`docs/04-api-design.md` 等契约文档。
- 对 `epoll`、线程、文件系统、Docker、网络权限采用最小权限原则。
- 后续将高价值流程做成项目 skill，例如 `reactor-review`、`asan-tsan-run`、`conan-build`。

### A2. AGENTS.md

**项目名称：** AGENTS.md

**GitHub 地址：** https://github.com/agentsmd/agents.md

**用途：** 面向编码 Agent 的开放说明文件格式。它将项目结构、开发命令、测试要求、PR 约定集中到 Agent 可预测读取的位置。

**适合 JobPilotAI 的原因：** 当前项目已有 `docs/06-coding-standard.md` 和 ADR，但缺少 Agent 最容易执行的“命令级说明”。AGENTS.md 可把文档规则压缩成可执行约束。

**是否推荐引入：** 推荐立即引入。根目录规则保持短小，目录级规则承载模块细节。

**如何整合：**

- 根 `AGENTS.md` 写全局规则、目录职责、验证命令和禁止事项。
- `backend/AGENTS.md` 写网络、HTTP、数据库、缓存和测试细节。
- `docs/AGENTS.md` 写文档先行、ADR 变更和链接校验要求。
- 每个局部文件只写该目录特有约束，避免与根规则重复。
- 将“不复制第三方实现”“不绕过测试”“不私改契约”写成硬规则。

### A3. GitHub Spec Kit

**项目名称：** GitHub Spec Kit

**GitHub 地址：** https://github.com/github/spec-kit

**用途：** 规格驱动开发工具包，提供 spec、plan、tasks、constitution 等模板，强调需求、设计、任务和测试之间的可追踪关系。

**适合 JobPilotAI 的原因：** Reactor 网络库涉及事件循环、连接生命周期、读写缓冲、线程切换和错误恢复，单个改动容易跨越多个文件。Spec Kit 的模板能迫使 Agent 在编码前明确行为、边界和验收标准。

**是否推荐引入：** 推荐选择性采用模板和门禁，不建议直接生成全套命令目录并与现有 docs 结构并行。

**如何整合：**

- 为 Reactor 类复杂改动新增 `docs/ai/changes/<change-id>/spec.md`、`plan.md`、`tasks.md`、`verification.md`。
- 将现有 ADR、架构文档和编码规范视为 constitution。
- 对 TCP Server、RPC、任务调度这种跨模块能力启用完整流程。
- 对小修小补不强制走完整 Spec Kit，避免流程成本超过收益。
- 模板中的 `[NEEDS CLARIFICATION]` 机制保留，禁止 Agent 猜测契约。

### A4. Advanced Context Engineering for Coding Agents

**项目名称：** Advanced Context Engineering for Coding Agents

**GitHub 地址：** https://github.com/humanlayer/advanced-context-engineering-for-coding-agents

**用途：** 总结复杂代码库中的 AI 编码方法，核心是 frequent intentional compaction、research、plan、implement，以及把人工注意力放在研究结论和计划上。

**适合 JobPilotAI 的原因：** `epoll` 边沿触发、写缓冲、跨线程回调、连接关闭时序等问题难以靠一次性大提示解决。该资源适合转成网络层问题排查流程。

**是否推荐引入：** 推荐立即引入方法，不需要引入其商业工具或 Claude 专用配置。

**如何整合：**

- 网络问题先输出研究记录，再输出计划，最后才允许改代码。
- 每个复杂任务维护一份压缩后的 `research.md`，记录已读文件、调用链、假设和证据。
- `plan.md` 必须列出测试、Sanitizer、压力验证和回滚点。
- 实现阶段按小 step 推进，每步更新验证结果，避免上下文漂移。
- 人工重点审查研究结论和计划，而不是等到数千行代码生成后再补救。

### A5. CLI11 Agent Guide

**项目名称：** CLI11

**GitHub 地址：** https://github.com/CLIUtils/CLI11

**用途：** 高质量 C++ 项目的真实 `AGENTS.md` 范例。它覆盖快速构建、单测运行、CMake Presets、关键选项、lint、IWYU、代码结构和版本来源。

**适合 JobPilotAI 的原因：** 当前项目已有 CMake、Conan、GoogleTest 和网络模块，但 Agent 缺少明确的快速迭代命令和模块地图。CLI11 的组织方式可直接映射到 JobPilotAI。

**是否推荐引入：** 推荐立即借鉴其文档结构，不复制构建脚本或代码。

**如何整合：**

- 在 `AGENTS.md` 中提供 `dev`、`test`、`asan`、`tsan`、`release` 等命令入口。
- 明确“单测怎么跑”“失败日志在哪”“哪些 target 属于核心”。
- 列出网络层不可破坏的不变量，例如生命周期、线程归属和错误传播。
- 将长期固定规则留在 AGENTS.md，将流程细节链接到 `docs/ai/`。

---

## 2. C++ 工程规范

### B1. C++ Core Guidelines

**项目名称：** C++ Core Guidelines

**GitHub 地址：** https://github.com/isocpp/CppCoreGuidelines

**用途：** 面向现代 C++ 的资源管理、所有权、生命周期、接口、并发和类型安全指南。

**适合 JobPilotAI 的原因：** 网络库的核心风险集中在对象生命周期、回调捕获、跨线程所有权、文件描述符管理和异常边界，这些正是 C++ Core Guidelines 的核心关注点。

**是否推荐引入：** 推荐立即作为评审视角和 clang-tidy 规则来源。

**如何整合：**

- 将 RAII、Rule of Zero、所有权表达、禁止悬空引用写入网络层检查清单。
- 对 `Channel`、`TcpConnection`、`EventLoop` 的回调捕获做生命周期专项 Review。
- 将 `cppcoreguidelines-*`、`bugprone-*`、`performance-*` 作为 `.clang-tidy` 首批规则。
- 不机械启用全部规则，先以 warnings 模式运行，再逐步升级为 errors。

### B2. Google C++ Style Guide

**项目名称：** Google Style Guides

**GitHub 地址：** https://github.com/google/styleguide

**用途：** C++ 命名、格式、注释、头文件、作用域、类和函数设计规范。

**适合 JobPilotAI 的原因：** 项目已有自己的命名规范，但缺少统一格式工具、头文件顺序和公开接口注释规则。Google 指南可作为差异对照。

**是否推荐引入：** 推荐选择性引入。不要整体替换项目现有命名风格。

**如何整合：**

- 保留 JobPilotAI 现有 camelCase 方法和 trailing underscore 成员规则。
- 只借鉴 include 顺序、显式构造、避免隐式转换、接口注释和文件组织思路。
- 以项目自己的 `.clang-format` 作为唯一格式裁判，避免风格文档和工具冲突。
- 在 Review 中优先检查行为、所有权和边界，不把格式争论留到人工评审。

### B3. Google Engineering Practices

**项目名称：** Google Engineering Practices

**GitHub 地址：** https://github.com/google/eng-practices

**用途：** Code Review 的审查者指南和变更作者指南，强调小变更、可理解性、设计、测试和快速反馈。

**适合 JobPilotAI 的原因：** AI 辅助开发会显著增加变更规模。明确 Review 标准能防止网络层改动变成难以审计的大补丁。

**是否推荐引入：** 推荐立即引入，并压缩成项目 PR 模板和 Review Checklist。

**如何整合：**

- PR 描述写清动机、行为变化、测试证据、风险和回滚方式。
- 网络层 PR 必须说明线程模型、对象生命周期、错误路径和资源释放。
- 每次提交保持单一逻辑变更，禁止把格式化、重构和功能混在一个 PR。
- Review 发现关键缺失时以 correctness、ownership、observability 为优先级。

### B4. Include What You Use

**项目名称：** Include What You Use

**GitHub 地址：** https://github.com/include-what-you-use/include-what-you-use

**用途：** 分析 C++ 源文件和头文件是否直接包含自己使用的符号，减少传递依赖和头文件耦合。

**适合 JobPilotAI 的原因：** `EventLoop`、`Poller`、`Channel`、`TcpConnection` 之间容易出现头文件传递依赖，最终让模块边界失真。

**是否推荐引入：** 推荐后续启用。当前先保证编译、测试和接口稳定，再进入 include 治理。

**如何整合：**

- 在 CMake Presets 中增加可选 `iwyu` preset。
- 先只在 `jobpilot_core` 上运行，输出报告但不阻塞 CI。
- 网络模块稳定后，把新增未声明依赖设为 Review 必查项。
- 对平台差异导致的误报保留 pragma，不机械删除标准库 include。

---

## 3. Linux 网络开发

### C1. Muduo

**项目名称：** Muduo

**GitHub 地址：** https://github.com/chenshuo/muduo

**用途：** 多线程 C++ Reactor 网络库，包含 one loop per thread、EventLoop、Poller、Channel、TcpConnection、Buffer、定时器和线程池。

**适合 JobPilotAI 的原因：** 当前项目已按 `EventLoop`、`Poller`、`EpollPoller`、`Channel`、`TcpConnection` 分层，Muduo 是最直接的设计校验对象。

**是否推荐引入：** 推荐作为核心只读参考，不建议直接替换现有网络栈。

**如何整合：**

- 对照 Muduo 审查 `RunInLoop`、`QueueInLoop`、wakeup fd 和 pending functors 的线程语义。
- 对照 `Channel` 生命周期审查 epoll 事件注册、删除和回调期间对象销毁。
- 参考其 Buffer 读写索引、扩容策略和高低水位回压设计。
- 借鉴 TimerQueue、EventLoopThreadPool 和测试用例思路，为后续任务调度打基础。
- 不引入 Boost 依赖和旧式构建方式，只抽取模式与不变量。

### C2. libevent

**项目名称：** libevent

**GitHub 地址：** https://github.com/libevent/libevent

**用途：** 跨平台事件通知库，抽象 epoll、poll、select、kqueue、IOCP 等后端，并提供 bufferevent、evbuffer、timer、signal、DNS 和 HTTP。

**适合 JobPilotAI 的原因：** 可用来对照当前 epoll 封装的边界：Poller 应该只负责事件通知，缓冲、超时、信号和协议处理应保持分层。

**是否推荐引入：** 暂不直接引入。作为抽象设计和安全修复案例参考。

**如何整合：**

- 参考 event base、event loop 和 backend 的职责边界，检查 `Poller` 是否过重。
- 参考 evbuffer 的读写游标、水位和内存回收策略。
- 如未来引入 libevent，必须至少使用包含 2026 年安全修复的稳定版本，并做 ASan/TSan 验证。
- 当前仍优先自研 Linux epoll Reactor，以满足项目展示和学习目标。

### C3. Sogou C++ Workflow

**项目名称：** Sogou C++ Workflow

**GitHub 地址：** https://github.com/sogou/workflow

**用途：** 异步编程引擎，统一网络、计算、文件 IO、定时器、计数器、Series、Parallel 和 DAG 任务。

**适合 JobPilotAI 的原因：** 后续任务调度系统需要统一表达网络任务、CPU 任务、依赖关系和生命周期。Workflow 的任务抽象能提供成熟参照。

**是否推荐引入：** 推荐后续阶段做只读评估。当前不要用它替换 EventLoop。

**如何整合：**

- 借鉴 `Protocol + Algorithm + Workflow` 的边界划分。
- 研究 Series、Parallel、DAG 对任务依赖和错误传播的表达方式。
- 对 AI 任务、爬虫入库、批处理和定时任务分别梳理任务类型。
- 当项目出现明确的任务图需求后，再做独立 ADR，比较自研调度器、Taskflow 与 Workflow。

### C4. Taskflow

**项目名称：** Taskflow

**GitHub 地址：** https://github.com/taskflow/taskflow

**用途：** 现代 C++ 任务图编程和调度库，支持任务依赖、条件任务、工作窃取和性能分析工具。

**适合 JobPilotAI 的原因：** 后续 AI 任务调度和爬虫 Pipeline 可能需要 DAG，Taskflow 可作为调度语义、可视化、profiling 和测试组织参考。

**是否推荐引入：** 推荐后续评估。当前不直接引入依赖。

**如何整合：**

- 研究任务图、条件分支、依赖完成和异常处理模型。
- 评估其任务模型是否适合 AI 异步任务和岗位采集流水线。
- 如果最终只需要线性异步队列，不引入通用任务图库。
- 若引入，必须补充超时、取消、优先级、持久化和重启恢复设计，不能只依赖内存任务图。

### C5. Apache bRPC

**项目名称：** Apache bRPC

**GitHub 地址：** https://github.com/apache/brpc

**用途：** 工业级 C++ RPC 框架，提供多协议服务、客户端、负载均衡、bthread、bvar、内建调试服务、CPU/Heap/Contention profiler 和并发控制。

**适合 JobPilotAI 的原因：** 后续 RPC 阶段需要协议、服务注册、超时、重试、负载均衡和可观测性。bRPC 可作为 C++ 原生高性能实现的重要参照。

**是否推荐引入：** 推荐后续选型阶段重点评估。当前不引入大依赖。

**如何整合：**

- 学习其同步、异步、半同步调用模型和错误传播方式。
- 借鉴 `/status`、`/vars`、rpcz、profiler 等内建可观测性设计。
- 研究 AutoConcurrencyLimiter、超时、熔断和连接池如何缓解雪崩。
- 若要落地 RPC，先写 ADR，明确 gRPC、bRPC、自研协议和 JSON/HTTP 的取舍。

### C6. gRPC

**项目名称：** gRPC

**GitHub 地址：** https://github.com/grpc/grpc

**用途：** 基于 HTTP/2 和 IDL 的跨语言 RPC 框架，支持同步、异步、流式调用、metadata、flow control 和多语言代码生成。

**适合 JobPilotAI 的原因：** 项目后续包含独立 Python AI Service。gRPC 能提供清晰的跨语言接口和流式能力，但当前 C++ 单体内部模块不应过早 RPC 化。

**是否推荐引入：** 推荐后续评估，当前不引入。

**如何整合：**

- 当 C++ 与 Python 服务需要正式接口时，优先用 proto 定义契约。
- 先写 contract tests，再生成客户端和服务端 stub。
- 区分 unary、server streaming 和 bidirectional streaming 的使用边界。
- 评估构建体积、Conan 集成复杂度、超时、重试、幂等和 tracing。

---

## 4. CMake 工程化与 Conan

### D1. cpp-best-practices CMake Template

**项目名称：** cmake_template

**GitHub 地址：** https://github.com/cpp-best-practices/cmake_template

**用途：** 现代 CMake 项目模板，包含 CMakePresets、Dependencies、ProjectOptions、CompilerWarnings、Sanitizers、StaticAnalyzers、Hardening、Coverage、Fuzzing、Ccache 和 IPO。

**适合 JobPilotAI 的原因：** 当前根 CMake 和 `backend/CMakeLists.txt` 已具备目标化基础，但缺少统一 Presets、Sanitizer、clang-tidy、coverage 和 release/debug 构建矩阵。

**是否推荐引入：** 推荐立即借鉴模块化方式，不整体覆盖现有 CMake。

**如何整合：**

- 将 `ProjectOptions.cmake` 的选项思想转成 `JP_ENABLE_ASAN`、`JP_ENABLE_TSAN`、`JP_ENABLE_UBSAN`、`JP_ENABLE_CLANG_TIDY`。
- 建立 `CMakePresets.json`，至少包含 `dev`、`release`、`asan`、`tsan`、`ci`。
- 把 warnings 和 sanitizer 作为 target interface library 传播，避免全局污染。
- 将测试注册改成统一函数，减少 `backend/CMakeLists.txt` 中重复的 `add_executable`、`target_link_libraries` 和 `add_test`。

### D2. cmake-init

**项目名称：** cmake-init

**GitHub 地址：** https://github.com/friendlyanon/cmake-init

**用途：** 现代 CMake 项目生成器，强调 consumer/developer target 分离、FetchContent ready、安装导出、CMake Presets、clang-tidy、cppcheck、coverage、sanitizer、Valgrind 和 Conan/vcpkg 支持。

**适合 JobPilotAI 的原因：** 可直接对照其目录结构、Presets 和开发工具开关，修正当前 CMake 的重复和全局状态问题。

**是否推荐引入：** 推荐选择性参考。当前已有项目结构，不建议直接由生成器覆盖。

**如何整合：**

- 对照它检查 `install`、`export`、`find_package` 和 target 命名。
- 借鉴 developer/consumer target 分离，区分服务可执行文件与可复用核心库。
- 参考其 Conan 集成模板，但保持 JobPilotAI 当前 Conan 2 工作流。
- 许可证需复核，生成器模板可用于参考，不要把 GPL 实现复制进仓库。

### D3. ModernCppStarter

**项目名称：** ModernCppStarter

**GitHub 地址：** https://github.com/TheLartians/ModernCppStarter

**用途：** 现代 C++ 项目模板，包含 CMake 目标化、测试、CI、coverage、clang-format、cmake-format、Sanitizer、clang-tidy、IWYU、cppcheck、Ccache 和安装包。

**适合 JobPilotAI 的原因：** 其工具开关和 CI 组合可以直接映射到 JobPilotAI 的构建质量矩阵。

**是否推荐引入：** 推荐选择性参考。不要采用 CPM 替换项目现有 Conan 依赖管理。

**如何整合：**

- 借鉴 `USE_SANITIZER`、`USE_STATIC_ANALYZER`、`USE_CCACHE` 的开发者选项设计。
- 将 Library、Service、Tests、Benchmarks、Fuzzers 分离为清晰 target。
- 不采用其 GLOB 源文件方式，继续显式列出源文件。
- 参考 CI、覆盖率上传和格式检查流程，保留 Conan 作为依赖主通道。

### D4. Conan 2 Examples

**项目名称：** Conan 2 Examples

**GitHub 地址：** https://github.com/conan-io/examples2

**用途：** Conan 2 官方示例，覆盖消费包、创建包、开发包、版本管理、交叉编译、配置文件和依赖图。

**适合 JobPilotAI 的原因：** 项目已使用 `service/conanfile.txt` 和 `CMakeDeps`、`CMakeToolchain`，但缺少 profile、lockfile、可选依赖、测试依赖和可复现构建约束。

**是否推荐引入：** 推荐立即采用其组织方式和版本锁定思想。

**如何整合：**

- 增加明确的 Conan profile，例如 Linux Debug、Release、ASan、TSan。
- 对关键依赖生成 lockfile，保证 CI 和 Docker 构建可复现。
- 将测试依赖与运行依赖分组，避免生产镜像带入测试工具。
- 为升级 spdlog、yaml-cpp、nlohmann_json、gtest、hiredis 建立统一验证流程。
- 所有依赖变更必须同时跑 CMake configure、build、ctest 和 Docker smoke test。

---

## 5. Debug、测试与 CI/CD

### E1. GoogleTest

**项目名称：** GoogleTest

**GitHub 地址：** https://github.com/google/googletest

**用途：** C++ 单元测试和 mock 框架，提供 fixture、参数化测试、death test、mock、类型化测试和 CMake target。

**适合 JobPilotAI 的原因：** 项目已使用 GoogleTest，并已有网络、HTTP、Buffer、连接池等测试。当前重点是提升测试组织和网络并发场景覆盖。

**是否推荐引入：** 已引入，推荐继续使用并升级组织方式。最新稳定版要求 C++17，与 JobPilotAI 匹配。

**如何整合：**

- 使用 `GTest::gtest_main`，减少每个测试重复写 main。
- 封装 `jobpilot_add_test(name sources...)`，统一链接核心库和测试库。
- 增加参数化测试覆盖 Buffer 边界、HTTP 解析异常和 epoll 事件组合。
- 网络测试覆盖半包、粘包、短写、EAGAIN、连接关闭、回调中销毁。
- 并发测试与 TSan 组合运行，不只看单线程断言。

### E2. Google Benchmark

**项目名称：** Google Benchmark

**GitHub 地址：** https://github.com/google/benchmark

**用途：** C++ 微基准测试框架，覆盖吞吐、延迟、多线程、自定义计数器和 JSON 输出。

**适合 JobPilotAI 的原因：** Reactor 网络库需要客观评价 Buffer 扩容、parser、路由、事件分发和队列实现，避免凭感觉优化。

**是否推荐引入：** 推荐后续引入。先把 correctness 和 sanitizer 稳定，再建立性能基线。

**如何整合：**

- 为 Buffer append/retrieve、HTTP 请求解析、路由匹配和 pending functor 执行建立 benchmark。
- 将 benchmark 作为独立 target，不混入普通测试。
- 保存基准数据并设置回归阈值，避免 CI 抖动导致误报。
- 性能优化必须先有 benchmark 证据，再改实现。

### E3. Google Sanitizers

**项目名称：** Sanitizers

**GitHub 地址：** https://github.com/google/sanitizers

**用途：** AddressSanitizer、ThreadSanitizer、MemorySanitizer、LeakSanitizer、UBSan 等动态检测工具的历史入口。

**适合 JobPilotAI 的原因：** 网络库常见问题包括 use-after-free、越界、数据竞争、未初始化读写和生命周期错误，Sanitizer 应成为必跑质量门禁。

**是否推荐引入：** 推荐采用 Sanitizer 方法，不建议把该归档仓库作为最新依据。

**如何整合：**

- 最新实现和文档以 LLVM 及当前编译器为准。
- Debug Preset 默认启用 ASan + UBSan。
- 并发测试独立启用 TSan，不与 ASan 混用。
- LeakSanitizer 纳入 Linux CI。
- 所有网络测试和压力测试至少跑一次 ASan/UBSan，线程共享路径跑 TSan。

### E4. OSS-Fuzz

**项目名称：** OSS-Fuzz

**GitHub 地址：** https://github.com/google/oss-fuzz

**用途：** 面向开源项目的持续模糊测试平台，支持 libFuzzer、AFL++、Honggfuzz 和 Sanitizer 组合。

**适合 JobPilotAI 的原因：** HTTP 请求解析、Buffer 编解码、协议帧解析和配置解析都属于不可信输入边界，长期应进入 fuzz 测试。

**是否推荐引入：** 推荐后续引入。当前阶段先完成单测、Sanitizer 和压力测试。

**如何整合：**

- 优先为 HTTP request parser 和 Buffer 建立本地 libFuzzer target。
- 使用语料库保存合法请求、畸形请求和极长 header。
- Fuzz 发现的问题必须补回归测试。
- 当前不接入 OSS-Fuzz 托管服务，先跑本地和 CI 短时 fuzz。

### E5. Valgrind

**项目名称：** Valgrind

**GitHub 地址：** 无官方 GitHub 主仓。官方上游为 `https://sourceware.org/git/valgrind.git`，常用镜像只能作为方便访问，不应作为权威来源。

**用途：** Memcheck、Helgrind、DRD、Callgrind、Massif 等动态分析工具，覆盖内存、线程、缓存和堆分析。

**适合 JobPilotAI 的原因：** 对网络库中的未初始化内存、文件描述符泄漏、线程竞争和内存增长，Valgrind 可作为 Sanitizer 之外的交叉验证。

**是否推荐引入：** 推荐作为本地诊断工具，不建议所有 CI 默认启用。

**如何整合：**

- 保留 `ctest -T memcheck` 或显式 Valgrind 命令作为 manual CI job。
- Memcheck 用于排查 ASan 难以定位的未初始化读取。
- Helgrind/DRD 用于 TSan 之外的竞态交叉验证。
- Callgrind/Massif 只在性能或内存问题专项分析时使用。

### E6. cpp-linter-action

**项目名称：** cpp-linter-action

**GitHub 地址：** https://github.com/cpp-linter/cpp-linter-action

**用途：** 对 PR 变更的 C/C++ 文件运行 clang-format 和 clang-tidy，并把结果输出为 annotations、comments、step summary 或 review。

**适合 JobPilotAI 的原因：** 可以把格式和静态检查自动反馈到 PR，减少人工 Review 噪声，并迫使 `.clang-format`、`.clang-tidy` 成为真实工程约束。

**是否推荐引入：** 推荐立即引入，先以 warnings 模式运行，后逐步升级。

**如何整合：**

- 增加 `.clang-format` 和 `.clang-tidy`。
- 在拉取请求工作流中开启 annotations 和 step summary。
- 第一阶段只检查变更文件，避免历史代码一次性报错。
- 第二阶段对 `jobpilot_core` 全量执行 clang-tidy。
- 不默认开启自动修复提交，先让开发者理解规则。

### E7. ccache-action

**项目名称：** ccache-action

**GitHub 地址：** https://github.com/hendrikmuhs/ccache-action

**用途：** GitHub Actions 中缓存 C/C++ 编译结果，支持 ccache/sccache 和多平台缓存键。

**适合 JobPilotAI 的原因：** C++ 模板、Sanitizer 和网络库编译会快速增加 CI 时间，缓存能显著降低重复构建成本。

**是否推荐引入：** 推荐在 CI 建立后立即引入。

**如何整合：**

- 在 `actions/checkout` 之后启用缓存。
- 按 Debug、Release、ASan、TSan 分开 cache key。
- CMake 中设置 `CMAKE_CXX_COMPILER_LAUNCHER=ccache`。
- 输出 ccache stats，观察命中率和无效缓存问题。
- Docker 构建中同步配置 ccache 挂载，避免每次全量编译。

---

## 建议落地顺序

### 第 1 阶段：让 Agent 可控

1. 建立 `AGENTS.md`，写清项目结构、命令、边界和验证要求。
2. 建立 `docs/ai/` 下的任务模板，统一 research、plan、verify 记录。
3. 为 Reactor 改动定义不可破坏的不变量。

### 第 2 阶段：让构建可复现

1. 建立 `CMakePresets.json`。
2. 建立 Conan profiles 和 lockfile 策略。
3. 将 warnings、ASan、UBSan、TSan、clang-tidy 做成可选 target。

### 第 3 阶段：让质量自动化

1. 统一 GoogleTest target 注册方式。
2. 增加 cpp-linter-action 和 ccache-action。
3. 在 CI 中运行 build、ctest、ASan/UBSan。
4. TSan 独立运行，避免与 ASan 混用。

### 第 4 阶段：让网络库可解释

1. 对照 Muduo 审查 `EventLoop`、`Poller`、`Channel`、`TcpConnection`。
2. 补半包、粘包、短写、EAGAIN、关闭时序和回调销毁测试。
3. 引入 Benchmark，建立 Buffer、解析器和事件分发的性能基线。

### 第 5 阶段：为 RPC 和任务调度做准备

1. 先写 ADR，再选 gRPC、bRPC、自研协议或轻量 JSON/HTTP。
2. 对任务调度只借鉴 Workflow 和 Taskflow，不在需求模糊时引入依赖。
3. 明确取消、超时、重试、幂等、持久化和重启恢复，再决定调度器边界。

## 不推荐事项

- 不直接复制任意第三方网络库实现。
- 不用大型框架替代当前 Reactor 开发阶段。
- 不把同名规则文件复制进多个目录，导致局部规则冲突。
- 不在缺少测试和 benchmark 的情况下做网络层微优化。
- 不为展示技术栈引入与当前阶段无关的依赖。
- 不让 Agent 修改 ADR、API 契约或数据库契约而不先更新文档并评审。
