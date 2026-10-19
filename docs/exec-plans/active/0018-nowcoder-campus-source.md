# ExecPlan 0018 — 牛客校招来源接入（nowcoder campus source）

> 状态：**ACTIVE（2026-10-19 批准）**
> 日期：2026-10-19
> 基线：`main` @ `928d335`（与 `origin/main` 一致；批准时工作区只有本计划与 `docs/ai/CURRENT_STATE.md`）
> 时间盒：**2–3 个工作日**（2026-10-19 → 2026-10-21/22）。到点必须提交收口报告，不做无限打磨。
>
> 批准记录（2026-10-19，owner）：
> - D1 批准；来源登记与 `docs/04` 写明 robots 允许、免登录、低频、无绕过，**不写法律结论**；ToS 接受由 owner 记录。
> - D2 live smoke 用 board `后端`；正式采集板另行配置。
> - D3/D4/D5/D6/D8 按本计划建议执行。
> - D7 隔离栈必须独立库名/端口/卷，收尾提交拆除证据。
> - 多板组织方式必须明确（多板 + 5s 间隔预算）；若配置暂不支持多板，纳入本计划或另立 0019 并说明影响。
> - `complete=false` 必须同时出现在 payload、文档与运行输出中；CURRENT_STATE 中未运行的项不得在收口报告里改为通过。
> - 批准后 `CURRENT_STATE.md` 移到 `docs/ai/`，本文件转为 `docs/exec-plans/active/0018-nowcoder-campus-source.md` 并提交。
> - 收口报告必须附：验收命令输出、唯一一次 L4 结果、robots 证据文件、隔离栈拆除证据。

## 1. 背景与当前状态

JobPilotAI 当前只有 `greenhouse` / `lever` / `ashby` 三个海外 ATS 来源。0014 Step 5 的国内来源尝试被记录为 environment-limited：国聘需要客户端签名，清华列表主机 robots 持续 503。此后没有注册任何国内 source 标识。

2026-10-19 的独立复核发现牛客存在一条**免登录、robots 允许的 SSR 列表通路**：

```text
GET https://www.nowcoder.com/jobs/school/jobs?search=<关键词>&city=<中文城市>
```

这与 handoff §10/§20 V3 的“牛客无可用列表、仅人工采集”结论不同。完整证据见 `CURRENT_STATE.md` §8；关键事实：

| 事实 | 证据 |
|---|---|
| robots 只禁止 `/search`、`/nccommon`、`/ab/ab-test-flow` | `robots.txt` 200，619 B；项目 `parse_robots` 判定 `/jobs/school/jobs`、`/jobs/detail/<id>` 均为 allowed |
| 免登录 | 页面无登录墙标记；响应清空 `username`/`uid` cookie；未使用任何登录凭据 |
| SSR | 页面内嵌 `window.__INITIAL_STATE__`；`jobListData` 在 `app.132`（本次构建），必须递归查找，不能硬编码 |
| 单次最多 20 条、无翻页 | `search=后端` 返回 20；`page=2`、`pageSize=100` 仍返回 20，且不是下一页 |
| 结果顺序有抖动 | 两次 `search=后端` 的 20 条中 19 条重叠；不能把一次窗口当作稳定全量 |
| 合法空结果 | 无匹配关键词返回 200 + `jobListData: []` |
| 原生岗 | 6 位 id；`jobName`/`companyNameText`/`jobCity`/`salaryText`；`ext` 是 JSON 字符串，含 `requirements` + `infos`（JD） |
| 内推聚合岗 | 14 位 id；`jobTitle`/`companyName`/`salary`/`description`（HTML 内联 JD）；`platform=moka`；`city` 可能为空 |
| 详情页 | 原生与聚合样本的 `/jobs/detail/<id>` 都有 SSR；聚合详情含 `globalJobId`、`originId`、`platform`、`jobDetailUrl`（外部 Moka 链接）。另一会话“聚合详情无 SSR”的判断在本次样本中**不成立** |
| 未知参数降级 | 本次 `page`/`foo`/`pageSize` 组合**未复现**“降级空壳”；但解析器仍必须 fail-closed |

**合规边界**：robots 允许不等于法律意见或 ToS 许可。本计划以 robots + 免登录 + 无 CAPTCHA/签名绕过 + 不自动投递为工程前提，ToS/法律接受是 owner 决策。任何 403/429/登录墙/验证码出现时停止，不绕过。

## 2. 目标

1. 新增 `nowcoder` 来源适配器，读取牛客校招公开 SSR 列表，归一化为现有 `POST /internal/jobs/source-sync` 观测。
2. 在 Python `SUPPORTED_PROVIDERS` / `PROVIDERS`、C++ `IsSupportedObservationSource`、`docs/04` §5.5 四处注册 `nowcoder`，并通过 allowlist 一致性测试。
3. 用 fixture 测试覆盖两种记录形态、20 条上限、空结果、缺状态/坏结构、字段映射、批量去重与 48 KiB 上限。
4. 在隔离栈上完成一次受控 live smoke（V5）：`--dry-run` 先看 payload，再实际发布，检查公开 API 与审核队列，然后拆除。
5. 保持 `complete=false`：牛客无法证明全量覆盖，绝不关闭缺失岗位。
6. 不改变 schema、不增加 `apply_url`、不引入自动投递、不触碰 P1 网络层。

## 3. 非目标（本计划明确不做）

- **不做自动投递/自动提交/表单填写**；`tools/checks/no_submission_path.py` 必须继续 rc=0。
- **不做登录墙自动化**：不登录、不复用 cookie、不伪造 UA、不绕 CAPTCHA/签名/TLS。
- **不接入 BOSS/智联/51job/猎聘/实习僧/国聘/北森**：分别因登录墙、字体反爬、客户端签名、TLS 自签等原因排除（沿用 0014 证据）。
- **不做牛客翻页**：官方列表没有可用翻页；一次查询最多 20 条，接受不完整窗口。
- **不做详情页抓取**：第一版列表已带 JD（原生 `ext`、聚合 `description`）；详情页只作为未来 `apply_url`/富化选项。
- **不做跨来源 canonical 去重**：0012 明确把 canonical multi-source Job identity 列为 non-goal；本计划只做批次内去重，跨来源 canonical 身份另立计划。
- **不新增 `apply_url` 列**：聚合岗的外部 Moka 链接 `jobDetailUrl` 在详情 SSR 中可见，但当前 schema 没有 `apply_url`；另立计划。
- **不做 `nowcoder-search` CLI（bun）依赖**：本 VM 无该 CLI、无 `bun`，仓库内也没有；不作为可复现的过渡投喂器。
- **不做 Web UI、公司/ATS 注册表扫描器、`--json` 输出、定时任务**：属于后续阶段；本计划只做一个来源、一个 CLI 路径。
- **不修改 P1**：`backend/{include,src}/jobpilot/{network,http}/` 不动。
- **不修改 CI workflow**：沿用现有 `ci` + `image`；新增 Python 测试自动被现有 suite 步骤覆盖。

