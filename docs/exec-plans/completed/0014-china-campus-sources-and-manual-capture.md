# ExecPlan 0014: China Campus Sources + Manual Capture

> Status: **Completed and archived** (2026-10-09). Step 8's single L4 passed
> (§21.2) and Step 7's documentation sync is done (§21.1); the plan is frozen here
> as historical evidence.
>
> **Carried forward — Step 5 is environment-limited, NOT done.** No compliant,
> reusable China list contract could be verified: 国聘's job list requires its
> client-side anti-automation signature (`Sign`/`T`/`Nonce`) and 清华's job-list
> host `career.cic.tsinghua.edu.cn` returns a persistent `robots.txt` HTTP 503 →
> fail-closed. Therefore **no China source adapter is implemented and no China
> `source` identifier is registered**; the registered sources remain
> `greenhouse`/`lever`/`ashby`. This archive must not be read as "China sources
> are done". Re-entry conditions: §20.4. The real-browser bookmarklet acceptance
> is still **UNVERIFIED** (§19.5).
> 真实浏览器书签验收的操作记录与限制见 §19.6。
>
> Historical planning-phase status, preserved as written: "Active — Research /
> Design / ExecPlan (this document). No production code is written by this
> stage's planning phase." Execution evidence lives in §18-§21; Step 5 conclusion:
> §20.3/§20.4.
>
> Task level: L4 (new subsystem boundary: source collection discipline, one
> additive migration, Python adapter framework, manual-capture path).
>
> Verification rules: `docs/ai/agent-workflow.md`. Testing levels: `docs/quality/README.md`.
>
> Dependencies: ADR-0011 (Accepted), ADR-0008 + its 0012 amendment, ADR-0009 +
> its 0013 amendment, ADR-0010, ExecPlan 0012 (completed), ExecPlan 0013
> (completed).
>
> **Contract conflict found during research — reported to the Lead, see §15.1.**
> `docs/exec-plans/completed/0013-ai-matching.md` §3/§14 assign stage number
> **0014 to resume tailoring**, while ADR-0011 §4 and
> `docs/ai/project-state.md` §"Next Entry" assign **0014 to China sourcing** and
> defer tailoring. This plan follows the later decision (ADR-0011 + project-state)
> and records the discrepancy instead of silently overwriting the 0013 text.

## 1. Objective

Make JobPilotAI usable for real 国内校招/应届求职 by adding the two合规 collection
channels ADR-0011 decided, **without** adding a general crawler and **without**
touching the login-walled platforms:

```text
(a) 公开来源自动同步   公司官网招聘页 / 国内 ATS 公开页 / 校本就业网 / 国聘
        -> Python cn adapters (stdlib only)
        -> Normalize + 去重(per-source identity)
        -> POST /internal/jobs/source-sync        [reuses 0012 verbatim]

(b) 人工采集导入       浏览器书签小工具只读取当前页 -> 剪贴板/JSON 文件
        -> 本机 publisher (no browser credentials)
        -> POST /internal/jobs/import  (source="manual")   [reuses 0010/0012]

(c) 明确不做           自动登录、绕 CAPTCHA、伪造 cookie/UA、自动投递
```

The stage is complete when a reviewer can: register a China source in a local
config, run the adapter offline against a recorded fixture, publish one honest
observation through `/internal/jobs/source-sync`, see the jobs in public search,
capture one login-walled posting by hand through the bookmarklet +
`/internal/jobs/import` path and see it appear, and read the per-source
robots/rate-limit evidence in the run log — with **no** closure of a job that
was merely missed once.

## 2. Background and Current State

Verified current state at planning time (HEAD `bf5053f`, clean working tree):

- **0012 is complete and reusable.** The source-observation pipeline exists end
  to end: additive freshness columns, `SourceObservation` domain,
  `SourceSyncService`, `POST /internal/jobs/source-sync`, and freshness-aware
  public search. Evidence: §4 rows R1-R6.
- **0013 is complete.** AI tasks, deterministic `match_rule_v1`, and the Python
  `ai_worker/` exist; 0014 does not need any AI work and must not make AI a
  lifecycle authority (`docs/adr/0009` + its 0013 amendment, `docs/adr/0010` §6).
- **There is no China source, no HTML/JSON-LD/RSS parsing, no robots check, no
  rate limiting, and no retry** anywhere in the adapter package. Evidence: §4
  rows R7-R10.
- **The observation source allowlist is hardcoded in C++ to three overseas
  providers**, so adding a China adapter requires a C++ change, not only a Python
  file. Evidence: §4 row R7.
- **The manual import path already exists and is login-free by construction**: it
  has no `complete` semantics, so manual rows can never drive the freshness
  lifecycle (`docs/04-api-design.md` §5.4, lines 435-437, 479-488).
- Product direction is China-first per `docs/adr/0011`, and `AGENTS.md` line 8-13
  records 0013 archived with no active plan.

## 3. Scope

### 3.1 In Scope

- 来源优先级清单与逐来源「公开且免登录」判定（§4.1），含明确的 OUT 名单。
- C++：来源 allowlist 扩展（新增 China 来源标识），并保持 0012 校验语义不变。
- Python `source_adapters/`：新增中国来源适配器 **框架**（robots 逐路径判定、
  Crawl-delay 下限、站内间隔、有界重试、超时、诚实 UA、页数硬上限、SSRF/私网防护、
  防御式解析、编码处理、离线 fixture 与契约测试）。
- 至少 **2 个真实公开来源**的适配器，且每个都已用真实请求验证过 robots 与响应形状
  （未验证前按 §12 stop condition 停在设计）。
  实测 → 登记 → 放行 allowlist 的先后顺序见 §9 Step 1/Step 2/Step 3 的依赖；
  任何未提交实测记录的来源标识**不得**先写入 allowlist。
- 人工采集通道：书签小工具（只读当前页 → 剪贴板/文件）+ 本机 publisher
  走既有 `POST /internal/jobs/import`（`source="manual"`）。
- 字段增量：校招真正需要的最小列表（§5.1），含 `docs/03` 契约与既有库 ALTER。
- **不**引入来源级消失策略：本阶段新增的 China 来源一律以 `complete=false` 上报，
  `CloseUnseenForScope` 路径不可达，继续使用 0012 的全局 `MissingPolicy{}`
  （`source_sync_service.cpp` L50；0012 §18 L614-615）。ADR-0010 §3 的
  「per adapter 可配置」只对能证明完整覆盖的来源生效，作为后续阶段记录（§15.3），
  不得在本阶段私自放宽或收紧闭合条件。
- 文档：`docs/03`（新增迁移节）、`docs/04`（§5.2/§5.4/§5.5 增量字段与人工采集说明）、
  `source_adapters/README.md`、`docs/ai/project-state.md`。

### 3.2 Out of Scope

- 登录墙平台（Boss 直聘 / 智联 / 51job / 猎聘 / 脉脉 / 拉勾 / 实习僧 等）的自动化采集。
- 自动登录、绕过 CAPTCHA、伪造 cookie/UA、代理池、付费解锁。
- 自动投递（AutoApply）与任何表单提交。
- 通用爬虫 / 通用浏览器爬取 / Playwright 核心依赖。
- 中文分词库（jieba 等）作为核心依赖（ADR-0011 rejected alternatives）。
- Canonical Job / 多源 alias 合并（ADR-0010 明确延后）。
- `city`/`district`、`company_size`、`company_industry`、`benefits` 列（ADR-0011 §4
  列名但本阶段不落库，理由与后续归属见 §5.1/§15.3）。
- 新字段的公开硬过滤/排名消费：`GET /api/v1/jobs`、`GET /api/v1/jobs/recommended`
  本阶段**不新增** `education_required`/`job_nature`/`experience_years_*`/`deadline`
  请求参数，`rule_v1` 权重不变（见 §5.1 末、§15.3）。
- 简历定制 / `resume_optimize` / `interview_gen`（属于后续 tailoring 阶段）。
- RabbitMQ、调度器集群、前端 UI、微信公众号采集。
- 对公开 `GET /api/v1/jobs` 既有字段语义的任何破坏性变更。

## 4. Research Findings (file-level evidence)

### 4.1 来源优先级与「公开且免登录」判定

判据（与 ADR-0011 §3 一致，可在实现期逐条验证）：**免登录可读**、
**robots 不禁止实际列表路径**、**无 CAPTCHA/风控**、**无需平台账号**、
**不自动提交**。下表把“架构可行性”与“访问规则”分开，访问规则一律标记为待实测：

| # | 来源类别 | 示例 | 响应形态（预期） | 免登录判定 | 0014 处置 |
|---|---|---|---|---|---|
| S1 | 公司官网招聘页 / 国内 ATS 公开页 | Moka、北森(Beisen)、e 成(eCheng) 的公开职位页 | HTML + 内嵌 JSON/JSON-LD，分页 | 预期公开免登录（**待实测**） | **一等来源，优先实现** |
| S2 | 公开校招/官方门户 | 国聘、国资央企校招公开页 | HTML/JSON，分页 | 预期公开免登录（**待实测**） | 优先 |
| S3 | 高校就业网 | 各校就业信息网公开列表 | HTML/RSS，分页 | 预期公开免登录（**待实测**），站点差异极大 | 次优先，按校逐个接 |
| S4 | 公开聚合/数据集 | 公共岗位数据集、公开 CSV/JSON、sitemap | JSON/CSV/XML | 公开即接入 | 作为 fixture/回归来源 |
| S5 | RSS/Atom feed | 公司/学校自建 feed | XML | 公开即接入 | 复用 S1-S3 的解析层 |
| O1 | Boss 直聘 / 智联 / 51job / 猎聘 / 脉脉 / 拉勾 / 实习僧 | — | 登录墙 + 强反爬 | **禁止自动化**（ADR-0011 §2.3） | **OUT**；只走人工采集 |
| O2 | 微信公众号 / APP 内推 | — | 无公开 Web API | 无法免登录读取 | **OUT** |
| O3 | 任何需登录的 ATS 后台 / 内推系统 | 企业内网招聘系统 | 需账号 | — | **OUT** |

排期依据（工程成本，不含来源本身的合法性判断）：S1/S2 单来源接入成本 ≈
“一个 transport + 一个 DOM/JSON 解析器 + 一份 fixture”；S3 是「一校一适配」，
成本按校数线性增长，因此 0014 只做 **1-2 所** 样板校并在文档中给出接入模板。

> 证据边界：上表的访问规则、字段名、分页参数、编码、日期/薪资格式与 robots
> 判定，**本阶段无网络访问，一律未实测**，实现期必须逐来源实测并按 §14 stop
> condition 处理不合格来源。

### 4.2 现有能力与证据

| ID | 结论 | 证据（实际读到的文件与行号） |
|---|---|---|
| R1 | 观测端点为 `/internal/jobs/source-sync`，`complete=true` 允许 `0..200`、`false` 允许 `1..200` | `service/include/jobpilot/service/job/source_observation.h` L36-39；`docs/04-api-design.md` §5.5 L529-534 |
| R2 | 手工导入为 `/internal/jobs/import`，`1..100`，无 `complete` 语义 | `docs/04-api-design.md` §5.4 L435-437, L479；`service/src/job/job_ingestion_service.cpp` L19 `kMaxBatchSize = 100`, L163-167 |
| R3 | 生命周期：完整观测缺失才 `missing_count += 1`；仅当 `>=2` 次或超过宽限（默认 48h）才关闭；重开清空 | `docs/adr/0010-job-freshness-and-lifecycle.md` L98-118；`service/src/job/source_observation.cpp` L419-431；`service/include/jobpilot/service/job/source_observation.h` L41-54 |
| R4 | 不完整观测绝不关闭、不递增缺失计数 | `docs/04-api-design.md` §5.5 L533；`service/src/job/source_sync_service.cpp` L44-53（`CloseUnseenForScope` 仅在 `complete=true` 调用） |
| R5 | 幂等键是 `source_url UNIQUE`；`source`/`source_scope`/`external_id`/`published_at` 为 0012 增量列 | `database/schema/03_job.sql` L13, L15-22, L25；`docs/03-database-design.md` §7.2 L274-282 |
| R6 | 公开搜索已按 `status=1 AND closed_at IS NULL` + stale 排除（`include_stale` 例外）；无 `last_seen_at` 的旧行不算 stale | `docs/04-api-design.md` §5.1 L330-343；`service/include/jobpilot/service/job/job_search_filter.h` L17-22 |
| R7 | **来源 allowlist 硬编码为 3 个海外 provider** | `service/src/job/source_observation.cpp` L297-303 `IsSupportedObservationSource` |
| R8 | **手工导入的 `source` 只能取 `manual`/`import`/`crawler`** | `service/src/job/job_ingestion_service.cpp` L60-66 `IsSupportedSource` |
| R9 | Python 侧只有 JSON 传输、无 HTML/RSS/编码处理 | `source_adapters/jobpilot_sources/client.py` L81-121（仅 `get_json`/`post_json`）；L15-16 `DEFAULT_USER_AGENT`/`DEFAULT_TIMEOUT_SECONDS = 15.0` |
| R10 | **无 robots、无间隔、无重试、无退避、无 SSRF 防护** | `grep -rn "robots\|delay\|retry\|backoff" source_adapters/jobpilot_sources/*.py source_adapters/jobpilot_sources/providers/*.py` → 无匹配；`grep -c "time.sleep"` → 全为 0 |
| R11 | 归一化只支持 ISO-8601 与 epoch 毫秒；缺失即省略，绝不编造 | `source_adapters/jobpilot_sources/normalizer.py` L55-92, L182-203 |
| R12 | 薪资列契约是「月薪千元」且带币种，海外适配器因此不发布薪资 | `database/schema/03_job.sql` L8-10；`source_adapters/README.md` L85-88 |
| R13 | 适配器必须无 DB 驱动、无 SQL、除 PyYAML 外无第三方依赖（已有 guard 测试） | `source_adapters/jobpilot_sources/tests/test_isolation.py` L20-33, L81-92, L94-102 |
| R14 | 既有 fixture 驱动测试与 `--dry-run` 先例 | `source_adapters/README.md` L98-111；`source_adapters/jobpilot_sources/tests/fixtures/`（4 个 JSON） |
| R15 | 每批 200 上限导致超限 board 必须以 `complete=false` 分批上报 | `source_adapters/jobpilot_sources/publisher.py` L14, L62-93 |
| R16 | 迁移契约的既有格式（列 + 索引 + ALTER + 不变量 + 实施顺序） | `docs/03-database-design.md` §7 L241-310 |
| R17 | 状态表与验证级别 | `docs/quality/README.md` L20-52（L1-L5）；`docs/ai/agent-workflow.md` §2-§3 |
| R18 | 计划模板与必需章节 | `docs/exec-plans/README.md` L15-27；`~/.agents/skills/engineering-research-plan/references/plan-template.md` L3-13 |

