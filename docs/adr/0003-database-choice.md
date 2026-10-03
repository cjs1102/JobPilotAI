# ADR-0003: 数据库选型

## Status

Accepted

## Context

JobPilot AI 需要持久化：用户、简历、岗位、投递记录、AI 任务与面试记录。同时需要缓存会话、热点岗位与 AI 分析结果。需要确定主库与缓存选型，并明确向量数据库/对象存储是否进入 MVP。

## Decision Drivers

- **ACID 事务**：投递状态流转、简历版本管理等业务需要强一致
- **复杂关联查询**：岗位 × 技能 × 投递 × 统计
- **就业市场认知**：MySQL 是 C++ 后端岗位最主流的数据技术，面试必考
- **MVP 规模约束**：单用户量级，不引入无收益的基础设施

## Considered Options

### 主库

| 选项 | 优点 | 缺点 |
|---|---|---|
| **MySQL 8.0** | ACID 事务、生态成熟、就业市场主流、运维简单 | 全文检索弱（当前不需要） |
| PostgreSQL | JSONB、全文检索内置 | 团队（我）不熟，面试侧 MySQL 认知度更高 |
| MongoDB | Schema 灵活 | 无强事务，关系模型是反范式，岗位数据偏结构化 |

### 缓存 / 其他

- **Redis 7**：会话、热点岗位、AI 结果缓存 —— 无争议，必选
- **向量数据库（Milvus / FAISS）**：MVP 不引入。理由见 [ADR-0005](0005-ai-agent-architecture.md)：岗位匹配是“简历 vs 岗位”的单点比较，embedding 现算即可，检索没有用武之地
- **对象存储（MinIO / OSS / S3）**：MVP 简历文件用本地磁盘，量小且单人使用

## Decision

1. 主库：**MySQL 8.0**，单实例
2. 缓存：**Redis 7**
3. 简历文件：**本地文件系统**（`storage/{user_id}/resume/v*.pdf`），对象存储列入扩展项
4. 向量检索：**不引入向量数据库**，用 embedding 现算 + Redis 缓存结果

## Rationale

1. 业务是典型的关系型数据（用户/岗位/投递/简历版本），MySQL 事务与索引完全胜任
2. 个人项目规模下，多数据源带来的运维复杂度 > 收益，高级工程师会主动削减组件
3. 面试考核点在 MySQL：索引优化、事务隔离、连接池、慢查询 —— 这些在 MVP 内就完整覆盖

## Consequences

### Positive

- 单库单缓存，部署与备份简单
- 面试可深挖：索引设计（见 02 架构文档 §5）、事务、连接池
- 数据模型与后续扩展（软删除、分表）兼容

### Negative

- MySQL 8 全文检索能力有限（若需岗位关键词搜索，用 `LIKE + 倒排索引` 或引入 ES，当前用 `LIKE` 足够）
- 单机 MySQL 无法水平扩展（个人规模无此需求）

### Risks

- 简历文件落在本地磁盘，迁移/备份需随容器卷处理 → 缓解：docker volume 挂载

## Related Decisions

- [ADR-0001](0001-tech-stack.md)：技术栈
- [ADR-0005](0005-ai-agent-architecture.md)：向量检索的取舍