## 4. 约束与不变量

- 依赖方向：Controller → Service → DAO 不变；适配器只通过 HTTP internal API 发布，不直连数据库。
- `source_url` 仍是 `uk_source_url` 幂等键；本计划不改变其语义。
- 人工优先（F14）：观测命中 `source='manual'` 行时服务端整条跳过；适配器不得覆盖。
- 任何来源字段无法确定时**省略**，不猜测；未知 `eduLevel`、歧义薪资、缺失城市一律 omit。
- 描述 48 KiB UTF-8 字节硬上限；超限**整行跳过并报诊断**，绝不截断。
- 诚实 UA：`jobpilot-source-adapters/0.1 (+https://github.com/jobpilotai; public job board reader)`。
- 低频：每个 nowcoder board 一次列表请求；建议 5 秒最小间隔；无并发；遇到 403/429 停止。
- `complete=false`：即使返回 20 条也不声称完整；空结果不发布（publisher 已有行为：`complete=false` + 0 条 → 无 payload）。
- 不记录、不提交 token；live smoke 的 token 只从环境变量读取。
- 所有修改前先更新合同/文档；L4 只在阶段收口运行一次，且独占。

## 5. 研究证据（2026-10-19）

### 5.1 请求预算与方式

- 9 次只读 GET：1 次 robots、4 次列表（C++ 深圳、后端、page=2、foo=bar）、1 次 pageSize、1 次空关键词、2 次详情。
- 诚实 UA；无登录、无 cookie 复用、无签名/CAPTCHA/TLS 绕过；在 agent-workflow §8.2 的 5 分钟候选预算内。
- 原始 HTML/响应头：`/tmp/jobpilot-nowcoder-probe-2026-10-19/`（仓库外，未提交）。实现阶段会把必要样本裁剪、脱敏后放入 fixtures。

### 5.2 robots

```text
User-agent:*
Disallow:/search
Disallow:/nccommon
Disallow:/ab/ab-test-flow
```

项目 `parse_robots` 判定：`/jobs/school/jobs`、`/jobs/school/jobs?search=...`、`/jobs/detail/404338` = allowed；`/search`、`/nccommon/*`、`/ab/ab-test-flow/*` = disallowed；无 `Crawl-delay`。

### 5.3 记录形态与字段样本

| 字段 | 原生岗（404338） | 聚合岗（30300000146411） |
|---|---|---|
| id | `404338`（6 位） | `30300000146411`（14 位） |
| 标题 | `jobName=嵌入式软件工程师` | `jobTitle=jobName=后端开发工程师` |
| 公司 | `companyNameText=Formlabs` | `companyName=重庆千里科技股份有限公司` |
| 城市 | `jobCity=深圳` | `city=None`, `jobCity=''`（缺失） |
| 薪资 | `salaryText=25-35K·12薪` | `salary=薪资面议` |
| JD | `ext={"requirements":"...","infos":"..."}` | `description=<p>...`（HTML） |
| 学历 | `eduLevel=5000`（编码未知，**不猜**） | `education=本科`（可映射） |
| 招聘类型 | `recruitType=1`（校招） | `recruitType=0`（未知） |
| 发布 | `createTime=1754033527000`（UTC 2025-08-01 07:32:07） | `createTime=1787378565000`（UTC 2026-08-22 06:02:45） |
| 截止 | `deliverEnd=1798732799000`（UTC 2026-12-31 15:59:59 = 北京 23:59:59） | 列表无 `deliverEnd`（omit） |
| 外部链接 | 无 | `platform=moka`；详情 SSR 有 `jobDetailUrl=https://app.mokahr.com/...` |
| 详情 SSR | 有（`id`、`jobName`、`jobCity`、`ext`、`companyName`） | 有（`globalJobId`、`originId`、`platform`、`jobDetailUrl`、`description`、`salary`、`education`、`publishedAt`、`openAt`、`status=open`） |

### 5.4 与另一会话结论的差异

| 另一会话说法 | 本次复核 | 计划影响 |
|---|---|---|
| 未知参数导致降级空壳 | 本次 `page`/`foo`/`pageSize` 未复现；空关键词是合法空数组 | 解析器仍对缺失 `__INITIAL_STATE__`/`jobListData` fail-closed；不把“未知参数”当作唯一判据 |
| 聚合岗详情页无 SSR | 本次聚合样本详情页**有 SSR** | 第一版不依赖详情页；未来若用详情页，按记录形态重新验证 |
| 单次最多 20 条、无翻页 | 确认 | `complete=false`；不实现翻页 |
| 原生/聚合两种形态 | 确认 | 两套映射 + 统一归一化 |

## 6. 选定设计

### 6.1 总体数据流

```text
config: type=nowcoder, board="<keyword>|<city>"
  -> nowcoder.board_url(board) = https://www.nowcoder.com/jobs/school/jobs?search=...&city=...
  -> Fetcher.get_text(url)  # robots-first, SSRF guard, retries, timeout, size cap, honest UA
  -> nowcoder.parse_board(text, board=..., company=...)
       -> extract window.__INITIAL_STATE__ (JSON)
       -> recursive find of key jobListData (list)
       -> detect native / aggregator per record
       -> normalize fields (omit unknowns; 48 KiB cap; F13 timestamps; salary rules)
       -> in-batch dedupe (normalized title+company+city)
       -> BoardResult(source="nowcoder", complete=False, jobs=[...], diagnostics=[...])
  -> publisher.build_batches / POST /internal/jobs/source-sync
  -> C++ JobQuality + UpsertJob (source=nowcoder, complete=false; no closures)
```