### 4.3 能力缺口（现有三适配器 → 中国来源）

| 维度 | 现有（Greenhouse/Lever/Ashby） | 中国来源需要 | 归属步骤 |
|---|---|---|---|
| 响应形态 | 纯 JSON API | HTML + 内嵌 JSON + JSON-LD + RSS/sitemap | Step 3 |
| 分页 | 单请求即整板 | 页码/cursor 分页 + 详情页补全 | Step 3 |
| 编码 | 假定 UTF-8 JSON | `utf-8`/`gbk`/`gb18030` Content-Type 与 `<meta charset>` 判定 | Step 3 |
| 时间格式 | ISO-8601 / epoch ms | `2026-10-08`、`2026/10/08`、`10月8日`、`刚刚`、`N天前`、必要时才用 epoch | Step 3 |
| 薪资 | 绝对值年包/展示串（当前不发布） | `15-25K·14薪`、`8k-12k`、`面议`、`300/天`（实习）→ 需要单位/月数规则 | Step 3 |
| 学历/年限/性质 | 无 | `学历`、`经验`、`校招/社招/实习` | Step 4 |
| 城市 | 单个自由文本 location | 城市 + 区县，别名（深圳/深圳市） | Step 4 |
| 公司规模/行业/福利 | 无 | 可选，低价值 | Step 4（建议只存描述） |
| 截止时间 | 无 | `deadline` 常见于校招 | Step 4 |
| 去重身份 | `source_url UNIQUE` | 同左；额外需要 URL 规范化（tracking 参数、末尾斜杠、大小写 scheme） | Step 3 |
| 完整性证明 | Greenhouse `meta.total`；Lever/Ashby 契约 | 多数国内页**无法证明完整覆盖** → 必须 `complete=false` | Step 3/6 |
| 访问纪律 | 无 robots/间隔/重试 | robots 逐路径 + Crawl-delay 下限 + 有界重试 + 超时 + UA | Step 3 |
| 安全 | 无出站 URL 校验 | SSRF/私网/回环/元数据地址拒绝（含重定向后） | Step 3 |

## 5. Contracts

### 5.1 增量字段契约（建议最小集）

`docs/03` §3 的 `jobs` DDL 当前列见 `database/schema/03_job.sql` L3-31。建议增量：

```sql
education_required   TINYINT     DEFAULT NULL COMMENT '最低学历:1不限/2大专/3本科/4硕士/5博士(0014)',
experience_years_min SMALLINT    DEFAULT NULL COMMENT '最低经验年限(0014)',
experience_years_max SMALLINT    DEFAULT NULL COMMENT '最高经验年限(0014)',
job_nature           TINYINT     DEFAULT NULL COMMENT '岗位性质:1校招/2社招/3实习(0014)',
deadline             DATETIME    DEFAULT NULL COMMENT '投递截止时间(UTC瞬时,0014)',
deadline_date_only   TINYINT     DEFAULT NULL COMMENT 'deadline是否仅来源日期:1是/0否/NULL未知(0014)'
```

`deadline_date_only` 是 F13 决策落地时**派生**出的必要列（依据见 §18.1）：
只存一个 UTC 瞬时无法同时满足「date-only 按北京正午存储且按日期往返」与
「正午不得被渲染成到期时刻」，也无法区分真实落在 `04:00:00` UTC 的具体时刻。
**按计划 `idx_job_nature_education` 不建**（延迟到后续检索阶段）。

**推荐（不是全部采纳 ADR-0011 §4 的列表）**：

| ADR-0011 字段 | 0014 处置 | 理由 |
|---|---|---|
| `education_required` | **新增列** | 校招硬过滤/匹配的必要条件；枚举化可测试 |
| `experience_years_min/max` | **新增列** | 应届/实习的核心判据；数值可比较 |
| `job_nature`（校招/社招/实习） | **新增列** | ADR-0011 §4 明确要求；也是“校招优先”的可见性开关 |
| `deadline` | **新增列** | 校招强时效信号；`deadline` 早于今天可用于“已过期待核实”提示（**不**据此关闭岗位） |
| `city` / `district` | **不新增**，复用 `location`；实现期约定 `城市`/`城市-区县` 两种形态 | `location VARCHAR(64)` 已存在（`03_job.sql` L7），单独拆列会同时改动检索、排名与公开响应 |
| `company_size` / `company_industry` / `benefits` | **不新增**，先写入 `description` 文本；`benefits` 列表暂不进 `job_requirements.skill` | 匹配价值低、来源不稳定；塞进 `job_requirements` 会污染技能匹配 |
| `salary` 单位/月数 | **不新增列**；实现期在适配器侧归一化为「月薪千元」整数，无法归一化则**不发布** | 列契约是月薪千元（`03_job.sql` L8-9）；`面议`/日薪/年包月份不明必须省略（沿用 R12 的诚实口径） |

迁移成本：`database/schema/03_job.sql` 追加 6 列（含派生的 `deadline_date_only`）+ 0 索引
（`idx_job_nature_education` 按确认决定延后）；`docs/03` 新增 §9 契约节
（§3 DDL、§5 索引表说明、既有库 ALTER 三处更新，格式照 §7 L241-310）；既有行全部
`NULL`，无需回填；实现步骤是一段 ALTER 加 schema 测试，属 0012/0013 同类成本。

**字段落点与写入/读回语义（Step 4 的验收对象）**：

- 传输键名（`docs/04` §5.4/§5.5 使用同一 job 对象）：`education_required`、
  `experience_years_min`、`experience_years_max`、`job_nature`、`deadline`、
  `deadline_date_only`；既有键语义不变。`source_url` 仍必填且全局唯一（§5.3）。
- DTO/校验：`JobImportData`（`job_ingestion_dao_interface.h` L12-23）新增对应可选字段；
  **新增键必须同时接入两条路径的 JSON 解析函数**，否则服务端读不到、字段静默丢失：
  人工路径 `ParseJob`（`internal_job_import_controller.cpp` L285-350）、
  观测路径 `ParseObservationJob`（`internal_source_sync_controller.cpp` L303-376）
  按既有 `GetOptionalString`/`GetOptionalUnsigned` 风格补可选键（`deadline` 为字符串键，
  走 `GetOptionalString` + `IsValidDbTimestamp` 校验）。
  校验：两条路径各自补校验——观测路径 `ValidateObservationJob`
  （`source_observation.cpp` L145-224），人工路径 `ValidateJob`
  （`job_ingestion_service.cpp` L68-148）：枚举范围、`experience_years_min <=
  experience_years_max`、`deadline` 非空时必须过 `IsValidDbTimestamp`
  （`source_observation.cpp` L305-360；声明 `source_observation.h` L80-83）。
  非法值返回 `kInvalidParam` → `400`（`source_sync_service.cpp` L21-26），
  **绝不静默丢弃、钳制或截断**。
- 值域与未知值：`education_required` 只认 `1..5`、`job_nature` 只认 `1..3`，
  未枚举出的整数一律拒绝；来源未提供或无法确定时**省略该键**（不写 `1` 兜底）；
  `experience_years_min/max` 为非负整年，负值/超出 `SMALLINT` 拒绝。
- **`deadline` 的日期/时刻与时区语义（F13 已决定，见 §18.1）**：全部中国招聘
  截止时间按 **Beijing time**（代码中的 `Asia/Shanghai`，固定 UTC+8，绝不使用服务器
  本地时区）解释。来源只给日期时存「北京本地正午 → UTC」= `04:00:00`，读回时
  加 `+08:00` 还原北京日期（日期往返不漂移）；正午只是**存储约定**，
  **不是到期时刻，不得被渲染成到期时刻**（API 对 date-only 输出裸 `YYYY-MM-DD`）。
  来源给了具体时刻时保留真实瞬时，按其来源时区换算为 UTC，绝不改写成正午。
  类型未知（既有行/直写）时 `deadline_date_only = NULL`，**不臆造类型**：保留瞬时、
  API 输出北京本地 `YYYY-MM-DD HH:MM:SS` 且 `deadline_date_only` 为 `null`。
  格式/时区的唯一权威落点是 `docs/03` §9.1。
- `deadline` 列、DTO、两个 JSON 解析函数、两条校验路径、两处 upsert、三处 SELECT
  与响应映射**全部落地**（Step 4 不再有 Pending 标记）。
- 本阶段**不新增** `deadline` 到期提示/自动关闭规则：过期岗位仍可见，
  该限制记录在 `docs/04` §5.1（后续阶段处理）。
- **同一 `source_url` 的优先级（F14 已决定，见 §18.2）**：人工采集（`source=manual`）
  **胜出**。观测路径命中 `source='manual'` 行时整条跳过：不更新任何列、不改 `source`、
  不动 freshness/`closed_at`/`missing_count`、不替换其 `job_requirements`，
  并计入响应 `skipped_manual` / `skipped_manual_urls` 与服务端日志。
  反向（人工导入覆盖观测行）保持既有整组覆盖语义。
- 持久化：两处 upsert 都要写新列——人工路径 `job_ingestion_dao.cpp` L44-68、
  观测路径 `source_sync_dao.cpp` L50-91。观测路径对来源可缺省字段沿用 0012 的
  `COALESCE(VALUES(x), x)` 保护（`source_sync_dao.cpp` L70-71），
  防止「本次未带 `deadline`」抹掉已观测事实；人工路径保持整组覆盖语义。
- 读回：`JobSummary`/`JobDetail`（`job_dao_interface.h` L15-50）新增可选字段，
  并同步更新三处 `SELECT`（`job_dao.cpp` L352-358、L416-422、L555-566）、
  `MakeJobSummary` 的 16 列硬断言（L244-248）以及 `FindActiveJobById` 的 19 列断言
  与 `description`/`source_url`/`status` 下标（L575-599）。
- 响应：只在 `SummaryToJson`（`job_controller.cpp` L505-537）**新增键**，
  搜索（L617-634）与详情（L667-696）同时生效；既有键不改。
- 适配器产出：`normalizer.build_job()`（`normalizer.py` L153-203）新增同名可选参数，
  未知即省略；`normalize_cn.py` 只做确定性映射，不做猜测。
- **不在本阶段**：新字段的搜索/排名消费。`JobSearchFilter`
  （`job_search_filter.h` L9-23）不新增新字段过滤条件，`docs/04` §5.1/§5.3 的
  请求参数与 `rule_v1` 权重保持不变；硬过滤与关键词过筛属后续匹配阶段（§15.3）。

### 5.2 Source Adapter 契约（Python 侧）

沿用 0012 的 provider 模块契约（`PROVIDER` / `board_url(board)` /
`parse_board(payload, *, board, company=None)`，见
`source_adapters/jobpilot_sources/providers/__init__.py` L1-5），并补上：

