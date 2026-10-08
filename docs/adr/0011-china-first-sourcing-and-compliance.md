# ADR-0011: 中国市场定位、合规采集边界与校招优先

## Status

Accepted (2026-10-08). Supersedes the "overseas ATS first" assumption behind the
0012 source adapters; 0012 itself (freshness lifecycle, source-sync contract) is
unchanged and still required.

## Context

项目原方向与两个头部开源项目对齐：面向海外 ATS（Greenhouse/Lever/Ashby）的公开
接口做岗位采集。产品负责人明确修正方向：

- 要能**真正用于自己在国内找工作**（自动化求职全流程），而不是演示型采集器。
- 重点市场是**中国**：国内招聘平台（Boss 直聘、智联、51job、猎聘、脉脉等）
  普遍登录墙 + 强反爬 + 风控，且自动化抓取/投递有明确合规与封号风险。
- 首批场景是**校招/应届生**，并且要覆盖**各公司官网的招聘信息**。
- 明确要求："多借鉴这两个项目怎么做采集，但不能违法"；不自动投递。

参考：[career-ops-hq/career-ops](https://github.com/career-ops-hq/career-ops)
（`providers/ADDING_A_PROVIDER.md`、`docs/SUPPORTED_JOB_BOARDS.md`、`check-liveness.mjs`）、
[MadsLorentzen/ai-job-search](https://github.com/MadsLorentzen/ai-job-search)
（`.claude/commands/add-portal.md`、`.agents/skills/*/cli/src/helpers.ts`、`tools/robots_check.py`）。

## Decision

### 1. 定位

- **中国优先**；校招/应届为主流程，社招作为后续扩展。
- **公司官网招聘页是一等来源**（含其使用的国内 ATS：Moka、北森、e 成等公开页）。
- 核心价值是**匹配 + 简历定制 + 投递管理**；来源只是输入。

### 2. 采集策略（合规优先，三条通道）

1. **公开来源自动同步**：企业官网招聘页、公开 REST/JSON-LD/RSS/sitemap、
   公开的高校就业网/国聘等。走 0012 已有的
   `POST /internal/jobs/source-sync`（含 `complete` 语义与 freshness 生命周期）。
2. **人工采集导入**：用户在自己已登录的账号里操作，通过本机手段把岗位送进系统——
   浏览器书签脚本（当前页面一键采集）或粘贴 JD 文本，走
   `POST /internal/jobs/import`（1..100 条）。**这是覆盖登录墙平台的正规途径。**
3. **明确不做**：自动化登录、绕过 CAPTCHA、伪造 cookie/UA、批量抓取登录墙平台、
   自动提交投递。

### 3. 来源适配器的强制纪律（吸收两个项目的做法）

每条来源适配器都必须满足（缺一项即视为未完成）：

- **robots.txt 逐路径判定**：`User-agent: *` 或匹配本工具的分组下，如屏蔽了
  实际读取的列表路径 → 放弃该来源（先例：Lagou 被 career-ops 判定为 stop）。
  仅屏蔽解析器不触碰的路由（如 `/api`、订阅表单）不构成放弃理由。
- **仅公开、无需登录的源**；登录墙来源一律拒绝并给出明确提示（ai-job-search
  `/add-portal` 的做法）。
- **站内间隔**：默认 150–250ms；`Crawl-delay` 作为下限；仅在实测到限流时上调。
- **有界重试**：只重试 429 / 5xx / 传输错误（超时、DNS），指数退避 + 抖动，
  `Retry-After` 采纳但截断（防止 `Retry-After: 86400` 拖死），
  非 429 的 4xx 与拒绝的重定向一律不重试。默认
  `{retries: 2, baseDelayMs: 500, maxDelayMs: 8000}`。
- **请求超时**（默认 10s）与**诚实 User-Agent**（自报工具名，绝不伪装成完整浏览器）。
- **页数硬上限**（MUST）+ 触顶告警；未能走完整个来源时按 0012 契约发布
  `complete=false`，绝不冒充完整观测。
- **SSRF / 私网防护**：所有出站 URL 集中校验，拒绝回环、私网与元数据地址，
  并有"防止回退"的来源级测试。
- **防御式解析**：字段缺失 → 跳过/告警，绝不编造；共享 HTML 实体解码器；
  对来源提供的 id/slug 做 URL 编码。
- **浏览器方案仅限独立工具**：若某来源确实只能靠渲染，则做成独立脚本，
  只用公开页面、不登录、不绕验证码，绝不进入核心链路。
- **每来源离线测试 + fixture**，真机 smoke 为可选。

### 4. 数据模型与匹配的中国化增量

- `jobs` 增量字段：`education_required`、`experience_years_min/max`、
  `job_nature`（校招/社招/实习）、`city`/`district`、`company_size`、
  `company_industry`、`deadline`、`benefits`。
- 简历/画像增量：届别、院校、学历、实习经历、项目。
- 匹配：新增**关键词过筛**（中文关键词 + 字符 n-gram，**不引入分词库依赖**）、
  学历/年限/城市/薪资档硬过滤、应届身份校验；AI 只在规则层之上做解释，
  不参与权威分数（沿用 ADR-0009 与 0013 决定）。

### 5. 架构映射

```text
cn_adapters (Python, stdlib)  ──公开源──►  POST /internal/jobs/source-sync
浏览器书签脚本 / 粘贴 JD      ──人工───►  POST /internal/jobs/import
                                              │
                                     C++ job/AI 模块（不感知来源细节）
```

C++ 侧不需要为"来源"新增概念；新增的是 Python 适配器集合与一个采集入口。

## Consequences

### Positive

- 覆盖真实求职路径：公开源自动 + 登录平台人工采集，既可用又不越界。
- 合规风险可控：不登录、不绕验证码、不自动投递、尊重 robots 与限流。
- 复用既有资产：0012 的 source-sync / freshness / 幂等，0013 的匹配与任务生命周期
  都与中国市场无关，直接可用。

### Negative

- 登录墙平台无法自动覆盖，依赖人工采集的产品体验设计。
- 校招来源分散（高校就业网、公司官网、公众号），适配器数量多、维护成本高。
- 中文简历与 ATS 关键词过筛需要额外的模板与中文处理工作。

### Neutral / 需要后续决策

- 0012 的三个海外适配器（Greenhouse/Lever/Ashby）降级为可选来源，保留不删。
- 新增"中国来源与采集"阶段需要 ExecPlan 编号（拟 0014，简历定制顺延）。
- 浏览器书签脚本需要产品形态决定（书签小工具 vs 本地 HTTP 端点）。

## Rejected Alternatives

- **自动化登录抓取 Boss/智联/51job/猎聘**：违反平台 ToS，封号与法律风险高。
- **浏览器扩展自动登录并批量采集**：同上，且平台改版即失效。
- **自动投递**：国内以 APP 内投递为主，自动化风险最高；两个头部开源项目也明确拒绝。
- **付费解锁/代理抓取服务**：默认不引入第三方付费依赖与凭据。
- **中文分词库（jieba 等）作为核心依赖**：与"轻量、可离线、无重依赖"冲突，
  关键词过筛用 n-gram + 词典即可。
- **文件为唯一事实源**（career-ops 的做法）：我们是多用户服务，MySQL 保持唯一权威。
