# ADR-0004: 消息队列选型

## Status

Accepted as the future target; RabbitMQ is deferred from the MVP by
[ADR-0009](0009-ai-pipeline-and-provider-strategy.md).

The decision below remains historical target architecture. Its MVP sequencing
and Crawler-driven ingestion assumptions are not the current effective plan.

## Context

按 [ADR-0002](0002-architecture-style.md)，系统需要为两类异步工作负载解耦：

1. **AI 任务**：C++ Backend 生成岗位匹配 / 简历优化 / 面试题生成任务 → AI Service 消费 → 结果回写。LLM 调用秒级延迟，必须异步、可重试、可削峰。
2. **爬虫采集**：Crawler 产出原始岗位数据 → 去重入库。IO 密集、易失败、需重试。

需要选择消息队列承载方式。

## Decision Drivers

- **可靠性**：消息不能丢，消费失败要能重试
- **确认与重试语义**：需要 ACK、requeue、死信
- **运维成本**：单人项目，组件要轻、要有管理界面
- **与 C++/Python 双语言的客户端支持**

## Considered Options

| 选项 | 优点 | 缺点 |
|---|---|---|
| **RabbitMQ** | AMQP 成熟、ACK/重试/死信/优先级齐全、Web 管理界面、C++（amqp-cpp，Conan 包名 amqp-cpp）与 Python（pika）客户端都成熟 | 需部署一个中间件；吞吐上限对个人项目绰绰有余 |
| Kafka | 超高吞吐、日志语义 | 运维重（ZooKeeper/KRaft）、语义偏流式，对本项目是杀鸡用牛刀 |
| Redis List / Stream 模拟队列 | 零新增组件 | 无完整 ACK/死信/重试语义，消费确认要自己造轮子，恰恰是踩坑区 |
| 自研队列 | 无依赖 | 重造可靠性轮子，无展示价值（同自研 RPC 的问题） |

## Decision

采用 **RabbitMQ 3.x**，定义两类队列族：

```
ai.task.job_match         # 岗位匹配（C++ → AI）
ai.task.resume_optimize   # 简历优化（C++ → AI）
ai.task.interview_gen     # 面试题生成（C++ → AI）
crawler.job.raw           # 爬虫原始岗位（Crawler → C++ 入库消费者）
```

队列配置要点：

- **持久化**：exchange/queue/message 均 durable
- **ACK + requeue**：消费者处理失败 NACK requeue，配合 `ai_tasks` 表状态机做幂等
- **死信队列**：重试超过阈值进入 DLX，人工/定时补偿处理
- **QoS prefetch=1**：避免一条慢消息阻塞整批

## Rationale

1. RabbitMQ 在“可靠性 + 轻量 + 双语言客户端”三个维度恰好覆盖本项目需求
2. 消费端幂等由 `ai_tasks` 状态机保证（pending → processing → success/failed），队列只负责投递，不负责业务状态——这是正确分工
3. 否决 Kafka/自研队列，和 [ADR-0002](0002-architecture-style.md) 否决微服务的理由同源：规模未到，不为不存在的问题付费

## Consequences

### Positive

- AI 异步链路完整：发起 → 入队 → 消费 → 回写 → 轮询，可演示可扩展
- 爬虫与业务解耦，采集高峰不阻塞业务
- 面试可讲：MQ 在系统里的精确边界、消息可靠性、幂等消费

### Negative

- 新增一个中间件（docker 里多一个容器）
- C++ 端需要引入 MQ 客户端库并维护连接/重连

### Risks

- 消息重复消费 → 缓解：ai_tasks 状态机幂等、job 入库按 `source_url` 唯一去重
- C++ MQ 客户端连接断开 → 缓解：连接池 + 自动重连 + 失败回退写表

## Related Decisions

- [ADR-0002](0002-architecture-style.md)：解耦架构
- [ADR-0005](0005-ai-agent-architecture.md)：AI 任务队列的消费端