```text
每次 sync 必须先访问 robots.txt，并对“实际读取的列表/详情路径”给出判定：
  允许 -> 继续；禁止 -> 放弃该来源（记一条明确日志，不重试、不降级）
Crawl-delay 作为站内间隔下限；未声明时默认 150-250ms（ADR-0011 §3）
请求超时默认 10s；User-Agent 自报工具名与项目地址，绝不伪装完整浏览器
有界重试：仅 429 / 5xx / 传输错误；指数退避 + 抖动；
  默认 {retries: 2, baseDelayMs: 500, maxDelayMs: 8000}（ADR-0011 §3）；
  Retry-After 采纳但截断（上限 maxDelayMs=8000）
  非 429 的 4xx、被拒重定向：不重试
页数硬上限（默认 20 页/来源）+ 触顶告警；触顶即 complete=false（§5.3）
所有出站 URL 过 SSRF 校验（拒绝回环、私网、link-local、云元数据地址），
  且校验发生在每一个跳转后的 URL 上；host 必须解析为 IP 后对**解析结果**复查
  私网/回环/元数据网段（防 DNS rebinding），复查失败即不发起连接
每次响应体大小有显式上限，单次 sync 有总请求量上限（robots + 列表 + 详情合计）；
  两者都是显式常量 + 触顶告警，触顶即 `complete=false`，绝不无界读取或续跑
解析防御式：字段缺失即省略/跳过并计数告警，绝不编造；共享 HTML 实体解码器
每来源一份离线 fixture + 契约测试；真实网络只用于 opt-in smoke
```

### 5.3 上报契约（复用，不重定义）

- 自动采集只走 `POST /internal/jobs/source-sync`（R1）。批内每个 job 的 `source`
  必须等于观测的 `source`（`source_observation.cpp` L175-180）。
- **完整性口径**：只有能证明「本 scope 全量已读」的适配器才允许 `complete=true`；
  否则一律 `complete=false`。≤200 条且证明完整 → 单批发 `true`；>200 条或分页
  触顶 → 分批 `false`（沿用 R15 的 publisher 行为）。
- **`complete=true` 需要正面证据，不是默认值**（ADR-0010 §2 L85-96）；
  以下四种情况**禁止** `complete=true`：
  1. 解析到 `0` 条但没有来源提供的正向「空 board」信号——空结果不等于空 board；
  2. 解析形状变化 / 关键容器缺失 / 契约测试失配——视为解析失败；
  3. 任一请求失败（超时、DNS、非 2xx、重试耗尽）、分页未走完、页数或体积/请求
     预算触顶——只允许 `complete=false`（或本轮不发观测）；
  4. robots 禁止实际读取的列表/详情路径——放弃该来源：不发观测、不重试、不降级。
- 人工采集只走 `POST /internal/jobs/import`，`source="manual"`（R8 允许值之一），
  批次 `1..100`（R2）。该端点没有 `complete` 语义，因此**人工采集永远不会驱动
  freshness 生命周期**（`docs/04` §5.4 L435-437）；`JobIngestionDao::UpsertJob`
  也从不写 `last_seen_at`/`source_status`/`missing_count`（`job_ingestion_dao.cpp`
  L44-68），`CloseUnseenForScope` 另有 `complete` 与 `(source, source_scope)` 双重
  约束（`source_sync_dao.cpp` L159-163、L204）。
- **共享幂等键的交叉影响（F14 已决定，见 §18.2）**：两条路径都按全局 `source_url`
  upsert。决定是**人工记录胜出**：观测路径命中 `source='manual'` 行时整条跳过
  （§5.1 末、`docs/04` §5.5），人工行因此不进入来源生命周期、不会被
  `CloseUnseenForScope` 关闭；反向人工导入覆盖观测行保持既有语义。本阶段仍是
  单一 `source_url` 键，**不新增**复合键。`source_adapters/README.md` 的行为说明由
  任务 B（Python 侧）补齐，C++ 侧的回归断言在本步落地。
- 新增 China 来源标识需要扩展 `IsSupportedObservationSource`
  （`source_observation.cpp` L297-303）。标识长度 ≤32 字节（同文件 L15）。
  **建议**：步 1 只加入实现期已验证的来源标识（例如 `moka`、`beisen`、
  `campus_web`），不预先扩大 allowlist。
- **allowlist 是四处，必须同时登记同一标识**：C++ `IsSupportedObservationSource`
  （`source_observation.cpp` L297-303）、Python 配置白名单
  `config.py` L15 `SUPPORTED_PROVIDERS`（L68-72 校验）、provider 注册表
  `providers/__init__.py` L11-17 `PROVIDERS`、以及 `cli.py` 的
  `--provider` choices（L156-161，取自同一 `SUPPORTED_PROVIDERS`）。
  只改 C++ 会让 `python -m jobpilot_sources sync` 在配置阶段就拒绝该来源；
  漏掉 `cli.py` 则只有 `--provider` 过滤路径失效。
- `source_scope` 由适配器稳定产生（同一 board 每次 run 取值相同），
  上限 128 字节（`source_observation.cpp` L16），且必须与 `source` 一起定位观测；
  人工采集不产生 `source_scope`（`/internal/jobs/import` 无该语义，`docs/04` L435-437）。
- **`source_url` 是全局唯一幂等键**（`database/schema/03_job.sql` L25；
  `docs/03` §3 L127），跨来源也唯一，本阶段不引入 `(source, source_url)` 复合键。
  长度上限 255 字节（`source_observation.cpp` L22、`job_ingestion_service.cpp` L26）；
  超长必须**报错退出**（C++ `kInvalidParam`/`400`，Python 侧在打包前拒绝），
  绝不截断、绝不改名重试。

### 5.4 需要同步修改的契约文档

| 文档 | 章节 | 改动 |
|---|---|---|
| `docs/03-database-design.md` | §3 DDL、§5 索引表、**新增 §9 0014 增量契约**（含 §9.1 deadline 四端权威约定与既有库 ALTER） | §5.1 的 6 列（含派生 `deadline_date_only`）+ 0 索引；`jobs.source` 的 `COMMENT 'manual/import/crawler'` 在新增 China 来源后已不完整，同步改为与新 `source` 取值一致（schema 与 `docs/03` 的 DDL 两处同改） |
| `docs/04-api-design.md` | §5.1/§5.2 响应新增键、**§5.4/§5.5 请求体新增键**、§5.4 人工采集说明、§5.5 `source` 取值 | 新字段按「新增键、不改既有键」发布（§5.1 落点表）；`source_url` 的 255 字节上限与「超长报错不截断」写明；`source` 取值补充 |
| `database/schema/03_job.sql` | `jobs` DDL | 与 `docs/03` 一致 |
| `source_adapters/README.md` | 布局/配置/来源表/纪律节 | China 适配器、robots 与限流纪律、人工采集 |
| `docs/ai/project-state.md` | Current Stage / Next Entry / Risks | 0014 状态、来源清单、未决项 |
| `AGENTS.md` | 「Current phase」段落 | 指向 active 0014 |
| `docs/adr/README.md` | ADR 索引表 | 补登 ADR-0011（当前索引只到 0010，而本计划把 0011 列为已接受的依赖；索引缺项按 `docs/adr/README.md` L28-33 的新增 ADR 流程第 4 步补齐） |

`docs/03`/`docs/04` 属锁定契约：按 `AGENTS.md` L76-78 与
`docs/ai/agent-workflow.md` §5，**先改契约文档并取得 review，再让代码依赖它**。

## 6. Selected Design

### 6.1 通道划分（三态边界）

```text
(a) 自动公开采集（适配器，无凭据）
      robots 允许 -> 公开页面/公开接口 -> normalize -> dedup -> source-sync
      只读 GET；绝不 POST/PUT 到来源站点；绝不跟随“投递”类链接

(b) 人工采集（用户自己已登录的会话里操作）
      书签小工具：只读当前 DOM -> 规范化 JSON -> 剪贴板/本地文件
        （书签内不含任何 token；不向任何服务器发请求）
      本机 publisher：读文件 -> /internal/jobs/import (source="manual")
      → 这是覆盖登录墙平台的唯一正规途径

(c) 禁止
      自动登录 / 绕 CAPTCHA / 伪造 cookie、UA、Referer
      批量抓取登录墙平台 / 代理池 / 付费解锁
      自动投递、自动提交任何表单
      从岗位正文里再抓取链接（untrusted input，ADR-0011 §3）
```

### 6.2 Python 侧的模块切分（对既有包做加法）

```text
source_adapters/jobpilot_sources/
  client.py        # 扩展：robots 缓存、间隔、重试/退避、单位校验、SSRF 守卫
  fetch.py         # 新增：URL 规范化 + 出站 URL 安全校验 + 页数上限编排
  decoding.py      # 新增：charset 判定（header/meta/BOM）+ 实体解码
  htmljob.py       # 新增：JSON-LD / 内嵌 JSON / 极简 DOM 提取 + 链接解析
  feeds.py         # 新增：RSS/Atom/sitemap 解析（stdlib xml.etree）
  timestamps.py    # 新增：中文日期/相对时间/多格式 -> UTC "YYYY-MM-DD HH:MM:SS"
  salary.py        # 新增：K/万/元/日薪/面议 -> 月薪千元整数 | None
  normalize_cn.py  # 新增：学历/经验/性质/城市/截止时间 的确定性映射
  providers/       # 新增 China provider 模块（同一 parse_board 契约）
  capture/         # 新增：本机 publisher + 书签小工具产物解析（走 /internal/jobs/import）
  tests/           # 新增：每来源 fixture + 契约测试
```

### 6.3 关键决策与取舍

1. **人工采集不新开 HTTP 端点**。书签小工具不发网络请求，只做 DOM→剪贴板；
   本机 publisher 复用已存在且已测试的 `/internal/jobs/import`。理由：不扩大内部
   信任边界、不需要把 `INTERNAL_API_TOKEN` 交给浏览器（书签运行在页面上下文，
   任何内嵌 token 都会暴露给站点）、不引入新的鉴权面。
2. **`complete` 只在能证明全量时置真**。国内多数来源无法证明 → 这些来源的岗位
   `last_seen_at` 会随观测刷新，但**永不因缺失自动关闭**；这是刻意的正确性取舍
   （ADR-0010 §3「不确定的岗位只值得以后再查一次」）。需要关闭时由人工在后续阶段
   决定（0014 不提供自动关闭）。
3. **薪资宁可省略也不换算**。`面议`、`300/天`（实习）、`30-50万/年`（月份不明）
   一律不写薪资列；仅 `15-25K`、`8k-12k`、`1.5-2.5万/月` 这类可确定到
   「月薪千元」的形态才发布。
4. **不新增城市/公司规模/福利列**（§5.1 取舍表），把 schema 增量压到 5 列，
   避免同时改检索、排名与公开响应。
5. **来源标识分阶段放开**。Step 1 只改 allowlist 到实现期已实测的来源；未实测
   来源不得先加。
6. **每来源独立 fixture 与契约测试**，真实网络仅用于 opt-in smoke（与 0012 的
   `--dry-run` 先例一致，R14）。

### 6.4 拒绝的替代方案

| 替代方案 | 拒绝理由 |
|---|---|
| 书签小工具直接 POST `/internal/jobs/import` | 必须把内部 token 交给页面上下文；浏览器会把它暴露给站点与历史记录 |
| 新开 `/api/v1/capture`（用户 token 鉴权的公开导入端点） | 扩大公开攻击面与新契约面；本轮用「本机文件 + 既有内部端点」已足够 |
| 适配器直接写 MySQL | 违反 ADR-0008 与既有 guard 测试（R13） |
| Playwright/Chromium 作为核心依赖做渲染 | 重量级依赖、与「stdlib 优先、可离线」冲突；ADR-0011 只允许独立工具形态 |
| 通用 HTML 爬虫 + 自动发现 | ADR-0008 已拒绝；合规模糊、维护成本高 |
| 用 AI 判断岗位是否关闭 / 判断来源可用性 | ADR-0010 §6：启发式不得成为生命周期事实；可用性判定必须是确定性规则 + 日志 |
| 引入 jieba 等分词库 | ADR-0011 rejected alternatives |
| 复用海外 provider 的 `parse_board` 签名直传 HTML | 会让 `client.get_json` 隐式承担 HTML/RSS；解析职责应显式分层 |
| 一次性把 ADR-0011 §4 的 8 个字段全部落库 | 迁移与公开响应面同时膨胀；本轮只做有匹配价值的最小集 |

## 7. Invariants

1. **单次缺失不关闭岗位**。关闭只发生在 `complete=true` 且
   `missing_count >= 2` 或超过宽限（R3、R4）。
2. **AI 永不决定生命周期事实**。AI 不得写 `closed_at`/`status`/`missing_count`
   （ADR-0010 §6；0013 的决定也不变）。本阶段 AI 完全不参与来源链路。
3. **适配器不触达业务数据库**。无 DB 驱动、无 SQL（R13 的 guard 测试必须继续通过）。
4. **只读公开来源**。适配器对来源站点只发 GET；不提交表单；不投递。
5. **登录墙来源不自动化**。O1/O2/O3 只允许人工采集；任何绕过尝试视为阶段失败。
6. **诚实性**：字段不可确定即省略；完整性无法证明即 `complete=false`；
   `Retry-After` 不得拖死运行；日志不得打印 token。
7. **`source_url UNIQUE` 仍是唯一幂等键**；本阶段不引入 canonical 多源身份。
8. **既有契约不变**：`docs/04` §5.1/§5.2 既有键的含义、类型与可见性规则不变；
   `rule_v1` 排名公式不变；0012/0013 的端点语义不变。
