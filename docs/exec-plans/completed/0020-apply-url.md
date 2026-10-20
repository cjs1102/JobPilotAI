# ExecPlan 0020 — `apply_url` 端到端

> 状态：**COMPLETED（2026-10-20 合并；VERDICT-0020-closeout PASS 附条件）**
> 分支：`feat/0020-apply-url`
> 合并：`c07c35a`（PR #3 merge commit）；最终 head `e01e283`
> 基线：`main@1a9a715`（0019 post-merge 状态提交）
> 时间盒：2–3 个工作日；到点提交收口报告。
> 收口条件：合并后 main 的 `ci` + `image` 双绿；PR 上 `image` 未触发属预期；
> DB 测试在无 `MYSQL_PASSWORD` 时以 SetUp exception/SEGFAULT 失败而非可读
> SKIP，记录为已知非阻塞测试环境行为；下一阶段 0021（docs/03–04 对账）
> 由 owner 指定，worker 不得自行启动。详见 `VERDICT-0020-closeout.md`。
> 批准后先更新合同/文档，再按 P0→P5 分阶段实施；每阶段按最小验证级别跑测试。

## 1. 背景与当前状态

`source_url` 已经是每个岗位的原始公告/详情页，并在 0015A 进入列表与详情响应；但
**“实际申请链接” `apply_url` 不存在**（handoff §5 row 5 / §7 item 2）。当前：

- `jobs` 表没有 `apply_url` 列；`database/schema/03_job.sql` 与 `docs/03` §3 是
  schema 的唯一事实来源。
- `POST /internal/jobs/import` 与 `POST /internal/jobs/source-sync` 的 job 对象都没有
  `apply_url`；`JobImportData` 与 `JobSummary`/`JobDetail` 也没有该字段。
- `GET /api/v1/jobs`、`/jobs/recommended`、`/jobs/{id}` 不返回 `apply_url`。
- 四个来源的可用性不同（见 §5）：Lever/Ashby 的公开 API 有显式 `applyUrl`；
  Greenhouse 只有 `absolute_url`（公告页）；nowcoder 原生岗只有可选的
  `redirectExternalUrl`，聚合岗的 Moka `jobDetailUrl` 只在详情 SSR 中。
- 迁移机制：项目**故意没有 migration runner / schema-version 表**（0016 §11
  decision 7）；既有库升级靠 `docs/03` §9.4 的 additive `ALTER`，由人阅读执行；
  `database/schema/README.md` 记录了升级表和“重复执行 duplicate column 属预期”。
  `backend/tests/test_schema.cpp` 已在 live/fresh/pre-0015 库上断言 schema；CI
  只加载 fresh schema，没有自动验证 upgrade 路径。handoff §9 把“无迁移工具”列为
  技术债，并要求在第一次真实 schema 变更（`apply_url`）前做出选择。

## 2. 目标

1. 为 `jobs` 增加可空 `apply_url`，不 backfill，不加索引；既有行保持 NULL。
2. 让 `import` 与 `source-sync` 两条写入路径接受可选 `apply_url`，并让
   `apply_url` 的 upsert 语义与 `published_at` 一致：省略不清空已有值
   （`COALESCE(VALUES(apply_url), apply_url)`）。
3. 在公开列表、推荐、详情响应中返回 `apply_url`（nullable），保持既有键不变。
4. 只从各来源**显式提供的字段**填充 `apply_url`；拿不到就保持 NULL，不推导、
   不拼接、不猜。
5. 为 schema 变更提供**可执行的 forward/backward 测试**，并在 CI 中强制。
6. 按阶段收口：L4、守卫、文档同步、CI，写 REQUEST 并等待 VERDICT。

## 3. 非目标（本计划明确不做）

- **不做自动投递/自动提交/表单填写**；`tools/checks/no_submission_path.py` 必须
  继续 rc=0。
- **不碰 P1**：`backend/{include,src}/jobpilot/{network,http}/` 不动。
- **不做 UI**：本次只提供 API 字段；最小 Web UI 是后续阶段。
- **不为 nowcoder 聚合岗抓详情页**：`jobDetailUrl` 只在详情 SSR 中，抓取会改变
  0018 的“一板一次列表请求”预算；本计划保持 NULL，另立后续计划。
