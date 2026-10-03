# ADR-0001: 技术栈选型

## Status

Accepted

## Context

JobPilot AI 是用于秋招面试展示的 C++ 后端个人项目。核心诉求：

- 展示 C++ 后端硬实力：网络编程、并发、数据库设计
- 3-6 个月内由单人完成，可运行、可部署、可演示
- 面试时能被问深、能讲清楚

需要为后端、AI 服务、前端分别选择技术栈。

## Decision Drivers

- **后端**：必须体现 C++ 网络编程能力，且是面试官认知度最高的技术
- **AI 服务**：LLM 生态（LangChain、Embedding、Agent）几乎全在 Python 侧
- **前端**：演示用，学习成本低、开发快
- **单人可维护性**：组件数量与复杂度必须受控

## Considered Options

### 后端

| 选项 | 优点 | 缺点 |
|---|---|---|
| **C++17 自研 HTTP Server**（epoll + Reactor） | 展示核心网络能力，面试价值最高；无框架依赖 | 需要自己处理 HTTP 解析边界、线程模型；开发量大 |
| 成熟 C++ 框架（drogon / Tars / tRPC-cpp） | 开发快、健壮 | 自研网络层的展示价值被抹掉；框架学习成本高；面试难以深入 |
| Go / Java 后端 | 生态好、开发快 | 与目标岗位（C++ 后端）错配，无法展示 C++ 能力 |

### AI 服务

- **Python + FastAPI**：选型无争议，LLM/Embedding 生态事实标准
- LLM 采用 DeepSeek/Qwen 开放 API（OpenAI 兼容协议），本地模型（Ollama）仅作扩展项

### 前端

- **Vue3 + Vite + Element Plus + ECharts + Axios**：个人项目最常见组合，文档多、上手快

## Decision

- 后端：**C++17 + CMake + Conan + Linux + epoll/Reactor 自研 HTTP Server**，辅助 spdlog（日志）、GoogleTest（测试）
- AI 服务：**Python 3.10 + FastAPI**，DeepSeek/Qwen API + sentence-transformers（Embedding）
- 前端：**Vue3 + Vite + Element Plus + ECharts + Axios**
- 工程：**Docker + Docker Compose + GitHub Actions**，Conan 管理 C++ 依赖

## Rationale

1. **自研网络层是面试的“压舱石”**：epoll/Reactor/Buffer/连接池是 C++ 后端岗位面试的高频深挖点，自己写一遍才能在面试中讲透
2. 放弃成熟 C++ 框架换来的是“从 socket 到业务”的完整链路，这正是简历里最有含金量的部分
3. Python 承担 AI 服务是行业惯例，职责边界清晰：C++ 管业务与性能，Python 管智能
4. 组件均为就业市场主流，答辩时技术选型本身可聊

## Consequences

### Positive

- 展示链路完整：socket → HTTP → 连接池 → 业务 → 缓存 → 异步任务 → AI
- 技术栈全部可聊可考，无“黑盒依赖”
- 就业方向匹配度最高

### Negative

- 自研 HTTP Server 需处理大量边界情况（超时、半包粘包、内存安全），是主要风险点
- Conan 环境在 Windows/服务器间存在配置成本

### Risks

- 网络层开发超出预期 → 缓解：网络层限定在“可用的 HTTP/1.1 + JSON”，不做 HTTPS、不做 HTTP/2
- 缓解方案记录于 [ADR-0002](0002-architecture-style.md) 的架构约束中

## Related Decisions

- [ADR-0003](0003-database-choice.md)：数据库选型
- [ADR-0005](0005-ai-agent-architecture.md)：AI 服务内部技术