### 6.2 Board 规格与配置

`board` 使用紧凑查询规格，避免把长 URL/中文转义写进 `source_scope`：

```yaml
# source_adapters/config.example.yaml（新增示例；默认 disabled）
sources:
  - type: nowcoder
    board: "后端"          # 或 "C++|深圳"；city 可省略
    enabled: false         # 默认不启用；owner 显式开启
```

- 解析：按第一个 `|` 分割；keyword 必填、city 可空；trim；拒绝空 keyword/控制字符/超长（建议 keyword ≤ 64 B、city ≤ 32 B）。
- `board_url` 用 `urllib.parse.urlencode` 生成 `search=` 与可选 `city=`。
- `source_scope`：沿用 `run_sync` 的 `source.board`（如 `后端` 或 `C++|深圳`），长度远小于 128 B；`source` 已是 `nowcoder`，无需再加前缀。

**多板组织方式（批准条件 1/5 的落实）**：

- 配置的 `sources:` 本来就是列表，因此**支持直接配置多个 nowcoder board**，例如：
  ```yaml
  sources:
    - { type: nowcoder, board: "后端", enabled: true }
    - { type: nowcoder, board: "C++|深圳", enabled: true }
    - { type: nowcoder, board: "算法|北京", enabled: true }
  ```
- 每个 board 是独立的 `source_scope`，各自做 20 条窗口内的批次内去重；不同 board 之间不做跨 board 去重（同一条岗位可能出现在两个关键词下，由 `uk_source_url` 幂等吸收）。
- **5s 间隔预算**：`nowcoder.MIN_INTERVAL_SECONDS = 5.0`；`run_sync` 为该 provider 创建独立 `Fetcher`，按顺序处理 board；每个 board 只发 1 次列表请求（不请求详情、不翻页）。因此一次 run 的 nowcoder 请求数 = 启用的 board 数，间隔 ≥5s。
- **推荐上限**：一次 run 最多 3 个 nowcoder board（3 次列表请求，约 10s 间隔总预算）；超过 3 个时建议拆成多次人工运行。配置层不强制拒绝，但在 README 与运行输出中提示。
- 单板覆盖很薄（最多 20 条，且顺序抖动）：这是 `complete=false` 的直接原因；多板只能扩大**观测窗口**，仍不能证明全量，文档与输出都不得暗示“全量扫描”。
- 若未来要支持“一个 board 内多页/多城市组合”，必须重新评估 robots、频率与数据完整性；不在本计划。

### 6.3 抓取与频率

- 复用 `Fetcher`（robots、SSRF、重试、超时、2 MiB 响应上限、诚实 UA）。
- `nowcoder.MIN_INTERVAL_SECONDS = 5.0`；`run_sync` 为该 provider 创建独立的 `Fetcher(min_interval_seconds=5.0)`（其余 provider 保持默认）。这是本计划唯一的 CLI/共享层小改动。
- 每个 board 只发 1 次列表请求；不请求详情；不翻页；无并发。
- 遇到 403/429/登录墙/验证码/结构异常：停止该 board，记录失败，不重试到“绿”。
- robots 每次运行由 `Fetcher` 重新获取（缓存 TTL 1h）；不缓存过期规则。

### 6.4 SSR 解析（fail-closed）

1. 在 HTML 中查找 `window.__INITIAL_STATE__=`；找不到 → `ProviderError`。
2. 用 `json.JSONDecoder().raw_decode` 解析对象；失败 → `ProviderError`。
3. 递归遍历 dict，收集**直接键**为 `jobListData` 且值为 list 的节点：
   - 0 个 → `ProviderError`（不是空结果）。
   - 1 个 → 使用。
   - 多个 → 取元素最多者，并记一条 diagnostic/warning；若并列，取第一个。
4. `jobListData` 存在但为空 list → 合法空结果：`BoardResult(jobs=[], complete=False)`；publisher 不发布（已有行为）。
5. 每个 record 必须是 dict；不是 dict → 跳过并记 diagnostic。
6. **不硬编码** `app.132`；fixture 用一个不同的模块号（如 `app.999`）证明递归查找有效。

### 6.5 形态识别

```text
aggregator := id 为 int/str 且数字长度 >= 12
              AND (ext is None)
              AND (jobTitle or description)
native     := ext is str and ext 可解析为 JSON object
```

- 两者都不满足 → 跳过该行，diagnostic `unrecognised_record_shape`。
- `id` 缺失/非数字 → 跳过，`missing_external_id`。
- 聚合岗的 `globalJobId`（详情 SSR）在本版不读取；列表 id 已是稳定的 14 位标识。

### 6.6 字段映射

| JobPilot 字段 | 原生岗来源 | 聚合岗来源 | 规则 |
|---|---|---|---|
| `title` | `jobName` | `jobTitle` / `jobName` | 必填；空则跳过 |
| `company` | `companyNameText` | `companyName` | 必填；空则跳过 |
| `location` | `jobCity` / `jobCityList[0]` | `city` / `jobCity` | `normalize_location`（去“市”等）；空则 omit |
| `salary_min/max` + currency | `salaryText` | `salary` | 仅 `parse_monthly_salary_thousand` 可确定时发布；`面议`/年薪/日薪/歧义 → omit |
| `description` | `ext.infos` + `ext.requirements`（JSON 字符串解析后拼接） | `description`（HTML → `html_to_text`） | 48 KiB UTF-8 字节上限；超限整行跳过 + `description_too_large`；绝不截断 |
| `published_at` | `createTime`（epoch ms） | `createTime`（epoch ms） | F13：显式时刻保留真实瞬时；缺失 omit |
| `deadline` / `deadline_date_only` | `deliverEnd`（epoch ms） | 列表无 → omit | 显式时刻 → UTC 瞬时 + `date_only=false`；缺失 omit |
| `job_nature` | `recruitType` 1→1、2→3、3→2 | 同左；0/未知 → omit | 源站详情映射：1校招 / 2实习 / 3社招 |
| `education_required` | `eduLevel` 编码未知 → **omit** | `education` 字符串 → `map_education` | 只映射明确字符串；`5000` 等编码不猜 |
| `experience_years_*` | 无 | 无 | omit |
| `source` | — | — | 固定 `nowcoder` |
| `external_id` | `str(id)` | `str(id)`（14 位） | 稳定；用于审计 |
| `source_url` | `https://www.nowcoder.com/jobs/detail/<id>` | 同左 | 必须指向详情页；这是 `uk_source_url` 键 |
| `source_scope` | — | — | `board` 规格（如 `后端` / `C++|深圳`） |
| `complete` | — | — | **始终 false** |
| `company` 配置 | 记录内优先；配置 `company` 仅作 fallback | 同左 | 牛客列表自带公司名；配置通常不需要 |