- **不新增来源**，不改 greenhouse/lever/ashby/nowcoder 的既有字段语义。
- **不做 backfill**，不推导 `apply_url = source_url`，不把公告页伪装成申请页。
- **不改 workflow 触发器**；只允许在既有 `integration` job 中增加一个迁移测试步骤
  （见 §6.1）；不改 `on:`。
- **不改写 git 历史**、不 force push、不直接推 main（合并由 owner 执行）。

## 4. 约束与不变量

- `source_url` 仍是 `uk_source_url` 幂等键；`apply_url` 不参与唯一键、不建索引。
- `apply_url` 只存显式 http(s) URL；最大 **512 UTF-8 bytes**；非法/超长则拒绝
  写入（import/source-sync 返回可诊断错误）或省略（provider 输出），**绝不截断**。
- `apply_url` 是展示字段，不参与 freshness/lifecycle/质量门/排序；不抓取、
  不跟随重定向、不校验目标可达性（避免 SSRF/额外请求）。
- API 只新增一个 nullable 键；既有键的名称、类型、顺序和语义不变。
- 迁移/升级 SQL 必须可在 pre-0020 库上重复演练；禁止把已应用块改成新语义。
- 所有 schema/API 变更先更新 `docs/03`/`docs/04`，再改代码。
- 不提交 token/密钥；不使用登录墙/自动登录。

## 5. 研究证据（2026-10-19）

### 5.1 四个来源实际可拿到的字段

| 来源 | 公告页（`source_url`） | 显式申请链接字段 | 本计划行为 | 依据 |
|---|---|---|---|---|
| Greenhouse | `absolute_url` | **无**；公开 board API 的 job 对象只有 `absolute_url` 等，没有 `applyUrl` | `apply_url = NULL`；不合成 `/apply`/`#app` | `providers/greenhouse.py`、fixture `greenhouse_board.json` 的键 |
| Lever | `hostedUrl` | **`applyUrl`**（如 `.../apply`） | 有则写入；缺失则 NULL | `providers/lever.py`、fixture `lever_postings.json` |
| Ashby | `jobUrl` | **`applyUrl`**（如 `.../application`） | 有则写入；缺失则 NULL | `providers/ashby.py`、fixture `ashby_board.json` |
| nowcoder 原生岗 | nowcoder 详情页 | `redirectExternalUrl`（可为 `null`；2026-10-19 样本为 `None`）；`deliverId`/`allowDeliverCount` 是站内投递标记，不是 URL | 仅当 `redirectExternalUrl` 是非空绝对 http(s) URL 时写入；否则 NULL；**不把 nowcoder 详情页当 apply_url** | 2026-10-19 只读探针（3 请求，诚实 UA，5s 间隔） |
| nowcoder 聚合岗 | nowcoder 详情页 | 列表只有 `data.router`（nowcoder 深链，不是申请页）；详情 SSR 有 `jobDetailUrl`（Moka，如 `https://app.mokahr.com/...`） | **本计划不抓详情页 → NULL**；`data.router` 不当作 apply_url；后续计划可加有界详情抓取 | 2026-10-19 探针；0018 的“一板一次列表请求”约束 |
| 人工采集 | capture 的 `source_url`/`page_url` | 当前 bookmarklet/artifact **没有**独立 apply 字段 | import payload 接受可选 `apply_url`，但当前 bookmarklet 不提取；manual 行保持 NULL，除非未来 artifact 增加显式字段 | `capture/artifact.py`、`bookmarklet.js` |

**如实性结论**：0020 预计只有 **Lever / Ashby** 会普遍拿到非空 `apply_url`；
nowcoder 原生岗只在来源显式给 `redirectExternalUrl` 时才有；Greenhouse 与
nowcoder 聚合岗为 NULL。计划文档、API 示例和收口报告都必须这样写，不得宣称
“四个来源都有 apply_url”。

