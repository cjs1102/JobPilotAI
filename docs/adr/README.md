# Architecture Decision Records

本目录保存 JobPilot AI 的架构决策记录（ADR），记录每个关键架构决策的背景、权衡与结论，供后续开发与面试答辩引用。

## 索引

| ADR | 标题 | 状态 | 日期 |
|---|---|---|---|
| [0001](0001-tech-stack.md) | 技术栈选型 | Accepted | 2026-08-09 |
| [0002](0002-architecture-style.md) | C++ 模块化单体架构形态 | Accepted shape | 2026-08-09 |
| [0003](0003-database-choice.md) | 数据库选型 | Accepted | 2026-08-09 |
| [0004](0004-message-queue.md) | 消息队列选型 | Future Work | 2026-08-09 |
| [0005](0005-ai-agent-architecture.md) | AI Agent 架构 | Accepted target | 2026-08-09 |
| [0006](0006-ai-data-access.md) | AI 数据访问边界（MVP 直连 MySQL） | Superseded for MVP | 2026-08-10 |
| [0007](0007-open-source-first-positioning.md) | Open-source-first 项目定位 | Accepted | 2026-10-07 |
| [0008](0008-source-adapters-and-job-discovery.md) | Source Adapter 与岗位发现 | Accepted | 2026-10-07 |
| [0009](0009-ai-pipeline-and-provider-strategy.md) | 分层 AI 管线与 Provider 策略 | Accepted | 2026-10-07 |
| [0010](0010-job-freshness-and-lifecycle.md) | Job Freshness 与生命周期 | Accepted | 2026-10-08 |

## ADR 状态说明

- **Proposed**：讨论中
- **Accepted**：已决策，正在实施
- **Deprecated**：不再适用
- **Superseded**：被新 ADR 取代
- **Rejected**：评审过但未采纳

## 新增 ADR 流程

1. 复制 `0000-template.md`（模板见各 ADR 内文格式）
2. 命名 `NNNN-title-with-dashes.md`
3. 填写 Context / Decision / Consequences
4. 更新本索引

## 决策图

```
ADR-0001 技术栈
   └── 支撑
ADR-0002 架构风格 ──> ADR-0004 消息队列（解耦方式）
   └── 决定
ADR-0003 数据库
ADR-0005 AI Agent 架构 ── 依赖 ──> ADR-0004
ADR-0006 AI 数据访问边界（MVP 直连 MySQL）── 依赖 ──> ADR-0005
ADR-0007 Open-source-first
   ├── 约束
ADR-0008 Source Adapter 岗位发现
   └── 依赖
ADR-0009 分层 AI 管线与 Provider 策略
ADR-0010 Job Freshness 与生命周期
```

## MVP 裁决

- ADR-0007 将项目定位为 open-source first / self-hosted。
- ADR-0008 用 Source Adapter 替代通用 Crawler。
- ADR-0009 将 RabbitMQ 推迟到 Future Work，并采用
  `Hard Filter -> Rule -> Embedding -> LLM`。
- ADR-0010 明确单次缺失不等于关闭，Freshness 生命周期在 Source Adapter
  阶段落地。