9. **隐私**：人工采集的产物里不得写用户凭据；本机 publisher 只读本地文件，
   只发内部端点。
10. **人工记录胜出（F14，已决定）**：人工行即使在 `source_url` 上与来源观测冲突，
   也不被观测覆盖、不被关闭；关闭只发生在 `complete=true` 的观测里，且该行属于
   对应 `(source, source_scope)`（R3、R4、§18.2）。
11. **截止时间只用北京时间（F13，已决定）**：`deadline` 按 `Asia/Shanghai`
   （固定 UTC+8）解释，绝不使用服务器本地时区；date-only 以北京正午的 UTC 瞬时
   存储且不是到期时刻；显式时刻保留真实瞬时；类型未知保持 `NULL` 不臆造；
   `deadline` 不参与生命周期（不自动关闭）。

## 8. Testing Plan

原则（`docs/ai/agent-workflow.md` §2、`docs/quality/README.md` L20-52）：每步跑**能证伪该步变更的最小集合**，
Stage close 跑一次 L4。**本阶段只规划，不执行任何测试。**

| 步骤 | 级别 | 计划执行的验证 | 通过证据形态 |
|---|---|---|---|
| Step 1 C++ 边界 | L1/L2 | `test_source_observation`（allowlist 正/反例）、`test_job_ingestion_service`（`source="manual"` 通过、未知 source 拒绝）、`test_source_sync_http` | 二进制的 `N/N` 通过行 |
| Step 2 来源清单与 fixtures | L1 | 新增的 robots/来源清单单元测试；fixture 解析断言 | 同一行的 `Ran N tests ... OK` |
| Step 3 Python 框架 | L1/L2 | `python3 -m unittest discover -s jobpilot_sources/tests -t .`（含 robots **逐路径**判定：允许列表路径但禁止详情路径；robots 禁止→放弃来源且不重试；Crawl-delay 下限生效；429 重试后成功；非 429 4xx 不重试；5xx 退避上限；`Retry-After: 86400` 被截断；超时；页数上限→`complete=false`；解析到 0 条但无空 board 证据→**禁止** `complete=true`；形状失配→`complete=false`；私网/link-local/metadata URL 被拒；**重定向目标为私网被拒**；host 解析到私网被拒；响应体超限失败；总请求预算触顶→`complete=false`；charset 优先级 header/meta/BOM + GBK/GB18030 解码；缺失字段不编造） | 套件总 `OK` 行 + 新增用例名 |
| Step 4 新字段 | L2 | `test_schema`（新列/索引存在、可空与默认值）；**新字段 round-trip**：`/internal/jobs/import` 与 `/internal/jobs/source-sync` 两条路径写入后经 `JobDao::SearchJobs`/`FindActiveJobById` 与 HTTP §5.1/§5.2 响应读回；未知值省略、`min>max`/非法枚举/负年限/非法 `deadline` 返回 `400`；观测路径“本次缺 `deadline`”不抹除既有值；`ctest -L job` | `NN% tests passed` 行 |
| Step 5 China 适配器 | L2 | 每来源 fixture 契约测试 + opt-in 真机 `--dry-run` smoke（非 CI，人工执行并记录） | 套件 `OK` + smoke 的原始终端输出 |
| Step 6 端到端 | L2/L3 | 新增 adapter→source-sync→搜索 集成测试（fixture 驱动，不联网）；人工采集→`/internal/jobs/import`→搜索 集成测试 | 测试名 + 通过行 |
| Step 7 文档 | L0 | `git diff --check`；文档中的字段/端点与实际代码逐条对照 | 无空白错误 + 对照表 |
| Step 8 L4（stage close，一次） | L4 | Debug / ASan / UBSan 全量 CTest；`service/` 与 root Release 构建；Python 两套（`source_adapters`、`ai_worker`） | 每个 preset 的 `NN% tests passed`、退出码、commit SHA |

补充规则：

- L4 前置条件与排他性照 `docs/ai/agent-workflow.md` §2.2/§2.3；
  L4 期间无人可改被验证的代码。
- **不重跑已绿的套件**；引用命令 + 通过行 + commit（§2 规则 1）。
- 真机 robots/smoke 属 opt-in，**不作为 CI 门禁**；但 Step 5 的验收要求把「某来源
  robots 判定 + 一次真实响应形状」记录进 `source_adapters/README.md` 或计划进度节。
- 若某来源实测不合格（robots 禁止 / 需登录 / CAPTCHA），按 §12 停并报告，
  **不得**为了让测试变绿而放宽纪律。
- `test_schema`（`backend/tests/test_schema.cpp` L276-308、L390-460 为先例）只证明
  §3 DDL 建出的库形状；**既有库升级路径**要按 `docs/03` 新 §9 的 ALTER 在一个
  pre-0014 库上实跑一次并复验同一套断言，作为 Step 4 的附加证据记录命令与结果
  （一次性证据，不是 CI 门禁）。

## 9. Phased Implementation Steps

> 每一步都要求：改动前改契约（若涉及 `docs/03`/`docs/04`）、改动后跑上表对应级别、
> 在 §13 进度节记录证据（命令 + 通过行 + commit）。

**Step 1 — C++ 来源边界（最小、可测）**
- 内容：为「未知来源被拒」与「同一 `source` 语义」补回归断言；
  同步更新 allowlist 负例断言——`test_source_observation.cpp` L90-100
  `SourceObservationSources.AcceptsOnlyConfiguredProviders` 与 L116-127
  `RejectsUnsupportedOrEmptySource` 目前把 `manual/import/crawler/workday`
  写死为拒绝名单，allowlist 一扩就必须逐项修订，避免放行任意字符串。
  **China 来源标识的登记（`IsSupportedObservationSource` 扩展）不在本步执行**：
  它必须等 Step 2 的逐来源实测结论，否则会把未验证来源提前放行（§9 Step 3/Step 5）。
- 依赖：无（0012/0013 已完成）。
- 验证：L1/L2（见 §8 Step 1 行）。
- 风险：allowlist 扩大后，负例测试需同步更新，避免放行任意字符串。

**Step 2 — 来源编目与离线 fixtures（research→fixture）**
- 内容：逐来源实测 `robots.txt`（记录路径级判定）、响应形态、分页参数、编码、
  时间/薪资格式；把真实响应**脱敏后**存为 fixture；产出「来源登记表」
  （含逐路径 robots 判定、判定日期、OUT 名单与理由），落点为
  `source_adapters/README.md`（Step 7 只做最终措辞与格式整理，不新增事实）。
- 依赖：无（本步与 Step 1 互不阻塞；Step 3/Step 5 依赖本步的结论）。
- 验证：L1（fixture 解析 + robots 判定单元测试）；**robots/响应形状实测为 opt-in
  真机验证**（§8 补充规则），不作为 CI 门禁，但其原始输出必须记入计划进度节。
- 风险：**无网络则本步不能完成**（§12 停）；来源改版使 fixture 过期。

**Step 3 — Python 采集框架（robots/间隔/重试/编码/解析/SSRF）**
- 内容：§6.2 的 `fetch/decoding/htmljob/feeds/timestamps/salary` 与 `client.py`
  扩展；URL 规范化；页数上限、响应体/总请求预算与 `complete=false` 编排；
  来源登记（`config.py` L15 与 `providers/__init__.py` L11-17）与
  **`cli.py` 的 `--provider` choices 同步**（`cli.py` L156-161 取自
  `SUPPORTED_PROVIDERS`，只改 `providers/__init__.py` 会让 `--provider <china>`
  在参数解析阶段就被拒绝）；
  `cli.py` 的按来源取数路径改造（现为 `http.get_json(module.board_url(...))`，
  `cli.py` L57；HTML/feed 来源必须走新的分层 fetch，不得让 `get_json` 承担解析）；
  **新增 `capture/` 子包时同步 `pyproject.toml` 的 `[tool.setuptools] packages`
  列表**（当前显式列出 `jobpilot_sources` 与 `jobpilot_sources.providers`，
  L17-18；未登记的子包不会被安装，测试从源码跑不受影响，但安装态会缺模块）；
  每来源 fixture 契约测试。
  **C++ allowlist 扩展（`IsSupportedObservationSource`）与本步同时进行**，
  只登记 Step 2 已验证并记录在来源登记表中的标识。
- 依赖：Step 2（来源实测结论）。
- 验证：L1/L2（§8 Step 3 行）。
- 风险：重试与间隔实现错误会放大对来源的压力；退避上限与抖动需断言。

**Step 4 — 新字段端到端（schema→DTO→validate→DAO→public）**
- 内容：§5.1 的 6 列（含派生 `deadline_date_only`）+ 0 索引；`docs/03` 新契约节
  与既有库 ALTER；`docs/04` §5.4/§5.5 请求新增键；**两个 JSON 解析函数**（`ParseJob`、
  `ParseObservationJob`）补可选键 → `JobImportData`/observation DTO 与两条校验路径
  （`ValidateObservationJob`、`ValidateJob`）补域校验；**两处 upsert**
  （`job_ingestion_dao.cpp`、`source_sync_dao.cpp`）写新列并按 §5.1 处理省略语义；
  三处 `SELECT` 与 `MakeJobSummary`/`FindActiveJobById` 列断言更新；
  `normalizer.build_job()` 产出新键；公共搜索与详情的**新增键**输出。
  `deadline` 的写入/读回验收按 F13（§18.1）**已解除 Pending**，含 date-only /
  explicit-time / NULL 三态。
  明确不做：`JobSearchFilter` 与 `docs/04` §5.1/§5.3 的新过滤参数（§15.3）。
- 依赖：Step 1（负例断言已稳定）+ 本步自身的契约文档先行
  （`docs/03`/`docs/04` 先改并经 review，再让 C++ 依赖它——`docs/ai/agent-workflow.md` §5）；
  不依赖来源是否已实测（Step 2/3 可并行）。
- 验证：L2（`test_schema` + 新字段 round-trip + `ctest -L job`）。
- 风险：`docs/04` 请求/响应键增加需与契约文档同步；枚举值定义漂移；
  只在一条路径写新列会留下不可见的半实现。

**Step 5 — China 适配器（1-2 个真实公开来源）**
- 内容：S1/S2 中各实现 1 个（不足则实现 1 个 S1 + 1 个 S3）；含速率、页数上限、
  `complete` 诚实判定、无薪资/无字段时的省略路径；
  `deadline` 按 F13（§18.1）产出：date-only 用北京正午 → UTC 并置
  `deadline_date_only=true`，显式时刻保留真实瞬时并按其来源时区换算
  （**Pending 已解除**）。
- 依赖：Steps 2-4（C++ allowlist 已在 Step 3 登记）。
- 验证：L2 + opt-in smoke（§8 Step 5 行）。
- 风险：反爬/改版；`complete=false` 导致岗位长期不关闭（可接受，写入文档）。

**Step 6 — 人工采集通道 + 集成**
- 内容：书签小工具（只读 DOM→剪贴板/文件）、本机 publisher（走
  `/internal/jobs/import`，`source="manual"`）、粘贴 JD 文本路径；端到端集成测试。
  两条 E2E 都必须断言 §5.1 新字段写入后能经公共搜索/详情读回，
  并断言人工路径不触碰 freshness 字段、不与 source-sync 共享 `complete` 语义。
  **小工具无法进 CI**：验收以人工执行 + 记录为证——在一个真实登录墙页面上运行
  书签小工具，把产出的 JSON 原样贴入计划进度节（脱敏），再用本机 publisher
  走 `/internal/jobs/import` 并确认岗位出现在公共搜索里。
  `deadline` 断言按 F13（§18.1）**已解除 Pending**：断言 date-only 与
  explicit-time 两种形态写入后经公共搜索/详情读回（date-only 渲染为裸日期）。
- 依赖：Steps 1-5。
- 验证：L2/L3（§8 Step 6 行）。
- 风险：小工具与站点 DOM 耦合；用户粘贴内容含未预期字段时的防御式处理；
  与观测来源命中同一 `source_url` 时的生命周期归属（F14 已决定：人工胜出，§18.2）。

**Step 7 — 契约与文档**
- 内容：§5.4 的七处文档更新（含 `docs/04` §5.4/§5.5 的请求新增键、
  `docs/adr/README.md` 的 ADR-0011 索引补登）；
  `docs/ai/project-state.md`（当前阶段、`Next Entry`，以及仍写「active plan is
  still 0012」的 L12 与「reserved for 0014」的 L110 两处过期表述）与 `AGENTS.md`
  的当前阶段；`source_adapters/README.md` 的纪律、来源登记表（含判定日期）与
  人工采集说明。
- 依赖：Steps 1-6。
- 验证：L0（`git diff --check` + 逐条对照）。
- 风险：文档描述未实测行为（禁止：不确定处必须标「待实现期验证」）。

**Step 8 — L4 验证（一次，冻结 commit）**
- 内容：§8 Step 8 行的完整矩阵；`git diff --check`；最终 diff 审阅（范围蔓延/回归/缺测试）。
- 依赖：Steps 1-7。
- 验证：L4。
- 风险：冷启动 sanitizer 构建耗时长（不得误判为 hang）。