### 5.2 当前 schema/迁移实践

- `database/schema/*.sql` 是 fresh install 的完整 DDL；不存在版本表/runner。
- 既有库升级：`docs/03` §9.4 的 additive `ALTER`，重复执行报 duplicate column
  属预期；`database/schema/README.md` 维护升级表。
- `backend/tests/test_schema.cpp` 在 live、fresh、pre-0015 migrated 库上断言列/索引；
  CI 只加载 fresh schema，未自动测试 upgrade 路径。
- 0016 §11 decision 7 明确“no migration runner and no schema-version table”。

### 5.3 当前代码落点（供实施阶段列文件）

- 写入：`JobImportData`（`job_ingestion_dao_interface.h`）、
  `JobIngestionDao::UpsertJob`、`SourceSyncDao` 的 upsert、
  `internal_job_import_controller.cpp`、`internal_source_sync_controller.cpp`。
- 读取：`JobSummary`/`JobDetail`（`job_dao_interface.h`）、`job_dao.cpp` 的
  `kJobSummarySelect`/`QueryJobDetail`/`MakeJobSummary`、`job_controller.cpp` 的
  `SummaryToJson`/详情映射。
- 响应：列表/推荐/详情共用 `SummaryToJson` 与详情 JSON。
- Provider：`providers/{greenhouse,lever,ashby,nowcoder}.py` 的 `build_job` 调用。
- 合同：`docs/03` §3/§9.4、`docs/04` §5.1/§5.2/§5.3/§5.4/§5.5、
  `database/schema/README.md`。

## 6. 关键决策

### 6.1 迁移机制：选择 **B — 文档化 additive ALTER + CI 强制**

**决定**：不引入 migration runner / schema-version 表；新增一对受版本命名的
upgrade SQL 文件，并加一个 CI 强制测试：

```text
database/upgrades/0020_jobs_apply_url.up.sql
database/upgrades/0020_jobs_apply_url.down.sql
database/upgrades/testdata/pre_0020_jobs.sql   # frozen pre-0020 jobs DDL
tools/checks/test_schema_upgrade.sh
```

- `up.sql`：`ALTER TABLE jobs ADD COLUMN apply_url VARCHAR(512) DEFAULT NULL ...`。
- `down.sql`：`ALTER TABLE jobs DROP COLUMN apply_url`（仅测试/完整回滚；数据会丢）。
- `test_schema_upgrade.sh`：
  1. 在 **scratch database**（`jobpilot_upgrade_test_<pid>`）创建 pre-0020 `jobs` 表；
  2. 应用 `up.sql`，断言列存在、`VARCHAR(512)`、nullable、无索引；
  3. 应用 `down.sql`，断言列不存在；
  4. 再应用 `up.sql`，断言恢复；
  5. 重复执行 `up.sql` 时先检测列，报告 “already applied”，不算失败；
  6. 结束时 drop scratch database。
- CI：在 `.github/workflows/ci.yml` 的 `integration` job 的 schema load 之后加一步：

  ```yaml
  - name: Schema upgrade forward/backward test
    run: tools/checks/test_schema_upgrade.sh
  ```

  只增加步骤，不改 `on:` 触发器；scratch DB 使用 job-local MySQL，不碰
  `jobpilot_ci` 的业务表。
- `docs/03` §9.4 增加 0020 段并引用 `database/upgrades/0020_jobs_apply_url.up.sql`；
  `database/schema/README.md` 升级表增加 0020 行。

**为什么选 B（而不是 A：引入 tiny versioned runner）**：

1. 本轮只有**一个 nullable 列**；runner + `schema_migrations` + baseline + 校验
   + 测试的成本与风险大于收益（项目关键约束是个人 3–6 个月可完成）。
2. 0016 §11 decision 7 是 owner 已批准的决定；没有充分理由不应当在本轮反转。
3. 真正痛点是“手动 ALTER 无自动验证”；CI 的 forward/backward/up 测试直接解决
   该风险，而 runner 还会引入 schema 文件与 migrations 的 baseline 漂移问题。
