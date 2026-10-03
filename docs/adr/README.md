# Architecture Decision Records

本目录保存 JobPilot AI 的架构决策记录（ADR），记录每个关键架构决策的背景、权衡与结论，供后续开发与面试答辩引用。

## 索引

| ADR | 标题 | 状态 | 日期 |
|---|---|---|---|
| [0001](0001-tech-stack.md) | 技术栈选型 | Accepted | 2026-08-09 |
| [0002](0002-architecture-style.md) | 架构风格：模块化单体 + 异步解耦 | Accepted | 2026-08-09 |
| [0003](0003-database-choice.md) | 数据库选型 | Accepted | 2026-08-09 |
| [0004](0004-message-queue.md) | 消息队列选型 | Accepted | 2026-08-09 |
| [0005](0005-ai-agent-architecture.md) | AI Agent 架构 | Accepted | 2026-08-09 |
| [0006](0006-ai-data-access.md) | AI 数据访问边界（MVP 直连 MySQL） | Accepted | 2026-08-10 |

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
```