## 10. Verification and Rollback

- 每步自带聚焦验证（§8）；Stage close 跑一次 L4（`docs/ai/agent-workflow.md` §2）。
- 证据记录在 `docs/quality/README.md` 的 Recorded Baseline 与 §13 进度节：
  命令、通过行、commit SHA；四种结论（product failure / harness error /
  not verified / cannot verify）必须显式区分（§4）。
- **回滚边界**：
  - 迁移是纯增量（6 列 + 0 索引，全部可空/`NULL` 默认）→ 列可保留不删，旧代码继续可用。
  - Python 侧是**新增模块 + `client.py` 扩展**；回滚 = 移除 China provider 与
    `capture/`，`client.py` 的新行为由配置开关（默认关闭重试/间隔仅对声明来源生效）
    控制。
  - `source` allowlist 扩展可单独回滚（改回 3 个字符串即可），不影响既有数据。
  - 人工采集产物是本地文件，删除即回滚；不产生远端状态。
- 回滚**不**需要数据修复：没有破坏性 DDL、没有数据回填、没有 `source_url` 语义变化。

## 11. Risks

| 优先级 | 风险 | 边界/缓解 |
|---|---|---|
| High | 无网络 → 无法完成来源实测（robots/形状） | §12 停并报告；不猜来源规则，不先放宽 allowlist |
| High | 国内来源反爬/风控导致封禁或法律风险 | 只读、免登录、robots 逐路径、Crawl-delay 下限、页数上限；不合格即放弃来源 |
| High | 登录墙平台被“顺手”自动化 | §7 不变量 5；代码审查 + 来源清单 OUT 名单 + 无凭据设计 |
| High | `complete` 误用导致批量误关闭 | 默认不完整；只有能证明全量才 `true`；0012 已有 `complete=false` 不关闭的 DAO 保护（R4） |
| Medium | 新增列与 `docs/03`/`docs/04` 契约不同步 | 先改契约再改码（workflow §5）；Step 7 逐条对照 |
| Medium | 薪资/时间归一化错误产生假数据 | 不确定即省略；纯函数 + 表驱动用例；`面议`/日薪/年包不发布 |
| Medium | `source_url` 规范化把不同岗位合并 | 只做保守规范化（去 tracking、统一 scheme/host 大小写、去末尾斜杠），不做模糊匹配 |
| Medium | fixture 过期导致静默失效 | 契约测试断言形状；opt-in smoke 记录时间；形状变化→`complete=false` |
| Medium | 人工采集的小工具随站点改版失效 | 小工具是独立产物（不在核心链路）；解析失败只报错不清空用户输入 |
| Medium | 校招来源多、维护成本线性增长 | 本轮只做 1-2 个样板 + 接入模板；其余按来源登记表逐个接 |
| Low | 适配器套件运行时间长 | fixture 驱动、离线；联机只做 opt-in |
| Low | `deadline` 被误当作关闭依据 | §7 不变量 2：只有 `complete` 观测缺失才能影响生命周期 |

## 12. Stop Conditions

Stop and report to the Lead (do not widen scope) if:

1. 无网络/无法访问候选来源 → 来源实测无法完成（Step 2 不可完成）。
2. 目标来源的 `robots.txt` 禁止实际读取的列表路径 → 放弃该来源（ADR-0011 §3），
   若所有候选都被禁止，则本阶段退化为「人工采集通道 + 框架 + 文档」。
3. 某来源需要登录、验证码或风控绕过才能读取 → 该来源移入 OUT，只走人工采集。
4. **契约冲突**（见 §15.1）：阶段编号 0014 已被 0013 文档预留给 resume tailoring。
5. 某个必需字段无法在 `jobs` 现有列 + §5.1 增量内表达，需要更大的 schema 重构。
6. 需要 canonical 多源身份/alias 合并才能继续。
7. 有人要求把 AI 引入生命周期判定或自动投递。
8. 需要新增 `docs/03`/`docs/04` 之外的公开端点来完成人工采集。

## 13. Progress Checkboxes

- [x] Research（读取 AGENTS/ai 路由、ADR-0008/0009/0010/0011、0012/0013 计划、
      C++ 观测与导入边界、`docs/03` §7、`docs/04` §5.1/5.4/5.5、Python 适配器包）
- [x] Capability Check（§4.2/§4.3 缺口表；确认无 robots/间隔/重试/HTML 解析）
- [x] Design（§6 选定设计与拒绝替代）
- [x] ExecPlan（本文件）
- [x] Step 1 C++ 来源边界（task-9：allowlist 未预扩大 + 同源语义回归断言）
- [ ] Step 2 来源编目与离线 fixtures — task-10 已交付来源登记表、逐路径 robots
      判定与离线 fixture，但**没有一个可用的已测形状来源**（SPA/302 循环/DNS/证书），
      故未完成
- [ ] Step 3 Python 采集框架 — task-10 已交付 robots/重试/编码/解析/SSRF/预算/
      页数上限/salary/F13 与 220 项测试；**来源登记（四处 allowlist）未做**，
      故未勾选
- [x] Step 4 新字段端到端（task-9：schema/DTO/解析/校验/upsert/读回/响应；
      含既有库 ALTER 实跑证据）
- [ ] Step 5 China 适配器（1-2 个真实公开来源）— 仅框架与通用 provider 已落地
      （task-10）；task-12 对两个首选来源做了复验（国聘 list API 需客户端签名
      → OUT；清华的 list host robots 503 → fail-closed），**无合规契约**，按 §12
      stop condition 2 退化；适配器与四处 allowlist 均**未改**（见 §20）
- [x] Step 6 人工采集通道 + 集成（task-11：`capture/` 书签小工具 + 本机
      publisher；真机书签运行仍为 **UNVERIFIED**，见 §19.5）
- [x] Step 7 契约与文档（task-13：README/项目状态/本计划/agent-workflow 四份文档
      同步；Step 5 环境受限结论与证据位置见 §20.3/§20.4/§21.1）
- [x] Step 8 L4 验证（task-13：单次全量 L4 全绿，见 §21.2）

> 证据位置：Step 1/Step 4 见 §18.3；Step 2/Step 3 的框架与来源登记表见
> `source_adapters/README.md`（task-10 的 write scope 不含本计划文件）；
> Step 6 见 §19。本文件未提交，证据对应的代码基是 HEAD `bf5053f` + 工作树改动。

## 14. Research Evidence Log（本阶段实际执行过的只读命令）

> 本节是**规划阶段**的只读命令日志，按当时事实保留。实现阶段的证据不在这里：
> Step 1/4 见 §18.3，Step 6 见 §19.3，Step 5 来源复验见 §20.2，Step 7/8 见 §21。
> 下文「未执行」一句仅适用于规划阶段。

- `git status --short`（空）与 `git log --oneline -5` → HEAD `bf5053f`，工作树干净。
- `grep -rn "robots\|delay\|retry\|backoff" source_adapters/jobpilot_sources/*.py
  source_adapters/jobpilot_sources/providers/*.py` → 无匹配；
  `grep -c "time.sleep" source_adapters/jobpilot_sources/*.py` → 全为 0。
- 读取（节选）：`AGENTS.md`、`docs/ai/README.md`、`docs/ai/project-state.md`、
  `docs/ai/agent-workflow.md`、`docs/exec-plans/README.md`、
  `docs/adr/0008/0009/0010/0011`、`docs/03-database-design.md` §5-§7、
  `docs/04-api-design.md` §5.1-§5.5/§10、`docs/quality/README.md`、
  `docs/research/2026-10-08-open-source-job-search-agents.md`、
  0012/0013 completed plans、`database/schema/03_job.sql`、
  `service/include/jobpilot/service/job/*.h`、
  `service/src/job/{source_observation,source_sync_service,job_ingestion_service,internal_job_import_controller,job_search_filter}.cpp`、
  `service/src/router.cpp` L296-324、`source_adapters/**`。
- 未执行：任何测试套件、任何构建、任何数据库操作、任何网络请求（`git` 只读命令除外）。

## 15. Known Issues and Follow-ups

### 15.1 契约冲突（已上报 Lead，不在本阶段自行消解）

`docs/exec-plans/completed/0013-ai-matching.md`：

- §3 Out of Scope：「`resume_optimize` 和 `interview_gen` …**0014 owns resume
  tailoring**」；
- §14 Known Issues：「`interview_gen` / `resume_optimize` remain contract-only
  (**0014**)」；
- §17 与 `docs/research/2026-10-08-open-source-job-search-agents.md` §4
  「0014 (resume tailoring) — requirements recorded now」。

与之冲突的较新决定：

- `docs/adr/0011-china-first-sourcing-and-compliance.md` §4 Neutral：
  「新增『中国来源与采集』阶段需要 ExecPlan 编号（**拟 0014**，**简历定制顺延**）」；
- `docs/ai/project-state.md` §Next Entry 2 与 **Stage numbering 表**
  （工作树未提交的更新，L219-226）已记录：0014 = China campus sources and manual
  capture，**0015 = Resume tailoring**，0016 = open-source packaging，并注明
  「supersedes older references inside the archived completed plans」。

因此**编号决定已经落地**，本计划按 project-state 的 Stage numbering 执行：
0014 = China 校招来源 + 人工采集，resume tailoring 顺延到 0015。**仍待 Lead 决定
的只有一项**：是否在归档的 `0013-ai-matching.md` 文本里补一条 superseded 注记
（该文件属归档证据，本阶段不修改任何其他文件）。

### 15.2 其他已知问题（实现期处理）

- `docs/ai/project-state.md` 顶部仍写「The active plan is still 0012 Job Source
  Adapter + Freshness」，而已归档的 0013 已完成；Step 7 一并修正。
- `MissingPolicy` 目前只有全局默认（48h / 2 次），本阶段**不引入**来源级策略参数
  （§3.1）：新增 China 来源全部 `complete=false`，闭合路径不可达。若后续引入能证明
  完整覆盖的来源，再按 ADR-0010 §3 与 0012 §18 的分层（Service 持业务策略、DAO 只
  执行 SQL）实现，见 §15.3。
- `ghost_score` 在 `docs/04` §10.2 L818 已列出，但 0014 不实现；继续留作后续。
- 岗位正文中的链接一律不抓取（untrusted input）；如未来要抓取，必须单独评审 SSRF 面。
- 来源清单 OUT 名单需要长期维护（平台 ToS/robots 变化）；建议在
  `source_adapters/README.md` 维护一张「来源登记表（含判定日期）」。
- 日期格式解析与薪资换算的边界（闰年、跨年、`N天前`、`·14薪`）需要用表驱动用例
  覆盖；无法确定时一律省略而不是估算。

### 15.3 本阶段明确不做的后续项（不扩大业务范围）

以下都是 ADR-0011 提到、但 0014 只做「字段落库」不消费的能力，登记为后续阶段，
本阶段不得顺手扩大：

| 后续项 | 依据 | 归属 |
|---|---|---|
| `city`/`district`、`company_size`、`company_industry`、`benefits` 落库 | ADR-0011 §4 L71-73；0014 §5.1 取舍表 | 后续（匹配/画像深化阶段） |
| 关键词过筛、学历/年限/城市/薪资档硬过滤、应届身份校验 | ADR-0011 §4 L75-77 | 后续匹配阶段；0014 只保证新列可被读取 |
| 新字段进入 `GET /api/v1/jobs` / `/recommended` 请求参数与 `rule_v1` | 0011 §10.1；0014 §5.1 末 | 后续阶段，需单独评审 `docs/04` §5.1/§5.3 |
| 来源级 `MissingPolicy` | ADR-0010 §3 L118-120；0012 §18 L614-615 | 仅在引入能证明完整覆盖的来源时 |
| `deadline` 到期提示 / 自动关闭 | 0014 §5.1（`deadline` 只提示不关闭） | 后续；且需先解决 §15.4 的语义 |

### 15.4 待产品决策（不得由实现者代替决定）

**F13/F14 已由产品/Lead 决定（见 §18），不再是待决项。**
**Step 5 的真实来源结论不是产品决策，而是环境受限（§20.2/§20.4）：已如实记录，产品
负责人接受；不需要新的产品决定，也不据此放宽任何纪律。**

1. ~~`deadline` 的日期/时刻与时区~~ → **已决定（F13，§18.1）**：按 Beijing time
   （`Asia/Shanghai`）解释；date-only 用北京本地正午的 UTC 瞬时（`04:00:00`）
   存储且**不是**到期时刻；显式时刻保留真实瞬时；类型未知保持 `NULL` 不臆造。
   不再有 Pending 标记。
2. ~~同一 `source_url` 被人工采集与来源观测同时命中时的优先级~~ →
   **已决定（F14，§18.2）**：人工采集胜出；观测命中 `source='manual'` 行时整条跳过
   并计入 `skipped_manual`/`skipped_manual_urls`。