4. `tests_schema.cpp` 已经覆盖 live/fresh/migrated 断言；本计划补的是 upgrade
   路径的自动执行，而不是重写整个 schema 管理。

**何时改为 A（记录触发条件）**：出现第二个 schema 变更、任何 destructive 变更、
多环境部署，或 upgrade 文件超过 3 个时，另立计划引入 tiny runner（`schema_migrations`
+ `baseline` + `up/down/status`），并把已应用的 upgrade 文件迁移为版本化 migrations。

**被否决的 A 方案摘要**：runner 不是不能做，而是本轮没有足够收益；若 reviewer 要求，
可按 `database/migrations/0001_jobs_apply_url.{up,down}.sql` + `schema_migrations`
+ `tools/db/migrate.sh` 的最小设计另案实施，但这会扩大 0020 的范围。

### 6.2 数据模型

```sql
ALTER TABLE jobs
    ADD COLUMN apply_url VARCHAR(512) DEFAULT NULL
        COMMENT '实际申请链接(0020; source_url 是原始公告页)';
```

- 可空、无默认值（`DEFAULT NULL`）、无 backfill、无索引；既有行 NULL。
- 为什么 512：已知显式 apply URL（Lever/Ashby/Moka）都远小于 255；512 字节给
  查询参数留余量，同时避免 `TEXT` 的无界性和额外溢出页。写入前按 UTF-8 字节
  校验，超限拒绝/省略，绝不截断。
- `source_url` 仍是唯一键；`apply_url` 不唯一、不参与去重。
- `apply_url` 不参与 freshness、quality gate、排序、deadline 生命周期。

### 6.3 写入语义

- `POST /internal/jobs/import`：新增可选 `apply_url`；必须为绝对 http(s)、
  ≤512 UTF-8 bytes；非法值使该 item 被拒绝（可诊断），不写入。
- `POST /internal/jobs/source-sync`：job 对象同样新增可选 `apply_url`；校验同上；
  provider 侧拿不到就省略该键。
- 两条 upsert 都使用 `apply_url = COALESCE(VALUES(apply_url), apply_url)`：
  省略不清空已有值（与 `published_at` 的 0017A 规则一致）。本计划不提供“显式
  清空”语义；如未来需要，另加 `clear_apply_url` 字段/计划。
- 人工导入不会因为 `apply_url` 缺失而清空；manual 行仍可被人工导入更新其他字段。

### 6.4 API 契约与向后兼容

- 新增响应键 `apply_url`（string|null），始终存在：
  - `GET /api/v1/jobs` 的每行；
  - `GET /api/v1/jobs/recommended` 的每行；
  - `GET /api/v1/jobs/{id}` 的详情。
- 既有键的名称/类型/语义/顺序不变；客户端忽略未知键即可兼容。
- **兼容性说明**：0015B 的“默认列表 payload 与旧版逐字节一致”保证会被本计划
  有意扩展为“既有键逐字节不变 + 新增一个 `apply_url` 键”；相关测试断言需要更新，
  并在 `docs/04` §5.1 明确记录，不把旧保证悄悄删掉。
- `docs/04` 更新点（逐条）：
  - §5.1 列表响应示例/字段说明：`apply_url` nullable；
  - §5.2 详情响应：`apply_url` nullable；
  - §5.3 推荐响应：与列表同形，新增 `apply_url`；
  - §5.4 import 请求示例：可选 `apply_url`、校验/COALESCE 语义；
  - §5.5 source-sync job 对象：可选 `apply_url`、校验/COALESCE 语义；
  - §1.2/§5.1 的兼容性说明：additive key；旧客户端忽略。
- `docs/03` 更新点：§3 jobs DDL、§9.4 升级块、§9.3 不变量（nullable/no index）、
  §5 索引策略（明确不加索引）。
- `database/schema/03_job.sql` 加入列；`database/schema/README.md` 升级表加 0020。
- `README.md` 能力清单把 `apply_url` 从“未实现”移到已实现，并写明来源覆盖差异。

### 6.5 Provider 填充（只写显式字段）