**不映射**：`jobDetailUrl`（外部 Moka 申请链接）本版不落库；`graduationYear` 在 `jobs` 无对应列；聚合岗 `data.skills` 无等级，按 0014 规则丢弃。

### 6.7 逐行容错与诊断

- `BoardResult` 增加可选字段 `diagnostics: list[dict] = []`（向后兼容；现有 provider 不填）。
- 每条 diagnostic 至少含 `reason`（稳定机器码）与可选的 `external_id`/`field`/`index`；不包含正文片段或凭据。
- 行级跳过原因：`missing_title`、`missing_company`、`missing_external_id`、`unrecognised_record_shape`、`description_too_large`、`invalid_ext_json`、`duplicate_in_batch`。
- `run_sync` 在日志中输出 `diagnostics=<n>` 与最多 5 条样例；不因单行跳过而失败整个 board。
- 若整个 board 的 `jobListData` 缺失/结构错误 → `ProviderError`，该 board 失败，不发布。

### 6.8 批次内去重与跨来源边界

- 去重键：`normalize(title) + normalize(company) + normalize(location)`（NFKC/trim/大小写折叠；城市去“市”）。
- 同一批内重复：保留第一条（源站排序优先），其余跳过并记 `duplicate_in_batch`；不发布重复行。
- 现有 C++ `duplicate_content_in_batch` 质量门仍是第二道防线（它只看单批）；本计划不改变它。
- **跨来源**（牛客 vs manual/ATS）：`uk_source_url` 只处理同 URL；F14 保证 manual 胜出。内容级跨来源重复需要 canonical identity（新列/新表/合同变更），**不在本计划**；作为已知限制记录，另立计划。

### 6.9 质量门与可见性

- 归一化 payload 走现有 C++ `JobQuality`：公司/标题/URL/描述/平台名/导航污染等判定不变。
- 预期部分行会进入 `needs_review`（例如公司名恰好是平台名、标题与公司相同、描述含导航）；这是设计行为，live smoke 要检查审核队列，**不自动 approve**。
- 公共搜索默认只返回 `clean`；`needs_review` 只在内部 token 复核面可见。

## 7. 被否决的备选

| 备选 | 否决原因 |
|---|---|
| 用 `public_page` 通用 provider 配置牛客 URL | 通用 provider 解析 HTML/JSON-LD/feed，不解析 `window.__INITIAL_STATE__`；会得到空结果或误判 |
| 先做 `nowcoder-search` CLI（bun）过渡投喂 | 本 VM 无 bun/CLI，仓库无该工具；无法复现、无法测试；且仍需把原始状态转成 import payload，仍是代码 |
| 用 `complete=true` | 牛客无翻页、单次最多 20 条、顺序抖动，无法证明全量；会导致错误关闭岗位 |
| 抓详情页补字段 | 列表已含 JD；详情页额外请求增加风险与预算；`apply_url` 尚无列，收益低 |
| 跨来源 canonical 去重本计划一起做 | 需要新列/新表/合同变更与迁移工具讨论；0012 已将其列为 non-goal；会扩大风险 |
| 自动登录/复用 cookie/代理轮换 | 违反 ADR-0011 与硬约束；不做 |
| 把 `eduLevel=5000` 猜成某个学历 | 编码未验证；猜测会伪造数据；omit |
| 用未知参数探测“降级空壳”作为解析判据 | 本次未复现；判据应是 `__INITIAL_STATE__`/`jobListData` 结构，而不是参数名 |

## 8. 涉及文件

### 8.1 新增

| 文件 | 用途 |
|---|---|
| `source_adapters/jobpilot_sources/providers/nowcoder.py` | provider：board_url / parse_board / 形态识别 / 映射 / 去重 / diagnostics |
| `source_adapters/jobpilot_sources/tests/test_provider_nowcoder.py` | fixture 测试：两种形态、20 条、空结果、坏结构、映射、去重、上限 |
| `source_adapters/jobpilot_sources/tests/fixtures/nowcoder_search_native.html` | 裁剪后的原生岗 SSR 样本（含 `__INITIAL_STATE__`） |
| `source_adapters/jobpilot_sources/tests/fixtures/nowcoder_search_aggregator.html` | 裁剪后的聚合岗样本（同时含两种形态） |
| `source_adapters/jobpilot_sources/tests/fixtures/nowcoder_empty.html` | `jobListData: []` |
| `source_adapters/jobpilot_sources/tests/fixtures/nowcoder_missing_state.html` | 缺 `__INITIAL_STATE__`/`jobListData`，用于 fail-closed |
| `source_adapters/jobpilot_sources/tests/fixtures/nowcoder_robots_2026-10-19.txt` | robots 原文（619 B） |
| `source_adapters/jobpilot_sources/tests/fixtures/cn_source_probe_2026-10-19.json` | 机器可读探测记录（URL、HTTP 状态、robots 判定、结果数、形态、结论；无 cookie/token） |
| `docs/exec-plans/active/0018-nowcoder-campus-source.md` | 批准后把本文件转成项目正式 ExecPlan（含进度/证据/回滚） |

### 8.2 修改