3. **是否给归档的 `0013-ai-matching.md` 补 superseded 注记**（§15.1）——**仍未决**
   （归档证据文本，本阶段不修改）；编号本身已由 `docs/ai/project-state.md` 的
   Stage numbering 表记录。
4. ~~学历枚举是否够用~~ → **本阶段已决定**：`education_required` 保持 `1..5`
   （`1不限/2大专/3本科/4硕士/5博士`），未枚举出的整数一律拒绝；来源无法确定即省略。
   是否新增 MBA/中专等档位**延后到后续阶段**，不阻塞 0014。

### 15.5 决策影响面（哪些步骤必须等，哪些可以并行推进）

| 未决项 | 受影响步骤 | 现在可以做 | 必须等待 |
|---|---|---|---|
| §15.4.1 `deadline` 语义（已决定 F13） | 无（Pending 已解除）：Step 4 的三态写入/读回、Step 5/Step 6 的 deadline 产出 | 全部（见 §18.1） | 无 |
| §15.4.2 `source_url` 路径优先级（已决定 F14） | Step 6 的冲突断言、`source_adapters/README.md` 的行为说明 | 全部（C++ 侧断言已落地，见 §18.2） | 无（README 行为说明属任务 B） |
| §15.4.3 归档 0013 补注记 | Step 7（文档） | 全部实现步骤；编号已由 project-state 的 Stage numbering 表落地 | 是否改写归档文本的最终答复 |
| §15.4.4 学历枚举扩展 | 后续阶段 | `1..5` 档位与校验（已落地） | 是否新增 MBA/中专等档位的产品决定 |
| Step 5 真实来源（**环境受限，非产品决策**） | Step 5 适配器与四处 allowlist 注册 | 全部其它步骤；框架/人工采集/文档已完成 | 出现可合规验证的来源（书面许可/公开 API；或 清华 list host robots 恢复） |

**执行结论**：F13 与 F14 已决定且落地；Step 1-4、6 完成，Step 7/8 由 task-13 完成
（§21）。**Step 5 环境受限**：两个首选来源都不存在可合规使用的列表契约（§20.2），
产品负责人已接受该结论（§20.4），因此**未编写国内来源适配器、未改四处 allowlist**
（仍为 `greenhouse/lever/ashby`）。本计划仍为 Active，唯一剩余的产品决策是
§15.4.3（归档 0013 注记）；Step 5 的重试条件记录在 §20.4。

## 16. Rejected Alternatives（汇总）

见 §6.4。核心三条：书签内嵌 token、另开公开导入端点、AI 参与生命周期判定。

## 17. Review Revisions（独立评审的 Class 1 修订记录）

独立评审（task-8，对照 HEAD `bf5053f` 的真实代码逐条核验）对本文做出的
**自可修复技术性修订**，每条附依据。除本节列出的内容外，评审未改动任何其他文件。

| # | 修订 | 依据（仓库为准） | 为何属 Class 1 |
|---|---|---|---|
| 1 | §5.1 补上两条路径的 JSON 解析函数 `ParseJob`（`internal_job_import_controller.cpp` L285-350）与 `ParseObservationJob`（`internal_source_sync_controller.cpp` L303-376）作为必需落点 | 这两个函数才是把请求 JSON 映射到 `JobImportData` 的地方；只改 DTO 与校验会让新键在 HTTP 层被静默忽略 | 实现落点可从事接口直接推出，漏掉即形成"写了但读不到"的假实现 |
| 2 | §5.1 修正 `IsValidDbTimestamp` 的行引用：定义在 `source_observation.cpp` L305 起，头文件 L80-83 只是声明 | `source_observation.h` L80-83 是声明注释；实现体在 cpp | 行引用错误（计划与仓库不一致时以仓库为准） |
| 3 | §9 Step 1/2/3 重排依赖：来源实测（Step 2）不再依赖 Step 1；China 标识的 allowlist 登记移到 Step 3 | 原文 Step 1「扩展到已实测来源」与 Step 2「实测」互为先后，且 Step 2 依赖 Step 1，构成环 | 内部矛盾 + 错误依赖，可直接消解 |
| 4 | §9 Step 1 点名将被改动的负例断言：`test_source_observation.cpp` L90-100 与 L116-127 把 `manual/import/crawler/workday` 写死为拒绝名单 | 实际测试在 `service/tests/test_source_observation.cpp` L95-99、L118-119 | 缺一个已知会被本步推翻的具体测试位点 |
| 5 | §5.1/§9 Step 4/5/6 + 新增 §15.5：把 §15.4 未决项对实现步骤的影响显式标注为 **Pending Product Decision** | 原文 §15.4 说"决定前不得落库"，但 Step 4 列了 `deadline` 列与写入、Step 5/6 会产出 `deadline`，未标注即自相矛盾 | 内部矛盾；只标注不替产品做决定 |
| 6 | §5.2 补齐 ADR-0011 §3 的重试默认值 `{retries: 2, baseDelayMs: 500, maxDelayMs: 8000}` | ADR-0011 §3 已给出该默认值，计划只抄了 `maxDelayMs` | 细节可从既有 ADR 直接补全，不留实现期自由裁量 |
| 7 | §5.4/§9 Step 7 补登 `docs/adr/README.md` 索引中的 ADR-0011 | `docs/adr/README.md` L7-18 的索引只到 0010，而本计划 §"Dependencies" 把 0011 列为已接受依赖 | 计划声明的文档同步清单漏了一个必须同步的记录文件 |
| 8 | §5.4 补 `jobs.source` 的 `COMMENT 'manual/import/crawler'` 同步更新 | `database/schema/03_job.sql` L12 与 `docs/03-database-design.md` 的 DDL 都把取值写死为三个；本阶段要放行 China 标识后该注释即失真 | 契约注释与新增行为不一致，属既有文档同步义务 |
| 9 | §1 完成判据写清两条通道的具体端点（`/internal/jobs/source-sync` 与书签小工具 + `/internal/jobs/import`） | 0012/0013 的既有端点，计划正文已确定 | 验收判据含糊，补全不改语义 |
| 10 | §9 Step 2 明确「来源登记表」的落点是 `source_adapters/README.md`；§9 Step 6 明确书签小工具的人工验收证据形态 | 原文只说"产出来源清单文档"，未给落点；Step 7 又要求更新 README 的来源登记表 | 消除同一交付物的落点歧义；指出无法进 CI 的验收方式 |
| 11 | §9 Step 3 补 `pyproject.toml` 的 `[tool.setuptools] packages` 显式列表 | `source_adapters/pyproject.toml` L17-18 是显式列举，新增子包不会自动安装 | 可从事接口直接推出的已知实现位点 |
| 12 | §9 Step 3 补 `cli.py --provider` choices 的同步（`cli.py` L156-161） | `build_parser` 用 `list(SUPPORTED_PROVIDERS)` 作为 choices；只登记 `config.py`/`providers` 会让 `--provider <china>` 在 argparse 阶段失败 | 三处登记改成四处，是同一条既有契约的具体实现位点 |

未修订、留给 Lead/产品决策或真机验证的内容见 §15.4、§15.5 与 §12。

## 18. F13/F14 决策与 task-9 实施记录

### 18.1 F13：`deadline` 的日期/时刻与时区（已决定）

- **权威时区**：Beijing time = `Asia/Shanghai`（固定 UTC+8，中国自 1991 年起无
  夏令时）。代码中保留该标识（`kDeadlineTimeZoneId`，`job_extra_fields.h`），
  实现为固定偏移算术，**完全不使用服务器/容器/MySQL 的本地时区**
  （无 `localtime`/`mktime`/`TZ` 依赖；已用 3 个非 UTC 时区实测）。
- **date-only**：来源只给日期 → 存「北京本地正午 12:00 → UTC」= `04:00:00`；
  `deadline_date_only = 1`。读回时 `+08:00` 还原北京日期 → 日期往返不漂移。
  正午是**存储约定，不是到期时刻**：`1` 时**必须**满足 `deadline` 的 UTC 时间部分
  为 `04:00:00`（校验不变量），API 对 `1` 输出**裸日期** `YYYY-MM-DD`。
- **explicit-time**：来源给了具体时刻 → 保留真实瞬时（按其来源时区换算为 UTC），
  `deadline_date_only = 0`；**绝不**改写为正午。C++ 只校验格式/范围，不重算时区。
- **kind 未知（不臆造精度）**：`deadline` 非空但 `deadline_date_only = NULL`
  （pre-0014 行或 SQL 直写）→ 保留 UTC 瞬时、API 输出北京本地
  `YYYY-MM-DD HH:MM:SS`、`deadline_date_only` 输出 `null`。**不补 0/1**：
  补 `0` 会把日历语义谎报成精确时刻，补 `1` 会掩盖真实时刻。
- **不新增自动关闭**：`deadline` 不参与可见性/排名/生命周期；过期岗位仍可见，
  该限制记录在 `docs/04` §5.1。
- **`deadline_date_only` 是派生的技术决定**（Lead 已批准，无需再走产品决策）：
  只存一个 UTC 瞬时无法同时满足「date-only 以北京正午按日期往返」与
  「正午不得被渲染成到期时刻」，也无法区分恰好落在 `04:00:00` UTC 的真实时刻。
- **唯一权威落点**：`docs/03` §9.1（覆盖 DB、Python 归一化器、C++ DTO、API 响应
  四端）。C++ 侧字段：`JobImportData::{deadline,deadlineDateOnly}`、
  `JobSummary::{deadline,deadlineDateOnly}`；两个 JSON 解析函数缺省
  `deadline_date_only=false`（有 `deadline` 且未给标志时）；带标志无 `deadline`
  视为非法请求 → `400`（**不**静默丢弃）。

### 18.2 F14：人工记录胜出（已决定）

- 观测路径命中 `source='manual'` 行时**整条跳过**：不更新任何列、不改 `source`、
  不动 `freshness`/`closed_at`/`missing_count`、不替换其 `job_requirements`。
- 实现点：`source_sync_dao.cpp` 的 `SkippedForManualRecord`
  （`SELECT id ... AND source='manual' LIMIT 1 FOR UPDATE`）+ `ApplyObservation`
  跳过计数；`JobUpsertStatus::kSkippedManual`；
  `SourceObservationApplyResult::{skippedManual,skippedManualUrls}`；
  `SourceSyncResult` 同步；响应新增 `skipped_manual` / `skipped_manual_urls`；
  服务端为每个跳过项写一条 `warn` 日志。
- 该行因此不属于观测的 `(source, source_scope)`，`CloseUnseenForScope`
  不会标记或关闭它（`WHERE source = ?` 天然排除 `manual`）。
- 反向（人工导入覆盖观测行）保持既有整组覆盖语义，是「人工胜出」的另一半。
- 回归断言：`test_source_sync_dao.cpp::ManualRecordIsUnchangedByACollidingObservation`
  （22 列 before/after 逐列比对）、`test_source_sync_http.cpp::ManualRecordWinsOverObservation`。
- `source_adapters/README.md` 的行为说明属任务 B（Python 侧），本步不写该文件。

### 18.3 Step 1 / Step 4 验证证据（task-9 实跑）

代码基：HEAD `bf5053f` + 本工作树改动（未提交，遵守 no-git-write 规则）。

已有库迁移证据（`docs/03` §9.4 的 ALTER 实跑，非仅文档）：

```text
$ docker exec -i jobpilot-mysql mysql -uroot -proot
  创建 jobpilot_pre0014（由 HEAD bf5053f 的 database/schema/*.sql 建库）
  jobs 新列数 = 0（期望 0）；jobs 列数 = 21
  ALTER 前插入 legacy 行 id=1
$ awk 提取 docs/03 §9.4 的 ALTER → /tmp/alter_0014.sql；在 jobpilot_pre0014 执行
  ALTER_EXIT=0（无错误）
  jobs 列数 21 → 27；6 个新列 is_nullable=YES / column_default=NULL
  legacy 行（ALTER 前插入）6 个新列全部 <NULL>（不臆造 deadline 类型）
$ test_schema（MYSQL_DATABASE=jobpilot_pre0014，迁移后的旧库）
  [==========] 11 tests from 1 test suite ran. (440 ms total)
  [  PASSED  ] 11 tests.
$ 另建 jobpilot_schema0014（由当前 DDL 新建）→ test_schema
  [  PASSED  ] 11 tests.
$ 同一 ALTER 已应用于开发库 jobpilot（列数 21 → 27），供后续测试使用
```

注意：迁移测试首次全红是**环境问题**（`jobpilot` 用户对新库无权限
`Access denied ... to database 'jobpilot_pre0014'`），GRANT 后 11/11 通过；
按 `docs/ai/agent-workflow.md` §3/§4 记为 harness error 而非产品失败。

C++ 验证（Debug preset，`build/presets/debug`）：