| Provider | 实现 | 拿不到时 |
|---|---|---|
| Greenhouse | 不读取任何“apply”字段；保持 NULL | NULL（`absolute_url` 只作为 source_url） |
| Lever | `build_job(apply_url=clean_text(raw.get("applyUrl")))`；校验绝对 http(s) | 省略 → NULL |
| Ashby | 同上，读取 `applyUrl` | 省略 → NULL |
| nowcoder 原生 | `apply_url = redirectExternalUrl` 仅当非空、绝对 http(s)；否则省略 | NULL；不把详情页/`deliverId` 当 URL |
| nowcoder 聚合 | 本计划不抓详情；`data.router` 不当作 apply_url | NULL；记录为后续（0021）候选 |
| manual | import API 接受可选字段；bookmarklet 不新增提取 | NULL |

Provider 输出必须经过与 `source_url` 相同的 URL 校验；超长/非 http(s) 则省略该
字段（不拒绝整个 board），并在 `diagnostics` 记 `apply_url_unusable`（如果 provider
已有 diagnostics；否则在收口报告说明）。

## 7. 实施阶段

### P0 — 合同与 schema upgrade 文件（文档先行）

- `docs/03` §3/§9.4/§9.3/§5、`docs/04` §5.1–5.5、`database/schema/README.md`、
  `database/schema/03_job.sql`、`database/upgrades/0020_*`、`testdata/pre_0020_jobs.sql`。
- 验收：`git diff --check`；docs/03 与 schema 的列定义一致；upgrade 文件可在
  scratch 库上手动执行。

### P1 — 写入链路与 DAO

- `JobImportData.applyUrl`；import/source-sync 解析器；两个 DAO 的 INSERT/UPDATE
  + `COALESCE`；`test_job_ingestion_dao`、`test_source_sync_dao`。
- 验收：定向 DAO 测试；`ctest -N` 计数（若新增用例，记录）。

### P2 — 读取与 API

- `JobSummary.applyUrl`；`kJobSummarySelect`/`QueryJobDetail`/`MakeJobSummary`
  列索引调整；`SummaryToJson`/详情 JSON 加 `apply_url`；HTTP 测试。
- 验收：`test_job_http`、`test_job_ingestion_http`、`test_source_sync_http`；
  列表/推荐/详情 round-trip；null/value；既有键不变。

### P3 — Providers

- Greenhouse/Lever/Ashby/nowcoder provider + fixtures；manual 不新增提取。
- 验收：`test_providers`、`test_provider_nowcoder`；Lever/Ashby 非空；Greenhouse/
  nowcoder 聚合 NULL；nowcoder 原生 `redirectExternalUrl` 有/无两种 fixture。

### P4 — 迁移测试与 CI

- `tools/checks/test_schema_upgrade.sh`；CI `integration` 步骤；README/docs 同步。
- 验收：forward/backward/up/idempotent；scratch DB 清理；CI 步骤本地可复现。

### P5 — 收口

- 全量守卫、唯一一次 L4（Debug/ASan/UBSan + Python + Release builds）、文档同步、
  推送 `feat/0020-apply-url`、owner 开 PR、核对 `ci`（image 视路径过滤）；写
  `REQUEST-0020-closeout.md` 并停止。

## 8. 测试计划

### 8.1 DAO

- `JobIngestionDaoTest`：插入带 `apply_url`；省略时 insert NULL；已有值时省略
  不清空（COALESCE）；提供新值时覆盖；`source_url` 幂等键不变。
- `SourceSyncDaoTest`：同上；F14 manual 行仍被整条跳过（apply_url 不改变 F14）。
- `JobDaoTest`：列表/详情 SELECT 返回 apply_url；NULL 时为 nullopt。

### 8.2 HTTP round-trip

- `test_job_ingestion_http`：导入 `apply_url` → 详情/列表读回一致；省略 → null；
  非法/超长 → 400 + diagnostic；第二次省略不影响已存值。
