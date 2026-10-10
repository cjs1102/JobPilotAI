# ExecPlan 0017A: 手工导入 `published_at` 契约缺口（最小向后兼容修复）

> Status: **Completed**（2026-10-10）。D1 决策（方案 a：把 `published_at` 纳入手工导入契约）
> 已实施并验证：代码提交 `35563da`，发布安全检查提交 `67197fa`（同一阶段顺带完成）。
> 本阶段只做这一处修复，未开启 0018。
> Task level: **L2/L3**（跨 Controller / DAO / API 契约 / 测试；不改 schema、不改
> `backend/{include,src}/jobpilot/{network,http}/`）。
> 依据：`docs/ai/agent-workflow.md` §2.1；`AGENTS.md`（API 契约变更必须同时更新 `docs/04`）。
> 基线 HEAD：`2055e05`（0017 已归档）。

## 1. 问题（0017-C 已记录的缺陷证据）

- `publish-capture` 把归一化后的 `published_at` 放进手工导入 payload
  （`source_adapters/jobpilot_sources/capture/publisher.py:220-224`）；
- `POST /internal/jobs/import` 的契约（`docs/04-api-design.md:599-625`）与解析器
  （`service/src/job/internal_job_import_controller.cpp:391-477`）都不含该字段，未知键被忽略；
- `JobIngestionDao::UpsertJob` 的 INSERT 列清单
  （`service/src/job/job_ingestion_dao.cpp:124-163`）同样没有 `published_at`；
- 观测：合成 capture（`published_at=2026-09-20 08:00:00`）发布成功（created=1）后，
  `GET /api/v1/jobs/{id}` 的 `posted_at` 为 `null`（0017 计划 §11）；
- 影响：人工采集岗位的发布日期不可读；`docs/04:488` 的"发布 N 天内"过滤要求
  `published_at IS NOT NULL`（`service/src/job/job_dao.cpp:217-218`），人工记录在该过滤下会被排除。

## 2. 决策

**D1（owner 授权范围内，本计划自行裁定）：采用方案 (a)——把 `published_at` 纳入手工导入
契约。** 理由：它是真正向后兼容的最小改动（可选字段；未知键本来就被忽略；`jobs.published_at`
列已存在，无需 schema 变更），并且让既有 publisher 的意图生效。方案 (b)（publisher 改发 note
并停止发送）会放弃已经采集到的信息，仅在 (a) 不可行时才考虑。

**写语义：** `ON DUPLICATE KEY UPDATE published_at = COALESCE(VALUES(published_at), published_at)`
——与 source-sync DAO 的既有语义一致（`service/src/job/source_sync_dao.cpp:154`）：新 payload
没带该字段时保留库中原值，绝不把已有发布日期清空。F14（人工记录不被观测覆盖）不在本次改动
范围内，必须保持原样。

## 3. 范围

**In scope**

1. `service/include/jobpilot/service/job/job_ingestion_dao_interface.h`：
   `JobImportData` 增加 `std::optional<std::string> publishedAt;`
2. `service/src/job/internal_job_import_controller.cpp` `ParseJob`：
   `GetOptionalString(value, "published_at", &job.publishedAt)`
3. `service/src/job/job_ingestion_dao.cpp` `UpsertJob`：列清单、占位符、`VALUES()` 更新
   （COALESCE）与参数
4. `docs/04-api-design.md` §5.2：请求示例补 `"published_at": "2026-09-20 00:00:00"`，并说明
   它可选、按 UTC 文本存储、缺省时 upsert 保留原值
5. 测试（**扩展既有用例、不新增 TEST 用例**，以保持 0017-B 审计过的 309 计数不变，
   Coverage map guard 因此不需要刷新计数）：
   - `service/tests/test_job_ingestion_dao.cpp`：创建时写入 `published_at`；再次 upsert 省略
     该字段时保留原值；带新值时覆盖
   - `service/tests/test_job_ingestion_http.cpp`：HTTP 导入接受该字段并可回读
6. `tools/dev/capture-acceptance.sh`：读回断言改为要求 `posted_at` 与 payload 的
   `published_at` 一致，删除原来的 `NOT_IN_IMPORT_CONTRACT` INFO 特例
7. 状态文档同步：`docs/ai/project-state.md` 的 "Defect found and carried forward" 改为
   "已由 0017A 修复"（指向本计划与提交）

**Out of scope**：schema/`docs/03` 变更；source-sync 行为与 F14；publisher 的归一化规则；
公开搜索的过滤语义；UI；0018。

## 4. 验收标准

- 重新编译受影响目标通过；下列二进制全绿：`test_job_ingestion_dao`、`test_job_ingestion_http`、
  `test_job_ingestion_service`、`test_source_sync_dao`（F14 不受影响）、`test_job_dao`、
  `test_job_http`；
- `ctest -N` 仍是 **309**，`python3 tools/checks/test_coverage_map.py --test-dir build/presets/release --map docs/quality/test-merge-map.json` 仍 PASS（未新增/删除用例）；
- `tools/dev/capture-acceptance.sh` 对合成样本端到端全绿，并新增 `posted_at` 往返断言；
- `python3 tools/checks/release_preflight.py` 仍 `7 pass / 0 fail / 0 skip`；
- `docs/04`、本计划、`project-state` 与提交信息三者一致。