```text
$ ctest -L job
  100% tests passed, 0 tests failed out of 106
  Label Time Summary: job = 5.35 sec*proc (106 tests)
  CTEST_RC=0

$ ./test_deadline_time        [==========] 10 tests ... [  PASSED  ] 10 tests.
$ ./test_source_observation   [  PASSED  ] 29 tests.
$ ./test_job_ingestion_service[  PASSED  ] 4 tests.
$ ./test_job_dao              [  PASSED  ] 6 tests.（含 0014 三态读回）
$ ./test_source_sync_dao      [  PASSED  ] 14 tests.（含 F14 before/after 冲突）
$ ./test_source_sync_service  [  PASSED  ] 6 tests.
$ ./test_job_ingestion_http   [  PASSED  ] 5 tests.（含两条路径 round-trip）
$ ./test_source_sync_http     [  PASSED  ] 8 tests.（含 F14 + COALESCE）
$ ./test_job_http             [  PASSED  ] 8 tests.

$ TZ=Pacific/Kiritimati|America/New_York|Pacific/Pago_Pago
    test_deadline_time / test_job_dao / test_job_ingestion_http 全部 10/6/5 passed
  MySQL: @@global.time_zone=SYSTEM, NOW()=UTC_TIMESTAMP()（容器 UTC）
```

严格静态检查（`-Wall -Wextra -Werror -fsyntax-only`，复用 debug 的
`compile_commands.json` 编译参数）：22 个新增/修改的 .cpp/.h + `test_schema.cpp`
**全部 rc=0**（`FAILURES=0`）。其中 `job_controller.cpp::HttpStatusFor` 的
`-Wswitch` 是 **HEAD 既有**告警（无 default 分支，行为已由函数末尾
`return 500` 兜底），为满足严格检查补了行为等价的 `default: return 500;`。

### 18.4 本次未验证 / 不在 scope

- Python `normalizer.build_job()` 产出 `deadline`/`deadline_date_only` 与
  `source_adapters/README.md` 的行为说明：**任务 B（Python 侧）**，本步未做也未验证。
- `deadline` 的 China 来源实战产出（Step 5）与真机 smoke：未做（本阶段无网络步骤）。
- 无 L4（按 task-9 指示只跑受影响 L1/L2）；Stage close 的 L4 在 Step 8。
- `JobSearchFilter`/公开请求参数的新字段消费：明确不做（§15.3）。

## 19. Step 6 实施记录（task-11：人工采集通道）

### 19.1 交付物与职责

```text
source_adapters/jobpilot_sources/capture/
  bookmarklet.js      书签小工具（浏览器页面上下文）：只读 DOM -> JSON 剪贴板；
                      无凭据、无网络请求（静态测试强制）
  bookmarklet.py      打包：读 JS -> javascript: href（percent-encode）-> 帮助页
  artifact.py         不可信产物解析/校验（形状、类型、字节上限、版本、重复键）
  publisher.py        归一化（复用 adapters 的确定性模块）-> /internal/jobs/import
                      payload（source="manual"）-> POST -> 响应计数器校验
source_adapters/tools/verify_manual_capture_e2e.py
                      可选 E2E 验证脚本（需运行中的 service；本步已实跑，见 §19.3）
source_adapters/jobpilot_sources/cli.py
                      + `capture-bookmarklet`、`publish-capture` 子命令
source_adapters/pyproject.toml
                      + packages 增加 `jobpilot_sources.capture`，
                        package-data 收录 `*.js`
```

未新增 HTTP 端点、未新增 ingestion 路径、未改 C++ allowlist、未改 Step 5 的
真实来源适配器与 allowlist（`SUPPORTED_PROVIDERS`/`PROVIDERS` 仍是三个海外
provider）。

### 19.2 数据流与授权边界

```text
用户自己的登录会话中的浏览器页面
  bookmarklet.js：只读 DOM（title / meta / JSON-LD JobPosting / 有界可见文本）
    -> JSON capture -> 剪贴板或本地文件（不发网络请求、不含任何凭据）
本机 publisher（publish-capture）
  artifact.py  解析+校验不可信产物
  publisher.py 用与 adapters 相同的确定性模块归一化（fetch.normalize_url /
               normalize_cn / salary / timestamps(F13)）-> payload(source="manual")
  传输层：POST /internal/jobs/import，仅 header `X-Internal-Token`
     -> C++ InternalJobImportController（常量时间比对）
     -> JobIngestionService::Import（1..100，source ∈ manual/import/crawler）
     -> JobIngestionDao::UpsertJob（uk_source_url 幂等；从不写 freshness 列）
     -> MySQL jobs / job_requirements
读回：GET /api/v1/jobs?keyword=… 与 GET /api/v1/jobs/{id}
F14：同 source_url 的 source-sync 观测被跳过（skipped_manual/_urls）
```

**授权边界**：书签运行在不可信页面上下文，因此**不持有任何凭据**且**不发任何
网络请求**（测试强制：无 fetch/XHR/sendBeacon/WebSocket、无 `/internal/`、
无 token/authorization/bearer 等标记、无 cookie/storage 读取）。token 只存在于
本机 publisher 的进程环境里，按**变量名**读取（`--token-env`，默认
`INTERNAL_API_TOKEN`，或 `--config` 中的名字），仅作为 `X-Internal-Token` 头发送；
CLI **没有** `--token` 选项，`config.example.yaml` 只写变量名不写值，所有成功/
失败路径的 stdout/stderr 都不含 token（测试断言）。capture 产物是不可信输入，
最多只能通过既有端点写一行 job，而写它仍需从未离开本机的 token。

### 19.3 实跑证据（本机 service + MySQL）

```text
$ ./build/presets/debug/service/jobpilot_service --config=/tmp/e2e_0014.yaml  (port 18080)
$ E2E_TOKEN=... python3 source_adapters/tools/verify_manual_capture_e2e.py \
      --base-url http://127.0.0.1:18080 --token-env E2E_TOKEN
  [PASS] publish imported exactly one manual job — ok=True submitted=1 created=1
         updated=0 total=1 status=200
  [PASS] manual job is visible in public search
  [PASS] source stays manual / education_required round-trips / job_nature round-trips
  [PASS] date-only deadline renders as a Beijing date (2026-10-31)
  [PASS] deadline_date_only round-trips
  [PASS] manual import leaves freshness unobserved (last_seen_at null) / freshness null
  [PASS] source-sync responds 200 — skipped_manual=1, skipped_manual_urls=[<url>]
  [PASS] F14: the manual record is byte-identical before and after the sync
         (changed fields: <none>); source still manual; human title/description survived
  ALL CHECKS PASSED

$ publish-capture --dry-run --json   -> exit 0，单个 JSON（含 notes），无 token 泄漏
$ publish-capture --json             -> exit 0，{"ok":true,"submitted":1,"created":1,
                                        "updated":0,"total":1}，无 token 泄漏
$ 同一文件再次 publish               -> exit 0，created=0 updated=1 total=1
                                        （uk_source_url 幂等，MySQL 中仅 1 行，无重复）
$ 错误 token                         -> exit 1，{"ok":false,"stage":"publish",
                                        "error":"import API returned HTTP 401:
                                        code=20001 message=unauthorized"}，无泄漏
$ 缺少 token                         -> exit 2（请求前失败），无泄漏
$ MySQL 复核：source=manual、source_status=unknown、last_seen_at=NULL、
  missing_count=0、closed_at=NULL、0014 字段按 F13 存储
  （date-only deadline 存 04:00:00 + deadline_date_only=1；
    explicit time 存 10:30:00 + 0）
$ 清理：DELETE 本次创建的 3 行 E2E job（remaining=0），停止 service，删除临时目录
```

Python 测试（offline，无网络/无 DB）：

```text
$ cd source_adapters && python3 -m unittest discover -s jobpilot_sources/tests -t .
  Ran 319 tests in 0.376s
  OK
```

新增用例：`test_capture_artifact` 20、`test_capture_publisher` 30、
`test_capture_bookmarklet` 19（含 8 个用 `node` 真实执行 `bookmarklet.js` 的用例）、
`test_capture_f14` 8、`test_capture_cli` 22 = 99 个新用例；既有 220 个保持通过
（含隔离守卫：无 DB 驱动、无 SQL、仅 PyYAML）。

### 19.4 F14 / 重复导入 / 凭据保护证据小结

- **F14**：Python 侧断言两条通道使用同一个 `normalize_url` 幂等键、manual payload
  无法表达 freshness/lifecycle/source_scope 字段、capture 包不出现 source-sync
  端点；服务端跳过行为由上面的**真实 E2E**（byte-identical before/after）以及
  task-9 的 C++ 用例
  `SourceSyncDaoTest.ManualRecordIsUnchangedByACollidingObservation`、
  `SourceSyncHttpTest.ManualRecordWinsOverObservation` 证明。
- **重复导入**：同文件重复发布 => `updated=1`，MySQL 仍只有 1 行；同一批次内
  `source_url` 重复（含只差 fragment/跟踪参数、归一化后相同）在本地即报错，
  请求前退出 2；跨批次相同 key 走 upsert 幂等。
- **凭据保护**：见 §19.2；另有测试断言 CLI 无 `--token*` 值选项、
  `config.example.yaml` 只有环境变量名、bookmarklet 源与产物都不含凭据标记。

### 19.5 未验证项（不得声称已完成）

- **真机书签运行在真实登录墙页面上：UNVERIFIED**。无法自动完成（不允许也不能
  模拟登录）。需产品负责人/DRI 在自己浏览器中执行并把脱敏后的产物贴入本节；
  模板：
  ```text
  站点类别（不写具体平台）: ______   判定日期: ____-__-__
  书签点击结果（复制到剪贴板/回退 prompt）: ______
  脱敏后的 capture JSON: { "capture_version": 1, ... }
  经 publish-capture 发布的结果行: created=__ updated=__ total=__
  公共搜索可见性（GET /api/v1/jobs?keyword=…）: 可见 / 不可见 + 原始响应片段
  robots/登录墙判定: 未绕过登录、未提交表单（人工确认）
  ```
  在填入之前，本节结论为「书签提取已离线（node + fixture）验证；真机运行未验证」。
- 未跑 L4；未跑 C++ 全矩阵（按 task-11 指示）。E2E 只跑了上述一次 service 级
  人工通道验证（非 L4）。
- Step 5 真实来源适配器与四处 allowlist 注册仍未完成（见 §13 说明与 README）。

### 19.6 真机验收记录（2026-10-10，产品负责人执行）

> 本节为 **append-only 追加记录**（owner 已批准；§19.1-§19.5、§13、§20、§21 原文未改一字）。
> 标注【用户报告】的条目是产品负责人在自己浏览器中的观察，**不是**代码、日志或形式化安全证据。

- 【用户报告】**Edge** 书签已在**真实招聘岗位详情页**执行成功。
- 【用户报告】**实习僧首页**测试只验证了脚本执行与 JSON 生成，**不算有效的单岗位提取样本**。
- 【用户报告】**牛客网桌面版**岗位详情页成功生成 JSON，但**招聘企业字段被误识别为平台名称**，
  **描述混有推荐岗位内容**。
- 【用户报告】**牛客网移动版**岗位详情页成功生成 JSON，包含岗位职责与要求，但**公司字段缺失**。
- 【用户报告】**清空 Network 记录后直接点击书签，未观察到新增请求** —— 这只是**本次观察
  结果**，**不得**描述为形式化安全证明；「无网络请求、无凭据」不变量仍以代码与离线测试为准
  （`capture/bookmarklet.js`；`tests/test_capture_bookmarklet.py`）。
- `publish-capture`、后端导入与 MySQL 持久化**未在本次浏览器验收中执行**；因此 §19.5 模板中的
  `created/updated/total` 与公共搜索可见性两行**仍然空缺**。
- **Step 5 中国真实来源仍为 environment-limited / 未完成**（四处 allowlist 仍为
  `greenhouse`/`lever`/`ashby`；见 §20.3/§20.4）。

**结论**：书签的**提取**步骤已在真实登录墙页面上完成并产出 JSON；**发布与读回**尚未执行，
因此 §19.5 的整体状态仍为「未验证」，本节只记录该验证项已迈出的第一步。

## 20. Step 5 实施记录（task-12：首个真实来源适配器尝试 + robots 修订）

### 20.1 Part A：robots 等价匹配改为 RFC 9309（Allow 胜）

`source_adapters/jobpilot_sources/robots.py` 的 `verdict()` 此前在**等价匹配**
（Allow 与 Disallow 规则同样具体）时让 Disallow 胜，并在文档里声明这是比
RFC 9309 §2.2.2 更严格的有意偏离。按任务要求改回标准语义：

- **更具体的规则仍然优先**（最长匹配）——`Disallow: /jobs/*/apply` 仍然胜过
  `Allow: /jobs/`，长度更长的 Disallow 仍然胜过更短的 Allow（反之亦然）；
- **仅等价匹配**（规则长度相同且都匹配）时 **Allow 胜**（RFC 9309 §2.2.2）；
- **所有 fail-closed 路径不变**：robots 拉取失败 / 非 2xx（除 404/410）/
  无法解码 / 无法解析 → 整站 deny；404/410 → 视为无规则（allow）；
- 单测更新：`test_longest_match_wins_and_allow_breaks_ties`、
  `test_equal_length_tie_allows_regardless_of_rule_order`（新增）、
  `test_equivalent_tie_never_weakens_specificity`（新增）、
  `test_two_disallow_rules_still_disallow`（新增）；
  README 的访问纪律条目同步为「等价匹配按 RFC 9309 由 Allow 胜」。