- `test_source_sync_http`：source-sync 同样 round-trip；complete=false 不关闭岗位。
- `test_job_http`：列表/推荐/详情 JSON 含 `apply_url`；既有键不变；null/value。
- 兼容性测试：默认列表 payload 的既有键值与旧断言一致，只多一个键。

### 8.3 Fixtures

- Lever/Ashby：非空 `applyUrl` → 非空。
- Greenhouse：NULL；不得从 `absolute_url` 推导。
- nowcoder 原生：`redirectExternalUrl` 非空 fixture → 非空；`None` fixture → NULL。
- nowcoder 聚合：NULL；`data.router` 不被使用。
- manual：artifact 无 apply 字段 → NULL；import payload 可手工提供（API 测试）。

### 8.4 迁移（forward/backward）

- `test_schema_upgrade.sh`：pre-0020 jobs 表 → up → assert → down → assert →
  up → assert；重复 up 先检测列；scratch DB drop。
- `test_schema.cpp`：live/fresh 库断言 `apply_url` 的 nullable/类型/无索引。
- CI：`integration` job 新增 upgrade 测试步骤；失败即红。

### 8.5 计数与守卫

- 若新增 CTest 用例：同步刷新 `docs/quality/test-merge-map.json` 与
  `docs/quality/test-overlap-audit.md` 的 counts；否则优先把断言加进既有用例，
  保持 312。
- Python `source_adapters` 测试计数会变化；记录新的 `Ran N tests`。
- `release_preflight`、`no_submission_path`、`history_secret_scan`、
  `test_coverage_map`、`git diff --check` 全绿。

## 9. 验收命令清单

```bash
# 迁移 forward/backward（scratch DB）
tools/checks/test_schema_upgrade.sh

# C++ 定向
cmake --build build/presets/debug -j"$(nproc)"
ctest --test-dir build/presets/debug \
  -R 'SchemaTest|JobIngestion|SourceSync|JobHttp' --output-on-failure
ctest --test-dir build/presets/debug -N | tail -1

# Python providers
cd source_adapters
python3 -m unittest jobpilot_sources.tests.test_providers \
  jobpilot_sources.tests.test_provider_nowcoder -v

# 守卫
cd ..
python3 tools/checks/test_coverage_map.py \
  --test-dir build/presets/debug --map docs/quality/test-merge-map.json
python3 tools/checks/release_preflight.py
tools/checks/history_secret_scan.sh
python3 tools/checks/no_submission_path.py
git diff --check

# 阶段收口：唯一一次 L4（Debug/ASan/UBSan + Python + Release builds）
# 通过既有 build/l4-* 模式，独立 project/端口/卷，用后拆除。
```

## 10. 回滚

- **首选（生产）**：revert 0020 的代码提交；`apply_url` 列保留，旧代码忽略它，
  无数据丢失；API 不再返回新键。然后视需要再做 schema down。
- **完整 schema 回滚**：应用 `database/upgrades/0020_jobs_apply_url.down.sql`
  （`DROP COLUMN apply_url`）。这会**丢失已存的 apply_url 值**，只在测试或明确
  决定放弃该字段时使用；执行前备份/确认。
- 迁移测试脚本在 scratch DB 上演练 down，不碰开发库。
- 文档/合同回滚：revert 0020 docs 提交；`docs/03`/`docs/04` 的 0020 段随代码一起
  回滚，不留半套契约。

## 11. 风险

| 风险 | 影响 | 缓解 |
|---|---|---|
| 手工 ALTER 与 schema 文件漂移 | 环境间 schema 不一致 | CI upgrade 测试 + `test_schema` live/fresh 断言 + README 升级表 |
| 迁移 down 丢数据 | 回滚时丢失 apply_url | 首选代码 revert 保留列；down 仅测试/明确放弃 |
| 512 字节不够 | 个别 URL 被省略 | 超限省略并诊断；如真实样本超限，再评估 1024（需合同变更） |
| 把公告页误当申请页 | 用户体验/语义错误 | 只接受显式字段；Greenhouse/nowcoder 聚合写 NULL |
| URL 不可信（恶意/钓鱼） | 未来 UI 点击风险 | 只存 http(s)；不抓取；UI 阶段单独做安全提示/域名展示 |
| nowcoder 聚合无 apply_url | 覆盖不完整 | 明确记录；0021 可加有界详情抓取（不在本计划） |
| API 新增键破坏逐字节断言 | 测试红/兼容性争议 | 明确 additive 兼容政策；更新断言为“既有键不变 + 新键” |
| CTest 用例数变化 | coverage map 失败 | 优先加断言到既有用例；新增用例则刷新 map/audit |
| CI workflow 改动 | 流程风险 | 只加 integration 步骤；不改触发器；本地先跑脚本 |
| 迁移测试使用开发库 | 数据破坏 | 强制 scratch database + drop；隔离规则 |