| 文件 | 改动 |
|---|---|
| `source_adapters/jobpilot_sources/providers/__init__.py` | import `nowcoder`；`PROVIDERS` 增加 `nowcoder`（顺序与 config 一致） |
| `source_adapters/jobpilot_sources/config.py` | `SUPPORTED_PROVIDERS` 增加 `"nowcoder"`；board 规格校验 |
| `source_adapters/jobpilot_sources/cli.py` | provider 级 `MIN_INTERVAL_SECONDS` 的 Fetcher 构造；`diagnostics` 日志 |
| `source_adapters/jobpilot_sources/providers/base.py` | `BoardResult` 增加 `diagnostics` 字段（默认空） |
| `source_adapters/config.example.yaml` | 新增 disabled 的 nowcoder 示例 |
| `source_adapters/jobpilot_sources/tests/test_config.py` | 允许列表/board 校验测试更新 |
| `source_adapters/jobpilot_sources/tests/test_providers.py` | 注册表包含 nowcoder（或由新测试覆盖） |
| `source_adapters/jobpilot_sources/tests/test_source_probe_evidence.py` | 三来源断言更新为四来源；新增 2026-10-19 探测证据断言 |
| `source_adapters/jobpilot_sources/tests/test_allowlist_consistency.py` | 由 config/C++ 驱动，通常无需改；确认顺序一致 |
| `service/src/job/source_observation.cpp` | `IsSupportedObservationSource` 增加 `nowcoder`（顺序与 Python 一致） |
| `service/tests/test_source_observation.cpp` | 在既有用例中加 `EXPECT_TRUE(IsSupportedObservationSource("nowcoder"))`（不新增 CTest 用例） |
| `docs/04-api-design.md` §5.5 | allowlist 改为四项；新增 nowcoder 的 `complete=false`、board 规格、来源边界说明 |
| `docs/03-database-design.md` | 不改 schema；如来源列注释需要，补一句 `source` 可为 `nowcoder`（可选，不改变 DDL） |
| `source_adapters/README.md` | provider 表、layout、source register（2026-10-19 牛客条目）、访问纪律、探测证据 |
| `README.md` | 能力清单/国内来源段落更新；指向 source register |
| `AGENTS.md` | 当前阶段与 allowlist 描述更新（如仍写三项） |
| `docs/ai/project-state.md` | 新增 0018 阶段状态、验证快照、carried-forward |
| `docs/PROJECT_DOCUMENTATION_INDEX.md` | 加入 0018 计划；更新 source_adapters README 的有效性说明 |
| `docs/quality/README.md` | 如 Python 测试计数/命令需要更新；C++ 计数不变则不改 coverage map |
| `docs/RELEASE_CHECKLIST.md` | 阶段收口时更新日期/证据块（若涉及） |

> **不改**：`database/schema/*.sql`、`Dockerfile`、`docker-compose.yml`、`.github/workflows/*`、`backend/{include,src}/jobpilot/{network,http}/`、`tools/checks/no_submission_path.py`。

## 9. 实施阶段与验收

### P0 — 合同与来源登记（文档先行）

- 更新 `docs/04` §5.5：allowlist 四项、nowcoder board 规格、`complete=false` 语义、聚合岗外部链接不落库。
- 更新 `source_adapters/README.md` 来源登记表：牛客条目（日期、robots、免登录、20 条上限、两种形态、ToS 未审阅）。
- 更新 `README.md` / `AGENTS.md` 的“当前只有 greenhouse/lever/ashby”措辞。
- 验收：文档 diff 自洽；`python3 tools/checks/release_preflight.py` 的 links 检查仍 PASS（新增相对链接有效）。

### P1 — 四处注册与共享层小改

- `config.SUPPORTED_PROVIDERS`、`providers.PROVIDERS`、C++ `IsSupportedObservationSource` 同时加入 `nowcoder`（同序）。
- `BoardResult.diagnostics`；`run_sync` 的 provider 级 Fetcher 间隔；`diagnostics` 日志。
- 更新 `test_source_probe_evidence.py`、`test_config.py`、`test_source_observation.cpp`。
- 验收：
  - `cd source_adapters && python3 -m unittest jobpilot_sources.tests.test_allowlist_consistency jobpilot_sources.tests.test_source_probe_evidence jobpilot_sources.tests.test_config -v` → 全绿。
  - `cmake --build build/presets/debug -j$(nproc)` → rc=0。
  - `ctest --test-dir build/presets/debug -R SourceObservation --output-on-failure` → 全绿。
  - `ctest --test-dir build/presets/debug -N | tail -1` → **312**（不新增 C++ 用例）。

### P2 — provider 与 fixtures

- 实现 `nowcoder.py`：board_url、SSR 解析、形态识别、字段映射、去重、diagnostics、48 KiB 上限。
- 裁剪/脱敏 fixtures；测试覆盖：
  1. 递归查找（fixture 使用 `app.999`，证明不依赖 132）。
  2. 原生岗映射（title/company/city/salary/JD/published/deadline/job_nature）。
  3. 聚合岗映射（title/company/description HTML→text/education/salary omit）。
  4. 20 条上限。
  5. 空 `jobListData` → 0 jobs，`complete=False`。
  6. 缺 `__INITIAL_STATE__` / 缺 `jobListData` → `ProviderError`。
  7. 坏 record/缺 id/缺 company/title → 跳过 + diagnostic。
  8. `ext` 非法 JSON → 跳过 + diagnostic。
  9. 超 48 KiB → 整行跳过 + `description_too_large`，不截断。
  10. 批次内重复 → 只保留一条 + `duplicate_in_batch`。
  11. `source_url` = nowcoder 详情 URL；`external_id` 稳定。
  12. robots 原文的 allowed/disallowed 判定（离线）。
- 验收：
  - `cd source_adapters && python3 -m unittest jobpilot_sources.tests.test_provider_nowcoder -v` → 全绿。
  - `cd source_adapters && python3 -m unittest discover -s jobpilot_sources/tests -t .` → 全绿（记录新的 `Ran N tests`）。
  - `python3 tools/checks/no_submission_path.py` → rc=0。
  - `python3 tools/checks/release_preflight.py` → 8 pass / 0 fail / 0 skip（需 actionlint）。
  - `git diff --check` → rc=0。

### P3 — 受控 live dry-run（不写库）

- 用临时配置（一个 board，建议 `后端`，可拿到 20 条）运行：
  ```bash
  cd source_adapters
  INTERNAL_API_TOKEN=dummy python3 -m jobpilot_sources sync \
    --config /tmp/nowcoder-live.yaml --provider nowcoder --dry-run
  ```