## 5. 验证方式

L2/L3：`cmake --build build/presets/debug -j4` 后直接运行上列 6 个二进制（debug preset）；
再用 C 单元的隔离栈跑一次 `capture-acceptance.sh`（自证 `posted_at` 往返）。
不做全量 L4（阶段收口才做；本阶段收口时按 §2.2 复用本次证据 + 一次 debug 全量即可）。

## 6. 停止条件

1. 发现导入路径上还有别的地方依赖 `published_at` 恒为 NULL，或存在会被这次写入破坏的约束
   ⇒ 停下给证据，不猜；
2. 改动影响到 F14 / source-sync 语义 ⇒ 只保留"契约 + 解析 + DAO 写入"的最小集并回滚其余；
3. 若不得不新增 TEST 用例（计数变化）⇒ 同步刷新 `test-merge-map.json` 与审计报告的
   `audit-counts`，不得删测试或放宽校验。

## 7. 回滚

纯增量改动：`git revert` 本阶段的提交即可回到"忽略该字段"的行为；数据库列本就存在，
无需数据迁移，也不需要在 revert 时清理已有 `published_at` 值。

## 8. 实测证据（原始行）

**实现**：`service/include/jobpilot/service/job/job_ingestion_dao_interface.h`（`publishedAt`）、
`service/src/job/internal_job_import_controller.cpp` `ParseJob`（可选 `published_at`）、
`service/src/job/job_ingestion_dao.cpp` `UpsertJob`（列 + 占位符 + `COALESCE(VALUES(...))` 更新）；
`docs/04-api-design.md` §5.2 记录字段与写语义；`docs/03` 未改（列已存在，无 schema 变更）。

**定向测试**（阶段自有隔离栈 `jobpilot-0017a`，MySQL `:13409` / Redis `:16409`，用后即拆；
开发库 `jobpilot` 与 `jobpilot_review_scratch` 未被触碰）：

```text
$ cmake --build build/presets/debug -j4          # rc=0
$ MYSQL_PORT=13409 REDIS_PORT=16409 ./build/presets/debug/service/<binary>
test_job_ingestion_dao   rc=0  [  PASSED  ] 5 tests.    （含 插入写入 / 省略保留 / 覆盖 三段断言）
test_job_ingestion_http  rc=0  [  PASSED  ] 5 tests.    （含 入库列与公开 posted_at 断言）
test_job_ingestion_service rc=0 [  PASSED  ] 7 tests.
test_source_sync_dao     rc=0  [  PASSED  ] 14 tests.   （F14 语义未受影响）
test_job_dao             rc=0  [  PASSED  ] 9 tests.
test_job_http            rc=0  [  PASSED  ] 9 tests.
```

**README/契约守卫**：`ctest -N` 仍为 309（只在既有用例内加断言），
`python3 tools/checks/test_coverage_map.py --test-dir build/presets/release --map docs/quality/test-merge-map.json`
→ `PASS (309 -> 309)`；`python3 tools/checks/release_preflight.py` → `8 pass / 0 fail / 0 skip`。

**负控制（证明新断言真的会失败）**：用**修复前**的 `jobpilot-backend:verify` 镜像跑
`tools/dev/capture-acceptance.sh`：

```text
dry run: clean=1 payload_fields=...,published_at,...
=== detail read-back ===
published_at round-trips: got "None == '2026-09-20 00:00:00'"
FAIL: the public detail does not match the published payload
```

**CI（runner 上的独立验证，`tools/dev/ci-status.sh` 取回）**：

```text
35563da ci    success 5m46s  clean build 3m58s + full test suite (MySQL + Redis) 1m40s
35563da image success 3m17s  （提交版 Dockerfile 在 runner 上构建 + demo gate）
67197fa ci    success 5m24s  clean build 3m59s + full test suite 1m18s
67197fa image success 2m23s
```

**端到端正向验证（修复后的镜像）**：用当前源码在本地重建验证镜像
`docker build -f build/Dockerfile.verify -t jobpilot-backend:0017a .`（`image_build_rc=0`；
Docker Hub 不可达，故用 0016 P1b 的 scratch Dockerfile，与提交版 Dockerfile 同一套命令），
再跑 `JOBPILOT_BACKEND_IMAGE=jobpilot-backend:0017a tools/dev/capture-acceptance.sh
--capture docs/examples/capture-acceptance-sample.json --keep-evidence build/0017a/section-19.5-evidence.txt`：

```text
3 legs before it unchanged … publish (created=1 updated=0 total=1)
dry run: clean=1 payload_fields=…,published_at,…
=== detail read-back ===
  published_at round-trips: "'2026-09-20 00:00:00' == '2026-09-20 00:00:00'"
  fields_compared=17
=== F14 (a colliding source observation must not win) ===
PASS: F14: …
legs passed: 9
CAPTURE_ACCEPTANCE_OK
```
与上面同一脚本、同一断言的**负控制**（旧镜像）失败 ⇒ 断言有效；正负两侧合起来即
"契约缺口已闭合"的完整证据。
