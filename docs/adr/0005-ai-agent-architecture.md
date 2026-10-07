# ADR-0005: AI Agent 架构

## Status

Accepted as a target direction; MVP sequencing and MQ assumptions are revised
by [ADR-0009](0009-ai-pipeline-and-provider-strategy.md).

For the current effective baseline:

- The Python worker talks to the C++ internal AI API.
- RabbitMQ is not required.
- LangChain/Agent remains future work.
- Provider selection is BYOK and provider-neutral.

## Context

JobPilot AI 的智能能力包括：岗位匹配、简历优化、面试题生成、以及一个能自主编排的求职 Agent。需要决定 AI 能力以何种形态接入系统，以及 RAG/向量检索是否进入 MVP。

关键约束：

- LLM 调用是秒级延迟 + 偶发失败 + 有成本 → 必须异步
- AI 服务不参与核心业务（不直接持有用户/岗位主数据）
- 单人项目，不能引入不可控的复杂度

## Decision Drivers

- **异步化**：AI 耗时操作不能阻塞 HTTP 同步链路
- **解耦**：AI 服务通过 [ADR-0004](0004-message-queue.md) 的队列消费任务，通过内部 HTTP 获取数据，结果回写 `ai_tasks`
- **可解释性**：岗位匹配要能回答“为什么这个分数”，不能是纯黑盒
- **向量检索的必要性**：MVP 数据量（几十~几百个岗位）下，检索没有存在价值

## Considered Options

### AI 服务形态

| 选项 | 优点 | 缺点 |
|---|---|---|
| **独立 FastAPI 服务 + MQ 消费者 + 内部 HTTP** | 异步、解耦、可重试；与 C++ 业务边界清晰 | 需维护一套服务与队列 |
| 同步 HTTP 调 LLM | 实现最简单 | 秒级阻塞连接池；失败无重试；前端必须同步等 |
| 完全内置在 C++ 里 | 无 | C++ 侧 LLM/Agent 生态差，成本最高 |

### 检索方案

| 选项 | 优点 | 缺点 |
|---|---|---|
| **Embedding 现算 + Redis 缓存**（本方案） | 简单；结果可解释；缓存命中免重复调用 | 岗位量大后查询慢（当前规模无感） |
| 完整 RAG + 向量库（Milvus/FAISS） | 检索能力强、显“高级” | 数据量小毫无收益；引入重组件；运维成本高 |

## Decision

1. **AI 服务 = 独立 Python 进程**（FastAPI 提供健康检查/内网接口），消费 `ai.task.*` 队列，通过**内部 HTTP** 向 C++ Backend 获取简历与岗位数据，结果写回 `ai_tasks` 表并缓存到 Redis
2. **岗位匹配管线（可解释评分）**：结构化解析 → 关键词/规则打分（基线） → Embedding 余弦相似度 → LLM 综合评分（输入基线分 + JD，输出分数/优势/不足/建议）→ 结构化 JSON 落库
3. **求职 Agent**：LangChain 编排，持有 JobSearch / JobAnalyze / ResumeOptimize / InterviewGen 四个工具，工作流 = 读画像 → 检索岗位 → 逐岗匹配 → 生成推荐 → 写通知
4. **Embedding**：本地 `sentence-transformers`（`BAAI/bge-small-zh-v1.5`），离线可用、零 API 成本
5. **LLM**：DeepSeek / Qwen 开放 API（OpenAI 兼容协议），Ollama 本地模型为扩展项
6. **否决**：同步 LLM 调用、完整 RAG 向量库进入 MVP

## Rationale

1. **先解释再智能**：匹配分数 = 可解释的规则分 + embedding 分 + LLM 分，面试能讲清每一层的作用，这是“高级工程师”与“调 API”的区别
2. **异步是硬约束**：LLM 延迟决定了它必须走队列 + 状态机，这也顺带展示了工程能力
3. **不引入向量库**：量级没到，引入就是给自己挖坑；把“为什么不做 RAG”讲清楚反而是加分项
4. Agent 用于展示“自主性”，但边界收敛在四个工具内，不做失控的自主循环

## Consequences

### Positive

- 异步 AI 链路完整可演示：发起任务 → 队列 → 消费 → 回写 → 前端轮询
- 匹配结果可解释，面试可深挖每一层
- 零向量库/零本地大模型成本，部署轻

### Negative

- 需要维护一套 Python 服务与 MQ 消费端
- 本地 embedding 模型首次需下载（约 100MB），离线分发需随包

### Risks

- LLM API 限流/欠费 → 缓解：Redis 结果缓存 + 超时 + 重试上限 + 降级返回规则分
- Agent 编排失控 → 缓解：工具白名单 + 单轮编排 + 输出 JSON 校验

## Related Decisions

- [ADR-0004](0004-message-queue.md)：任务队列
- [ADR-0002](0002-architecture-style.md)：独立服务边界
- [ADR-0003](0003-database-choice.md)：不引入向量库