- 记录：HTTP 200、`20 job(s)`、`board_complete=False`、`1 batch(es)`、`publish_complete=False`、diagnostics 计数、字段样例。
- 验收：无任何 POST；日志不含 token；至少 1 条原生、1 条聚合；空结果 board 不发布。
- **停止条件**：robots 判定变化、403/429、登录墙、结构异常 → 停止并记录，不绕过。

### P4 — 隔离栈受控发布（V5）

- 启动阶段自有 compose 项目（独立 project name / MySQL / Redis / 端口 / 卷；不碰开发库），加载 `database/schema/*.sql`。
- 用真实 internal token 运行一次 `sync --provider nowcoder`（一个 board）。
- 断言：
  - 响应 `created + updated == total`，`complete=false`，`skipped_manual` 计数合理。
  - `SELECT source, source_scope, external_id, source_url FROM jobs WHERE source='nowcoder'` 行数与响应一致。
  - `GET /api/v1/jobs?keyword=后端&page_size=100` 能看到 `clean` 行；`source_url` 指向 `https://www.nowcoder.com/jobs/detail/<id>`。
  - `GET /api/v1/jobs/{id}` 的 `posted_at`、`deadline` 与源字段一致（抽样）。
  - `GET /internal/jobs/review-queue` 检查 `needs_review` 行；**不自动 approve**。
  - 公开搜索/详情/AI/投递校验都看不到 `needs_review` 行（沿用 0015A 断言）。
- 拆除栈；记录原始输出（脱敏）到 ExecPlan 证据块。
- **停止条件**：任何自动提交/投递路径出现、robots 变化、数据质量不可接受 → 停止。

### P5 — 文档同步、L4、归档

- 更新 `docs/ai/project-state.md`、`docs/PROJECT_DOCUMENTATION_INDEX.md`、`docs/RELEASE_CHECKLIST.md`（如需要）。
- 阶段收口唯一一次 L4：
  - Debug / ASan / UBSan CTest（312 或更新后的计数；预期 312）；
  - service + root Release builds；
  - Python `source_adapters` 全绿（记录新计数）与 `ai_worker` 190；
  - sanitizer diagnostics 0；
  - review-backfill isolation：dev `[ SKIPPED ]` / scratch `[ PASSED ] 7`（沿用 0015A/0017 方法）；
  - L4 前跑 `test_bcrypt` 探针，>14s 则拒绝运行（0017 harness 规则）。
- 守卫：
  - `test_coverage_map.py` PASS（若 C++ 用例数不变）；
  - `release_preflight.py` 8/0/0；
  - `history_secret_scan.sh` 0 credential-shape；
  - `no_submission_path.py` rc=0；
  - `git diff --check`。
- 推送后 `tools/dev/ci-status.sh --limit 5`：`ci` + `image` 绿；记录 run id 与逐 job 耗时。
- 归档 `docs/exec-plans/active/0018-nowcoder-campus-source.md` → `completed/`，更新索引。

## 10. 依赖与风险

| 风险 | 概率/影响 | 缓解 |
|---|---|---|
| 牛客 SSR 结构随构建变化 | 中/高 | 递归查找，不硬编码模块号；fixture 用不同模块号；结构缺失即 fail-closed |
| 结果窗口抖动/无翻页/20 条上限 | 高/中 | `complete=false`；不关闭岗位；同 URL 幂等；批次内去重；接受不完整覆盖 |
| 聚合岗字段缺失（city/salary） | 高/低 | omit，不猜；质量门决定可见性 |
| `eduLevel` 编码未知 | 高/低 | omit；只映射明确的 `education` 字符串 |
| 描述 HTML 污染/超 48 KiB | 中/中 | `html_to_text`；超限整行跳过 + diagnostic；不截断 |
| 平台名/标题/描述触发 `needs_review` | 中/低 | 设计行为；审核队列人工处理；不自动 approve |
| robots/ToS 变化 | 中/高 | 每次运行重取 robots；来源登记表记录日期；403/429/登录墙即停 |
| 被限流/风控 | 中/中 | 一个 board 一次请求；5s 间隔；无并发；有界重试；不代理轮换 |
| 四个 allowlist 漂移 | 中/高 | `test_allowlist_consistency.py` 读取 C++ 源码，CI 运行 |
| 既有测试断言“只有三项” | 高/低 | 明确更新 `test_source_probe_evidence.py`、`test_config.py` |
| C++ 用例数变化导致 coverage map 失败 | 低/中 | 只在既有用例加断言，保持 312；若必须新增用例，同步刷新 `test-merge-map.json` 与审计 counts |
| Python 测试计数变化 | 高/低 | 记录新 `Ran N tests`；无固定计数守卫 |
| 跨来源重复 | 中/中 | 本计划只做批次内去重；canonical identity 另立计划；F14/manual 优先 |
| 无 `apply_url`，外部 Moka 链接丢失 | 中/低 | 明确 non-goal；另立 `apply_url` 计划 |
| live smoke 污染开发库 | 低/高 | 只用隔离 compose 项目；独立库/端口/卷；用后拆除 |
| L4 环境慢导致假失败 | 中/中 | 沿用 0017 的 `test_bcrypt` 14s 探针；独占地串行运行 |

**依赖**：现有 `Fetcher`/`robots`/`normalize_cn`/`salary`/`timestamps`/`publisher`；C++ source-sync；MySQL/Redis 隔离栈；internal token 仅从环境变量读取；actionlint（preflight 8/0/0 需要）。

## 11. 验收命令（汇总）