- 未做任何无关重构。

### 20.2 Part B：真实来源复验（2026-10-09，只读、按 robots 逐路径、低量）

预算：国聘 12 次请求、清华 16 次请求（含 503 复核），全部在预算内；诚实 UA；
无登录、无 CAPTCHA、无 cookie/UA 伪造、无证书绕过。机器可读记录：
`source_adapters/jobpilot_sources/tests/fixtures/cn_source_probe_2026-10-09.json`
（含 HTTP 状态、robots 判定、发现方法、脱敏后的证据片段与结论）。

**国聘（首选）→ OUT（自动化）**

- `robots.txt` 200，`User-agent: *` + 空 `Disallow:` → 全路径允许；首页 200 但为
  1.3 KB SPA 壳；无登录/CAPTCHA 标记。
- 公开 bundle `/static/js/main.a5c1e78d.js`（3,331,541 B，robots 允许）声明了真实
  契约：`POST /api/jobs/v3/list`（列表）、`GET /api/jobs/v3/info`（详情）。
- 同一 bundle 显示：生产环境下**所有以 `/api/jobs/v3` 开头的请求都必须带客户端
  签名** `Sign`/`T`/`Nonce`（`g(p.MB, …)`，`p.MB` 为打包在公开 JS 里的变换/密钥）。
- 未签名验证：`POST /api/jobs/v3/list`（3 种 body）→ nginx **405**；
  `GET /api/jobs/v3/list`、`/api/jobs/v3/info`、`/sitemap.xml`、
  `/sitemap_index.xml` → 均返回 1.3 KB SPA 壳（无数据、无 sitemap）。
- 结论：列表只能通过站点自有的**反自动化签名**访问 → 属风控/站点限制，
  不绕过、不复现签名 → **OUT**；人工采集仍是合规通道。

**清华（备选）→ 无可用契约**

- `career.tsinghua.edu.cn/robots.txt` 404 → 无规则 → 允许；首页 38,242 B
  服务端渲染 HTML（公开、无登录标记），但只有公告类栏目
  （`/tzgg.htm`、`/xwrd.htm`、`/gjzz.htm`、`/gatxs.htm`、`/jyrl.htm`），
  **没有岗位列表**。
- 其导航的「招聘信息」指向**另一个主机**：
  `career.cic.tsinghua.edu.cn/xsglxt/f/jyxt/anony/xxfb`（匿名视图）。
- 该主机 `robots.txt` 返回 **HTTP 503**（https 与 http 各 2 次、跨约 12 分钟
  共 4 次尝试均 503）→ RFC 9309「unavailable」→ **整站 fail-closed** →
  该列表**未被读取**（遵守 robots，未绕过）。

**其它候选**：仍维持 README 登记表既有判定（北森 TLS 证书错误 → OUT；O1 登录墙
→ OUT；南大 SPA / 牛客 / 北大 / 浙大 → 未验证），本步未再探测。

### 20.3 Part C/D：未开始（合规前提不成立）

没有已验证的合规列表契约，因此**没有**编写来源适配器，**没有**改动四处
allowlist（`config.SUPPORTED_PROVIDERS`、`providers.PROVIDERS`、CLI
`--provider` choices、C++ `IsSupportedObservationSource` 仍精确为
`greenhouse/lever/ashby`），也**没有**改动 `service/`。这正是 §12 的
stop condition 2：

> 目标来源的 robots.txt 禁止实际读取的列表路径 → 放弃该来源；若所有候选都被
> 禁止，则本阶段退化为「人工采集通道 + 框架 + 文档」。

**结论（最终措辞）**：Step 5 的真实来源部分**环境受限**
（*environment-limited*）——在预算内没有找到任何**可合规、可复用**的列表契约：

- 国聘：岗位列表只能通过站点自有的**客户端签名**接口访问（`Sign`/`T`/`Nonce`，
  反自动化风控）→ 记为 OUT，未复现签名；
- 清华：唯一公开的岗位列表位于 `career.cic.tsinghua.edu.cn`，其 `robots.txt`
  持续返回 **HTTP 503** → 按 RFC 9309「unavailable」fail-closed，**未读取**。

因此**没有任何国内来源适配器被实现，也没有注册任何国内 source 标识**；当前注册
来源仍只有 `greenhouse`/`lever`/`ashby`。证据位置：`source_adapters/README.md`
的「来源登记表 / Step 5 re-verification (2026-10-09)」与
`source_adapters/jobpilot_sources/tests/fixtures/cn_source_probe_2026-10-09.json`
（机器可读的探测记录，含 robots 判定、HTTP 状态、签名证据与结论）。

本阶段的可用产物是：robots/重试/编码/解析/SSRF/预算/页数上限框架 + F13 归一化
（task-10）、人工采集通道（task-11）、以及本节的来源复验证据。

### 20.4 产品负责人接受（环境受限）与未验证项 / 解除条件

**接受记录**：产品负责人已选择接受 Step 5 为「环境受限（environment-limited）」，
按 §12 stop condition 2 退化，**不重试** 国聘/清华、**不改** source allowlist、
**不新增**业务功能。以上判定是环境/合规结论，不是产品决策，也未被用作放宽纪律的
理由。

- **未验证**：国聘与清华都没有可用的列表契约；适配器与 allowlist 未改。
- **解除条件**：(a) 国聘侧拿到书面许可或官方公开数据 API（或出现不需要签名的
  服务端渲染公开列表）；(b) 清华侧 `career.cic.tsinghua.edu.cn/robots.txt`
  恢复为 200/404 —— 之后其匿名列表即可按同样的纪律复验并据以编写适配器；
  (c) 另择一个**服务端渲染色、robots 可用、无签名**的 S3 高校就业网作为新的
  候选（需 Lead 授权扩大候选范围）。
- **未运行**：L4（按任务要求）；C++ 测试未重跑（本步未改 C++ 任何文件）。

## 21. Step 7 / Step 8 实施记录（task-13：文档同步 + 单次 L4）

### 21.1 Step 7：文档同步（L0）

四份文档按顺序同步，保持一致（阶段编号、已完成项、剩余工作、下一步）：

1. **仓库根 `README.md`**：重写为「当前真实能力」清单（用户/画像/简历/岗位检索与
   推荐/投递/岗位导入/来源观测/AI 匹配；`source_adapters/` 的 0012 适配器、
   0014 框架、人工采集通道），并新增「未实现 / 非目标」一节，明确写出：
   > **没有真实的国内来源适配器**。0014 Step 5（首个真实中国来源）结论是
   > **环境受限（environment-limited）**：国聘的岗位列表只能通过其客户端签名
   > （`Sign`/`T`/`Nonce`，反自动化风控）访问；清华就业网的岗位列表主机
   > `career.cic.tsinghua.edu.cn` 的 `robots.txt` 持续返回 503 → 按 fail-closed
   > 不读取。因此**未编写国内来源适配器，也未注册任何国内 source 标识**；当前注册
   > 来源仍只有 `greenhouse` / `lever` / `ashby`。
   同时修正原文件未闭合的代码块，并指向 03/04/agent-workflow/project-state。
2. **`docs/ai/project-state.md`**：新增 **Active Plan (0014)** 一节（进度、已交付、
   环境受限的 Step 5、剩余工作与下一步）；「Current Stage」补 0013/0014 状态；预置
   的 0012 过期表述与 `reserved for 0014` 修正；Verified Snapshot 增加 0014 L4 一行。
   **`## Next Entry` 的 16/6 stage-numbering 记录逐字保留未改**（新增的 Active Plan
   末尾注明该段落是 2026-10-08 的记录、当前状态以本节为准）。
3. **本计划**：`Status` 注明实现至 Step 8 且 Step 5 环境受限；§13 勾选 Step 6/7/8
   并标注证据位置；§14 注明该节只适用于规划阶段、执行证据在 §18-§21；§15.4 注明
   Step 5 结论不是产品决策；§15.5 表格与「执行结论」同步；§20.3 补最终措辞
   （environment-limited、无合规契约、未写适配器/未注册）；§20.4 记录产品负责人
   **接受**该环境受限结论；本节记录 Step 7/8。
4. **`docs/ai/agent-workflow.md`**：新增 **§8 Execution budget and stop conditions**
   （七条：复用未受影响证据；单来源默认 ≤5 分钟探测且更严的计划上限优先；有界重试后
   停止且不得放宽合规守卫；按影响面选测试；每阶段只做一次 Git 审阅；失败仍交付已核实
   事实/停止原因/不确定性/下一步；绝不以跳过验证、放宽安全约束或编造结果换时间）。
   §2 绑定规则加第 6 条指向 §8；§7 失败模式表补 3 行。未重复或矛盾既有条款。

一致性检查：四份文档对「0014 = China sources + manual capture（环境受限）、
0015 = resume tailoring、0016 = packaging」的编号一致；对「已完成 Step 1-4/6/7/8、
Step 5 环境受限、浏览器真机运行 UNVERIFIED」一致；F13/F14/既有库迁移证据未被改写或
弱化（只在文档层增加说明）。

### 21.2 Step 8：单次 L4（全绿，2026-10-09）

环境探测（先做，harness）：MySQL/Redis 容器 healthy；`docker exec jobpilot-redis
redis-cli ping` → `PONG`；MySQL 凭据探测 `service/build/presets/debug/backend/test_schema`
→ `[  PASSED  ] 9 tests.`（该二进制是 L4 前的旧构建，故为 9 项；重建后为 11 项）。
凭据来自运行中的容器（user/password `jobpilot`，`MYSQL_HOST=127.0.0.1`），未猜测、
未提交。

六阶段**串行**执行，每阶段完整 stdout 重定向到独立日志（ctest 从未经 `tail` 管道）：

| 阶段 | 开始 → 结束（本地时间） | 耗时 | 关键原始行 | rc |
|---|---|---|---|---|
| 1 service Debug（configure+build+full ctest） | 20:50:33 → 20:52:38 | 125s | `100% tests passed, 0 tests failed out of 254` | configure/build/ctest = 0 |
| 2 service ASan（同上） | 20:52:38 → 20:55:37 | 179s | `100% tests passed, 0 tests failed out of 254` | 0/0/0 |
| 3 service UBSan（同上） | 20:55:37 → 20:57:40 | 123s | `100% tests passed, 0 tests failed out of 254` | 0/0/0 |
| 4 service Release（build all + link check） | 20:57:40 → 20:58:41 | 61s | `[ 76%] Linking CXX executable jobpilot_service`；`-rwxrwxr-x … jobpilot_service`（2,454,416 B）；`ldd` 无 `not found` | configure/build/ls/ldd = 0 |
| 5 root Release（full build） | 20:58:41 → 20:59:45 | 64s | `[100%] Built target test_application_http` | configure/build = 0 |
| 6 Python suites | 20:59:45 → 20:59:47 | 2s | `Ran 337 tests in 0.325s` `OK`（source_adapters）；`Ran 190 tests in 0.438s` `OK`（ai_worker） | 0/0 |

- **发现总数**：service 三个 preset 均为 **254**（本地 230 的旧 discovery 已因本阶段
  的 24 个新用例更新为 254）；root preset 254。`job` 标签在每次全量 CTest 中为 106 项
  且全部通过（Label Time Summary: `job = 5.67 / 10.33 / 4.04 sec*proc (106 tests)`）。
- **LastTestsFailed.log**：三个 preset 目录均**不存在**（`ls` 报 no such file），
  故既无现存失败也无陈旧失败文件可误读。
- **sanitizer 文本检索**：ASan/UBSan 日志中 `AddressSanitizer|LeakSanitizer|
  detected memory leaks|UndefinedBehaviorSanitizer|runtime error:` 命中数均为 **0**；
  `***Failed` 与 `The following tests FAILED` 命中数均为 **0**。
- **日志完整性**：三个 ctest 阶段日志均包含 `Label Time Summary` 与
  `Total Test time (real) = 60.94 / 50.78 / 33.89 sec`（证明完整 stdout 被捕获，
  `NN% tests passed` 行未被截断）。日志文件：
  `/tmp/l4_0014_phase1_debug.log` … `phase6_python.log`；汇总
  `/tmp/l4_0014_summary.txt`。
- **总耗时**：20:50:33 → 20:59:47 ≈ 9 分 14 秒（4 核主机，串行）。
- **结论**：L4 一次通过（0 失败、0 sanitizer 诊断、两处 Release 构建与链接成功、
  两套 Python 套件全过），四阶段 L4 前置条件满足；本阶段满足关闭条件，可归档
  （归档动作由 Lead 执行，本任务未写 git）。

### 21.3 本轮未做

- 未重新探测国聘/清华（按产品负责人决定：Step 5 环境受限，不再重试）。
- 未改 source allowlist、未新增业务功能、未跑任何真实网络请求。
- 未跑 TSan（本阶段未新增并发；TSan 结论记录在 `docs/quality/README.md`）。
- 未提交/推送（no git write commands）。
