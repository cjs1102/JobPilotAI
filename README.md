# JobPilotAI

JobPilotAI 是一个基于 C++ 高性能后端架构的 AI 求职自动化平台：公开来源采集 +
人工采集 + 岗位导入/检索 + 投递管理 + AI 匹配。

> 本页只描述**当前真实能力**。未实现的部分在「未实现 / 非目标」一节明确列出。

## 当前能力

### C++ 服务（`service/`，P1 网络层行为冻结）

- 用户与鉴权：注册、登录、Redis 会话、`Authorization` 鉴权、`me`。
- 画像与技能：登录用户的画像读写、技能增删改查。
- 简历：上传、分页列表、文本版本生成、版本列表。
- 岗位读取：公开岗位搜索与分页、岗位详情与技能要求、`rule_v1` 确定性推荐排名
  （薪资格式/技能硬过滤、分数明细、稳定分页）。
- 投递：创建/列表/状态/统计，岗位与简历版本校验、重复投递保护、状态机。
- 岗位导入（内部）：`POST /internal/jobs/import`，token 保护、`source_url` 幂等
  upsert、事务内整组替换技能要求，批次 `1..100`。
- 来源观测（内部）：`POST /internal/jobs/source-sync`，`complete` 完整性语义、
  消失宽限与重新出现、freshness 可见性（`fresh`/`aging`/`stale`/`closed`）。
- AI 匹配（0013）：公开 `POST /api/v1/ai/match` + `GET /api/v1/ai/tasks/{id}`，
  token 保护的 `/internal/ai/tasks/*`，确定性 `match_rule_v1` 分数永不被告覆写。

### Python `source_adapters/`

- **公开来源适配器（0012）**：Greenhouse / Lever / Ashby，fixture 驱动，通过
  `POST /internal/jobs/source-sync` 发布观测；适配器从不直连数据库。
- **0014 采集框架**：robots.txt 逐路径判定（fail-closed）、Crawl-delay 下限、
  有界重试与退避、每次请求超时、诚实 User-Agent、charset 优先级
  （BOM > header > meta > utf-8）、HTML/JSON-LD/RSS/sitemap/JSON 解析、SSRF 守卫
  （含重定向目标与 DNS 复查）、响应体积上限、总请求预算与页数上限
  （触顶即 `complete=false`）、薪资只发布「月薪千元」可确定形态、F13 北京时间
  截止时间归一化。
- **人工采集（0014 Step 6，可用）**：浏览器书签小工具只读取当前已登录页面
  （无凭据、不发任何网络请求）→ 剪贴板 JSON；本机 publisher
  `python -m jobpilot_sources publish-capture` 复用既有
  `POST /internal/jobs/import`（`source="manual"`），失败可诊断且绝不误报成功。

### 0014 关键契约（已实现并有测试）

- **F13 截止时间**：中国招聘截止时间按北京时间（`Asia/Shanghai`，固定 UTC+8）
  解释，绝不用服务器本地时区；来源只给日期时按「北京正午 → UTC `04:00:00`」
  存储并置 `deadline_date_only=true`，读回按北京日期渲染（不渲染成正午到期时刻）；
  来源给了具体时刻则保留真实瞬时；类型未知保持 `NULL` 不臆造。权威约定见
  [`docs/03-database-design.md`](docs/03-database-design.md) §9.1。
- **F14 人工优先**：`source-sync` 观测命中 `source='manual'` 行时整条跳过
  （不改任何列、不改 `source`、不动 freshness、不替换技能要求、不关闭），
  并在响应里给出 `skipped_manual` / `skipped_manual_urls`。
- **既有库迁移**：`jobs` 新增 6 列（全部可空、无回填，含
  `deadline_date_only`）+ 0 索引；`docs/03` §9.4 的 ALTER 已在 pre-0014 库上实跑并
  用同一套 schema 断言复验。

## 未实现 / 非目标（不要误读为已具备）

- **没有真实的国内来源适配器**。0014 Step 5（首个真实中国来源）结论是
  **环境受限（environment-limited）**：国聘的岗位列表只能通过其客户端签名
  （`Sign`/`T`/`Nonce`，反自动化风控）访问；清华就业网的岗位列表主机
  `career.cic.tsinghua.edu.cn` 的 `robots.txt` 持续返回 503 → 按 fail-closed 不读取。
  因此**未编写国内来源适配器，也未注册任何国内 source 标识**；当前注册来源仍只有
  `greenhouse` / `lever` / `ashby`。证据：
  [`source_adapters/README.md`](source_adapters/README.md) 的「来源登记表」与
  [`cn_source_probe_2026-10-09.json`](source_adapters/jobpilot_sources/tests/fixtures/cn_source_probe_2026-10-09.json)。
- **不自动化登录墙**：不做自动登录、绕 CAPTCHA、伪造 cookie/UA、代理池；登录墙岗位
  只允许人工采集（书签小工具）。
- **不做通用爬虫/自动投递**：没有通用 HTML 爬虫、没有自动投递、没有 RabbitMQ /
  Gateway / SaaS RBAC。
- **截止时间不驱动生命周期**：`deadline` 只是展示字段，本阶段没有「过期自动关闭」
  或过期提示；过期的岗位仍然可见（已知可见性限制）。
- **书签小工具的真机运行未验证**：离线（node + fixture）已验证提取逻辑，但在真实
  登录墙页面上的点击运行仍需产品负责人自行执行。

## 技术栈

### Backend

- C++17、CMake、Conan

### Infrastructure

- MySQL 8.0、Redis 7、Docker Compose

### Libraries

- spdlog、yaml-cpp、nlohmann/json、hiredis、GoogleTest
- Python 侧（`source_adapters/`、`ai_worker/`）：标准库 + PyYAML

## 项目结构

```text
JobPilotAI
├── backend/                 # jobpilot_core：网络/HTTP、config/logging、DB、cache
├── service/                 # jobpilot_service_core + 可运行服务与测试
│   ├── config/config.yaml
│   ├── include/jobpilot/service/
│   ├── src/
│   └── tests/
├── source_adapters/         # Python 采集：公开来源适配器 + 0014 框架 + 人工采集
├── ai_worker/               # 0013 可选 AI worker（Python）
├── database/schema/         # MySQL 初始化脚本
├── docs/                    # 03 数据库 / 04 API / ADR / ExecPlan / ai agent 规则
├── docker/
├── docker-compose.yml
├── CMakeLists.txt           # 统一开发者入口（root preset）
├── CMakePresets.json
└── .env.example
```

## 构建与验证

```bash
# C++（service 与 root 两套 preset）
cmake --preset debug      && cmake --build --preset debug
ctest --preset release -L job          # 受影响标签；L4 见 docs/ai/agent-workflow.md

# Python 采集与人工采集
cd source_adapters && python3 -m unittest discover -s jobpilot_sources/tests -t .
python3 -m jobpilot_sources capture-bookmarklet --html     # 人工采集小工具
```

- 验证等级与何时测试：[`docs/ai/agent-workflow.md`](docs/ai/agent-workflow.md)。
- 数据库契约：[`docs/03-database-design.md`](docs/03-database-design.md)。
- API 契约：[`docs/04-api-design.md`](docs/04-api-design.md)。
- 当前阶段与下一步：[`docs/ai/project-state.md`](docs/ai/project-state.md)。