```bash
# A. 静态与 Python
cd "$(git rev-parse --show-toplevel)/source_adapters"
python3 -m unittest jobpilot_sources.tests.test_provider_nowcoder -v
python3 -m unittest discover -s jobpilot_sources/tests -t .
cd "$(git rev-parse --show-toplevel)"
python3 tools/checks/no_submission_path.py           # rc=0
python3 tools/checks/release_preflight.py             # 8 pass / 0 fail / 0 skip
tools/checks/history_secret_scan.sh                   # 0 credential-shape
git diff --check

# B. C++
cmake --build build/presets/debug -j"$(nproc)"        # rc=0
ctest --test-dir build/presets/debug -R SourceObservation --output-on-failure
ctest --test-dir build/presets/debug -N | tail -1     # 312
python3 tools/checks/test_coverage_map.py \
  --test-dir build/presets/debug --map docs/quality/test-merge-map.json  # PASS

# C. live dry-run（不写库）
cd source_adapters
INTERNAL_API_TOKEN=dummy python3 -m jobpilot_sources sync \
  --config /tmp/nowcoder-live.yaml --provider nowcoder --dry-run
# 期望：20 job(s), board_complete=False, 1 batch(es), publish_complete=False

# D. 隔离栈发布（V5；按实际项目名/端口替换）
# 启动阶段自有 compose -> 加载 schema -> 运行 sync -> 查询 API/DB -> 检查 review queue -> 拆除

# E. CI
tools/dev/ci-status.sh --limit 5
# 期望：最新 ci + image 均 completed/success
```

**验收标准**：

- `nowcoder` 在四个注册点存在且同序；allowlist 一致性测试绿。
- fixture 测试覆盖两种形态、20 条、空结果、坏结构、映射、去重、48 KiB。
- dry-run 不写库；live smoke `created+updated==total`，`complete=false`，公开 API 可见 `clean` 行，`needs_review` 行不公开。
- 无自动投递路径；无 schema 变更；P1 未改。
- L4 一次通过（或失败项明确归因并修复后重跑）；CI 绿。

## 12. 用户可自行验证的步骤

1. **robots**：
   ```bash
   curl -sS -A 'jobpilot-source-adapters/0.1 (+https://github.com/jobpilotai; public job board reader)' \
     https://www.nowcoder.com/robots.txt
   ```
   确认只禁止 `/search`、`/nccommon`、`/ab/ab-test-flow`；`/jobs/*` 不在禁止列表。
2. **SSR**：
   ```bash
   curl -sS -A '<同上 UA>' \
     'https://www.nowcoder.com/jobs/school/jobs?search=%E5%90%8E%E7%AB%AF' \
     | grep -c 'window.__INITIAL_STATE__'
   ```
   应看到 1；用浏览器 DevTools 搜索 `jobListData`，确认记录数与页面一致（≤20）。
3. **dry-run**：按 §11 C 运行；检查输出中的 job 数、`complete=False`、字段样例，确认没有 POST。
4. **隔离栈发布**：按 §11 D；用 `curl http://127.0.0.1:<port>/api/v1/jobs?keyword=后端&page_size=100` 查看结果，确认 `source_url` 指向牛客详情页。
5. **审核队列**：
   ```bash
   curl -H "X-Internal-Token: $INTERNAL_API_TOKEN" \
     'http://127.0.0.1:<port>/internal/jobs/review-queue?status=needs_review'
   ```
   检查被标记的行与原因；人工决定是否 approve/fix/reject。
6. **回滚**：`git revert <implementation commits>`；删除 nowcoder 配置；隔离栈直接拆除。

## 13. Schema 迁移与回滚

- **Schema 迁移：无。** 使用现有 `jobs` 列：`source`（VARCHAR(32)）、`source_scope`（VARCHAR(128)）、`external_id`（VARCHAR(255)）、`source_url`（VARCHAR(255)）、`published_at`、`deadline`、`deadline_date_only`、`education_required`、`job_nature`。
- **风险**：`nowcoder` 长度 8，`source_scope` 的 board 规格远小于 128 B；`external_id` 14 位远小于 255 B。无需 ALTER、无回填、无索引。
- **未来**：若要把聚合岗的外部 Moka 链接保存为 `apply_url`，需要新增可空列（无回填、无索引）并更新 docs/03 §9.4 的 ALTER 约定；那是单独的计划，不在本计划。
- **回滚**：`git revert` 本阶段提交；从 `config.yaml` 移除 nowcoder source；如需清理已发布行，`DELETE FROM jobs WHERE source='nowcoder'`（仅限隔离库/经 owner 确认；生产库不做自动清理）。无 schema 回滚。

## 14. 需要用户决策的事项

| # | 决策 | 建议 | 备选 |
|---|---|---|---|
| D1 | 是否以“robots 允许 + 免登录 + 无 CAPTCHA/签名绕过”为工程合规前提，接入牛客 | **批准**（ToS/法律接受由 owner 记录；不声称法律意见） | 要求书面许可/法务审阅后再做；或继续只做人工采集 |
| D2 | 首个 live smoke 用哪个 board | **`后端`**（20 条，覆盖上限与两种形态） | `C++|深圳`（12 条，更聚焦 C++ 岗位） |
| D3 | 跨来源去重范围 | **本计划只做批次内去重**；canonical 跨来源另立计划 | 本计划就加 canonical identity（需要 schema/合同变更，风险与工期显著增加） |
| D4 | 是否同时做 `apply_url` | **不做**，另立计划 | 本计划加可空列 + 解析 `jobDetailUrl` + API/UI（扩大范围） |
| D5 | 是否使用 `nowcoder-search` CLI（bun）过渡 | **不使用**（VM 无 bun/CLI，无法复现） | owner 提供 CLI 位置/版本后，作为只读探索工具，不作为计划依赖 |
| D6 | 是否现在加 `--json` 输出 | **暂不**；`--dry-run` 已可审阅 | 本计划加 `--json`（agent 友好，但扩大 CLI 面） |
| D7 | live smoke 是否只写隔离栈 | **是**（独立 compose、独立库/端口/卷，用后拆除） | 写开发库（不推荐） |
| D8 | 默认是否启用 nowcoder | **`enabled: false`**，owner 显式开启 | 默认启用（每次 sync 都会访问牛客，不推荐） |

## 15. 当前进度（实施时维护）

```text
P0 合同与来源登记        [x] docs/04 §5.5.1、source_adapters/README 登记表、README/AGENTS
P1 四处注册与共享层小改  [x] config/providers/C++ allowlist + diagnostics + 5s Fetcher
P2 provider 与 fixtures  [x] nowcoder.py + 8 个 fixture + 33 个新 Python 用例
P3 受控 live dry-run     [x] board=后端，20 jobs，diagnostics=0，complete=False
P4 隔离栈受控发布（V5）  [x] 单板 19 created；多板 2 板/7s/12 created；拆除证据已留
P5 文档同步 + L4 + 归档  [x] L4 已跑；ASan 1 例超时待 Supervisor 判定；CI 待推送后核验
```

