# ADR-0006: AI 服务数据访问边界（MVP 直连 MySQL）

## Status

Superseded for MVP by
[ADR-0009](0009-ai-pipeline-and-provider-strategy.md). The AI worker should
use the C++ internal boundary rather than direct business-table access.

The direct-MySQL decision below is retained only as historical context.

## Context

AI Service 需要读写三类数据：任务状态（`ai_tasks`）、匹配输入（`jobs` 岗位描述、`resume_versions` 简历文本）、结果缓存（Redis）。

早期设计存在矛盾：架构图（02 §4.2）画了 AI 直连 MySQL 回写 `ai_tasks`，而 project-structure §5.1/§9 声明"禁止 ai/crawler 直连业务库"。

约束：

- 单人 MVP，2026 秋招前交付，时间窗口 1-3 个月
- `ai_tasks` 是 AI 域表，职责上归属 AI 能力
- 岗位/简历是业务域数据，正式形态下 AI 不应直连

## Decision Drivers

- **MVP 交付速度**：减少一层内部 HTTP 往返与一套内部接口的开发量
- **职责边界**：`ai_tasks` 属 AI 域，AI 直读写该表不违反"业务库归属"
- **演进清晰**：岗位/简历数据必须保持业务域边界，不能为省事放开

## Decision

1. **MVP 阶段（P3/P4）**：AI 服务**直连 MySQL**——读写 `ai_tasks`（AI 域表），直读 `jobs`/`resume_versions` 内容；结果缓存 Redis。
2. **正式形态**：AI 经 C++ Backend 内部 HTTP（`/internal/ai/*`）取数与回写，岗位/简历不经 AI 直连。
3. **面试口径**：先直连跑通，再演进为内部 HTTP——这是可解释的演进路径，不是偷懒。

## Rationale

1. `ai_tasks` 是 AI 域的持久化状态，AI 服务作为该表的所有者直连是职责正确的
2. 岗位/简历为只读输入且数据量小，MVP 直读换取交付速度，代价是耦合业务 schema；正式形态由内部 HTTP 收敛该耦合
3. 该决策把"边界"从工程细节提升为可演进的设计，面试可展开讲

## Consequences

### Positive

- P3/P4 无需开发内部接口，闭环最快
- 面试可讲"数据归属域 + 演进路径"的架构思维

### Negative

- AI 服务耦合业务表结构（`jobs`/`resume_versions`），表变更需同步 AI 侧
- 与 project-structure §9 的字面约束冲突，已在执行手册（08 §2.3 R1）中裁决

### Risks

- 业务表结构变更导致 AI 读取失败 → 缓解：AI 侧只读固定字段，变更走 03 文档评审流程
- 演进到内部 HTTP 时需改造 AI 数据访问层 → 缓解：数据访问集中在 `ai_service/app/repos/` 单点，替换成本低

## Related Decisions

- [ADR-0005](0005-ai-agent-architecture.md)：AI 服务形态
- [ADR-0004](0004-message-queue.md)：任务队列
- [ADR-0002](0002-architecture-style.md)：独立服务边界