## 12. 待决策问题

1. **迁移机制**：批准 B（文档化 ALTER + CI 强制），还是要求 A（tiny versioned
   runner）？推荐 B；若选 A，本计划的范围和工期需要重估。
2. **nowcoder 聚合岗**：0020 保持 NULL（推荐），还是在本计划加入有界详情抓取？
   后者会增加每板最多 N 次详情请求和 5s 间隔预算，建议另立 0021。
3. **`apply_url` 长度**：512（推荐）还是 1024？如无真实超限样本，512 足够。
4. **API null 表示**：始终返回 `"apply_url": null`（推荐），还是为 null 时省略键？
   始终返回更易被客户端处理，但会改变每个 payload 的字节。
5. **manual 采集**：是否在本计划给 bookmarklet/artifact 增加显式 `apply_url`
   字段？推荐不做（避免启发式提取），仅让 import API 接受可选字段。
6. **CTest 计数**：优先把断言加进既有用例（保持 312），还是新增专用用例并刷新
   coverage map？推荐前者，迁移脚本单独作为 CI step。

## 13. 当前进度

```text
P0 合同 + upgrade 文件    [x] schema/upgrade/testdata/CI step/docs
P1 写入链路 + DAO         [x] applyUrl + COALESCE + boundary validation
P2 读取 + API             [x] list/recommended/detail apply_url
P3 Providers              [x] Lever/Ashby explicit; nowcoder redirect; Greenhouse null
P4 迁移测试 + CI          [x] upgrade forward/back/re-forward/idempotent
P5 L4 + 守卫 + REQUEST    [x] L4 green; CI pending PR
```

Evidence (2026-10-19):

```text
# upgrade script (scratch DB)
forward: varchar(512)|YES|<null>; no index
backward: absent
re-forward: present
idempotent up: "apply_url already exists; skipped"
idempotent down: "apply_url absent; skipped"
SCHEMA_UPGRADE_OK

# targeted C++
ctest -R 'SchemaTest|JobIngestion|SourceSync|JobHttp'
100% tests passed, 0 tests failed out of 72

# full Python
Ran 481 tests ... OK
CTest count: 312 (unchanged)

# L4: project jobpilot-l4-0020, MySQL 14018 / Redis 17018,
# scratch jobpilot_l4_0020_scratch, 14:12:14–14:28:15
probe: test_bcrypt 6142ms
debug: 100% tests passed, 0 failed out of 312 (67.34 s)
asan:  100% tests passed, 0 failed out of 312 (88.80 s)
ubsan: 100% tests passed, 0 failed out of 312 (59.20 s)
sanitizer diagnostics: 0 / 0 / 0
builds: debug/asan/ubsan/service-release/root-release rc=0
python: sources 481 OK; ai_worker 190 OK
review: dev SKIPPED (6 passed); scratch PASSED 7
teardown: project containers/volumes removed; ports free; dev containers untouched
```

## 14. 批准后动作

1. 把本计划的状态改为 ACTIVE，按 P0→P5 实施；每阶段先更新合同/文档。
2. 任何 schema/API 变更先改 `docs/03`/`docs/04`，再改代码。
3. 阶段收口运行唯一一次 L4，写 `REQUEST-0020-closeout.md`，停止等待 VERDICT。
4. 不合并、不开始下一阶段；合并由 owner 执行。