已记录的证据（收口时补原始行与提交 sha）：

- P2：`Ran 479 tests ... OK`（source_adapters 全量；0017 记录为 446，新增 33）。
- P3：`[nowcoder] 后端: 20 job(s), board_complete=False, 1 batch(es),
  publish_complete=False, diagnostics=0 [dry-run]`。
- P4 单板：`created=19 updated=0 closed=0 complete=False diagnostics=1`
  （1 条 `duplicate_in_batch`）；DB 19 行、全部 `review_status=0`；公开列表
  `total=19`；detail `source_url`/`posted_at`/`deadline` 正确；review queue `total=0`。
- P4 多板：`后端` 19 行（2 created + 17 updated）+ `C++|深圳` 12 created，
  两块合计 7s（5s 间隔生效）；DB 33 行；公开列表 `后端` total=23、`C++` total=16。
- P4 拆除：`docker compose -p jobpilot-nowcoder-0018 down -v` 删除 2 容器、1 卷、
  1 网络；剩余容器/卷为空；端口 18088/13318/16318 已释放；`TEARDOWN_OK`。

### P5 L4（唯一一次矩阵）

- 隔离：project `jobpilot-l4-0018`，MySQL `13418` / Redis `16418`，
  独立卷 `jobpilot-l4-0018_mysql_data`，scratch 库 `jobpilot_l4_0018_scratch`；
  起止 2026-10-19 03:10–03:37 CST；开发栈 `jobpilot-mysql`/`jobpilot-redis` 未被触碰。
- 主机探针：`test_bcrypt` **9363 ms**（阈值 14000 ms）。
- 构建：debug / asan / ubsan / service-release / root-release 全部 `rc=0`。
- CTest：Debug **312/312**（82.21 s），UBSan **312/312**（59.54 s），
  ASan **311/312**（100.51 s）。
- Sanitizer 诊断：Debug / ASan / UBSan 均为 **0**。
- Python：`source_adapters` **Ran 479 tests, OK**；`ai_worker` **Ran 190 tests, OK**。
- Review-backfill isolation：dev `[ SKIPPED ]`（6 passed + 1 skipped）；
  scratch `[ PASSED ] 7 tests`。
- 拆除：L4 结束后 project 容器/卷/网络全部移除，端口释放；开发容器仍在。

**ASan 异常（未写成通过）**：L4 的 ASan 段有 1 个用例失败
`ResumeHttpTest.EnforcesCrossUserIsolation`，异常文本 `registration failed`，
耗时 22.35 s，sanitizer 诊断 0。按 agent-workflow §3/§4 做定向复现：

- 单独重跑同一 ASan 用例（新隔离栈 `jobpilot-asan-0018`，13518/16518）：
  `[ PASSED ] 1 test`，1.76 s。
- 单独重跑另一个注册密集用例
  `ResumeHttpTest.ManagesTailoredArtifactsWithinTheConfirmationBoundary`：
  `[ PASSED ] 1 test`，3.36 s。
- 重跑完整 ASan ctest（同一隔离栈）：**311/312**，失败换成
  `ResumeHttpTest.ManagesTailoredArtifactsWithinTheConfirmationBoundary`
  （`registration failed`，36.15 s），sanitizer 诊断 0。
- 证据指向测试 harness 的 3 s 客户端接收超时
  （`service/tests/test_resume_http.cpp:103-105`）与宿主机/ASan 负载下的
  registration bcrypt 超时，而非 0018 改动的产品逻辑（0018 未触碰
  auth/resume 代码；Debug/UBSan 同用例全绿）。**最终 ASan 矩阵不是全绿**，
  这一点留给 Supervisor 判定：接受为 harness/environment 限制，或要求单独的
  测试超时修复计划。
- TSan 仍不在本阶段矩阵内（沿用 0017 决定）；未运行，不得写成通过。

## 16. 已知问题与后续

- **H-2（另立任务，不阻塞 0018）**：`docs/ai/project-state.md:557` 记录
  “Opt-in live smoke: Greenhouse 217, Lever 10, Ashby 160”，但仓库内没有原始证据
  （命令、日期、响应计数、日志）。处理方式二选一：
  1. 在隔离环境重跑三个 ATS 的 `--dry-run`/受控发布并记录原始输出；或
  2. 在该行显式标注 **unverified**，并在 `docs/quality/README.md` 说明“数字无原始证据，
     不得作为开源可信度证据引用”。
  本计划选择 **(2) 先标注，后续以独立任务（建议编号 0019）重跑**；0018 不修改这三个
  数字，也不把它们当作已验证事实。
- **跨来源 canonical 去重**：本计划只做批次内去重；同一条岗位在牛客/ATS/人工三条路径
  的内容级重复仍可能出现。需要 canonical identity（新列/新表/合同变更）时另立计划。
- **`apply_url`**：聚合岗详情 SSR 的 `jobDetailUrl`（Moka）本版不落库；另立计划。
- **详情页富化**：本版不抓详情页；若未来需要 `apply_url`/更完整字段，必须重新核对两种
  形态的 SSR 稳定性与请求预算。
- **`eduLevel` 编码**：原生岗的 `eduLevel=5000` 等编码未验证；本版一律 omit，不猜。
- **ToS/法律**：本计划只记录 robots/免登录/低频/无绕过；不构成法律意见，ToS 接受由
  owner 记录。
- **CI Actions minutes**：owner-only，本计划不读账单。

## 17. 批准记录

本计划已于 2026-10-19 经 owner 批准（附加条件见文件头）。批准后的动作：

1. `docs/ai/CURRENT_STATE.md` 已归位；本文件已转为 active ExecPlan 并提交。
2. 按 P0 → P5 实施；每个切片先更新合同/文档，再改代码，按 L1/L2/L3 最小验证。
3. 阶段收口运行唯一一次 L4，并记录原始 pass 行、sanitizer 结果、CI run id。
4. 收口报告附：验收命令输出、L4 结果、robots 证据文件、隔离栈拆除证据。
5. 不修改 P1、不新增 schema、不引入自动投递、不提交任何密钥。
