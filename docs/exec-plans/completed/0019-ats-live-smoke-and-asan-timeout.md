# ExecPlan 0019 — ATS live-smoke evidence + sanitizer-aware HTTP test timeout

> 状态：**COMPLETED（2026-10-19 合并；VERDICT-0019 PASS 附条件）**
> 分支：`feat/0019-ats-live-smoke`
> 合并：`0c24c45`（PR #2 merge commit）；最终 head `c945eea`
> 基线：`main@cc9b333`（0018 的 merge commit；PR #1 已由 owner 合并）
> 时间盒：2–3 个工作日；到点提交收口报告，不做无限打磨。
> 收口条件：UBSan 宏缺口接受；ATS 以 2026-10-19 快照 `232/10/167` 为证据
> （旧 `217/10/160` historical/unverified）；历史 home path ×10 record-only；
> 合并后 main 的 `ci` + `image` 双绿；下一阶段由 owner 指定。

## 1. 背景与当前状态

0018 已合并，`nowcoder` 成为第一个国内来源。VERDICT-0018 为 PASS，附两个强制条件：

1. **ASan 311/312 接受为 harness/environment-limited**，但：
   - 任何文档/报告不得写成 “ASan 全绿”，必须写 311/312 + 原因；
   - **0019 必须包含**「sanitizer-aware HTTP 测试客户端超时」修复，并重跑 ASan；
   - 保留残余不确定性说明。
2. **历史 home path 计数 7 → 10**（新增提交导致；仅历史，当前树干净）；在
   `docs/ai/project-state.md` 与 0019 中记录；禁止改写历史，处置由 owner 决定。

另外，H-2 仍未解决：`docs/ai/project-state.md` 记录
“Opt-in live smoke: Greenhouse 217, Lever 10, Ashby 160”，但仓库内没有原始
命令/日期/输出证据；handoff 称没有 live board run。该数字影响开源可信度，因此
VERDICT-0018 把「ATS live smoke 重跑并记录原始输出」列为 0019 的下一任务。

## 2. 目标

1. **H-2**：用现有 ATS 适配器对 Greenhouse / Lever / Ashby 的公开 board 做一次
   `--dry-run` live smoke，记录日期、命令、board、原始输出与计数；把结果写入
   `docs/ai/project-state.md`，替换/标注旧的未验证数字。
2. **ASan 超时修复**：把 HTTP 测试客户端的 2 s / 3 s `SO_RCVTIMEO` 改为
   sanitizer-aware（ASan/TSan/UBSan 下放大），覆盖 `service/tests/` 与
   `backend/tests/` 的所有 HTTP 测试客户端；**不弱化任何断言**。
3. **ASan 重跑**：在隔离栈上重跑完整 ASan CTest（312），记录原始 pass 行；
   若仍失败，按 §3/§4 分类并如实报告。
4. **阶段收口**：唯一一次 L4（Debug/ASan/UBSan + Python + Release builds）+
   守卫检查 + 文档同步；按监督协议写 `REQUEST-0019-closeout.md` 并停止等待
   VERDICT。
5. 记录 0018 合并状态、main 的 `ci`/`image` 证据、ASan 条件、历史计数漂移到
   `docs/ai/project-state.md`。

## 3. 非目标

- 不新增来源、不改 ATS provider 的生产逻辑、不改 `nowcoder`。
- 不修改 workflow 触发器；不修改 P1 `backend/{include,src}/jobpilot/{network,http}/`。
- 不改 schema、Dockerfile、compose；不引入自动投递/登录墙自动化。
- 不把 217/160 条 ATS 岗位发布到开发库；H-2 的证据是 **dry-run 计数**，不是
  一次全量入库。
- 不改写 git 历史；不 force push；不直接推 main。
- 不通过降低断言、跳过测试、缩小矩阵来让 ASan 变绿。

## 4. 约束与不变量

- 分支：`feat/0019-ats-live-smoke`；main 保持绿；未经 VERDICT 不合并。
- ATS live smoke：公开 board、诚实 UA、低量（每个 board 一次请求）、`--dry-run`
  （不写库）、记录日期/命令/输出；失败就记录失败，不伪造计数。
- 测试超时：仅改测试 harness 的等待时间；所有 `EXPECT_*` / `ASSERT_*` 不变。
- ASan 重跑：独占、串行、隔离栈；不与其他构建/E2E 并发。
- L4：每阶段一次，按 `docs/ai/agent-workflow.md` §2.2；L4 前跑 `test_bcrypt`
  探针，>14 s 拒绝运行。
- 历史计数：只记录，不改写；owner 决定处置。
- 不提交 token/密钥；live smoke 使用 dummy token（dry-run 不需要真实 token）。

## 5. 研究证据

### 5.1 0018 ASan 失败（已记录在 REQUEST-0018）

```text
asan: 99% tests passed, 1 failed out of 312
ResumeHttpTest.EnforcesCrossUserIsolation: "registration failed", 22.35 s
sanitizer diagnostics: 0
```

定向复现（0018 收口时）：

- 同一 ASan 用例单独重跑：PASS 1.76 s；
- 另一个 registration 密集用例单独重跑：PASS 3.36 s；
- 完整 ASan 重跑：311/312，失败漂移到
  `ResumeHttpTest.ManagesTailoredArtifactsWithinTheConfirmationBoundary`
  （`registration failed`，36.15 s），0 sanitizer 诊断；
- 测试客户端接收超时：`service/tests/test_resume_http.cpp:103-105` 为 3 s；
  ASan `test_bcrypt` 3.48 s；失败瞬间的服务端 bcrypt 耗时未打点（残余不确定性）。

**结论**：harness/environment timeout，不是 0018 产品回归；但 ASan 未全绿，
0019 必须修复并重跑。

### 5.2 HTTP 测试客户端超时清单

`grep -rn 'SO_RCVTIMEO|timeout.tv_sec' service/tests backend/tests`：

| 文件 | 当前超时 |
|---|---|
| `service/tests/test_resume_http.cpp` | 3 s |
| `service/tests/test_job_http.cpp` | 3 s |
| `service/tests/test_job_ingestion_http.cpp` | 3 s |
| `service/tests/test_job_review_http.cpp` | 3 s |
| `service/tests/test_source_sync_http.cpp` | 3 s |
| `service/tests/test_source_sync_e2e.cpp` | 3 s |
| `service/tests/test_application_http.cpp` | 3 s |
| `service/tests/test_ai_http.cpp` | 3 s |
| `service/tests/test_user_http.cpp` | 2 s |
| `backend/tests/test_http_server.cpp` | 2 s |

### 5.3 ATS live smoke 候选 board

使用现有适配器与 0017 记录相符的公开 board（`source_adapters/jobpilot_sources/tests/support.py`
的 `base_config` 使用同一组）：

| Provider | Board | Endpoint |
|---|---|---|
| Greenhouse | `gitlab` | `boards-api.greenhouse.io/v1/boards/gitlab/jobs?content=true` |
| Lever | `leverdemo` | `api.lever.co/v0/postings/leverdemo?mode=json` |
| Ashby | `ramp` | `api.ashbyhq.com/posting-api/job-board/ramp?includeCompensation=true` |

这些 board 的岗位数会随招聘季变化；本次记录的是 2026-10-19 的观测，不是永久
常量。

## 6. 选定设计

### 6.1 ATS live smoke（H-2）

- 临时配置（不提交）：
  ```yaml
  jobpilot:
    base_url: http://127.0.0.1:8080
    internal_token_env: INTERNAL_API_TOKEN
  sources:
    - {type: greenhouse, board: gitlab, company: GitLab, enabled: true}
    - {type: lever, board: leverdemo, company: Lever Demo, enabled: true}
    - {type: ashby, board: ramp, company: Ramp, enabled: true}
  ```
- 命令：
  ```bash
  cd source_adapters
  INTERNAL_API_TOKEN=dummy python3 -m jobpilot_sources sync \
    --config /tmp/0019-ats-live.yaml --dry-run
  ```
- 记录每条原始输出行到 `build/0019/ats-live-smoke.log`（gitignored），并把
  日期、board、计数、`board_complete`、batch 数写入
  `docs/ai/project-state.md` 与 0019 的证据段。
- 不写库；如果某个 board 失败，记录 HTTP/provider 错误并保留旧数字的
  `unverified` 标签，不伪造。

### 6.2 Sanitizer-aware HTTP test timeout

- 新增 `service/tests/http_test_timeout.h`（backend 测试树同样一个小头文件，
  或在 backend 测试文件中使用同一逻辑的本地常量）：
  ```cpp
  namespace jobpilot::test {
      constexpr int HttpTestTimeoutSeconds(int normal_seconds) {
  #if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__) || \
      defined(__SANITIZE_UNDEFINED__)
          return normal_seconds * 5;   // 3 s -> 15 s; 2 s -> 10 s
  #else
          return normal_seconds;
  #endif
      }
  }
  ```
  GCC defines `__SANITIZE_ADDRESS__` and `__SANITIZE_THREAD__`; it does **not**
  define a UBSan macro, so UBSan keeps the normal timeout (UBSan passed
  312/312 in both the 0018 and 0019 matrices). If a future UBSan timeout
  appears, add an explicit build definition rather than lowering assertions.
- 在 10 个测试文件中把 `timeout.tv_sec = N;` 替换为
  `timeout.tv_sec = jobpilot::test::HttpTestTimeoutSeconds(N);`，并加入对应
  `#include`。
- 头文件内加编译期断言，证明 sanitizer 构建确实放大：
  ```cpp
  #if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__) || \
      defined(__SANITIZE_UNDEFINED__)
  static_assert(HttpTestTimeoutSeconds(3) == 15);
  #else
  static_assert(HttpTestTimeoutSeconds(3) == 3);
  #endif
  ```
- **不改任何断言**；只改等待时间。

### 6.3 ASan 重跑与 L4

- ASan 重跑：隔离栈 `jobpilot-asan-0019`（MySQL 13818 / Redis 16818，独立卷），
  运行 `ctest --test-dir build/presets/asan --output-on-failure`，记录原始
  `NN% tests passed` 行与 sanitizer 诊断计数。
- 阶段 L4：`build/l4-0019/run.sh`，project `jobpilot-l4-0019`，端口
  13918/16918，scratch `jobpilot_l4_0019_scratch`；复用 0018 的矩阵结构
  （Debug/ASan/UBSan、service/root Release、两个 Python suite、
  review-backfill isolation、teardown 证据）。L4 前跑 `test_bcrypt` 探针。
- 如果 ASan 仍失败：保留原始失败行，按 harness vs product 分类，不重跑到绿；
  报告给 Supervisor。

## 7. 被否决的备选

| 备选 | 否决原因 |
|---|---|
| 把 217/160 条 ATS 岗位发布到隔离栈作为 H-2 证据 | 会扩大请求量/运行时间，H-2 只需要计数证据；`--dry-run` 已能复现计数 |
| 只修 `test_resume_http.cpp` 的 3 s | 其他 HTTP 测试客户端同样会在 ASan 下超时；修复应覆盖所有 10 个 |
| 全局无条件把超时改成 15 s | 会掩盖 Debug 下的真实慢响应；只在 sanitizer 构建放大 |
| 降低/跳过断言或把失败用例标记为 flaky | 违反 agent-workflow；verdict 明确禁止弱化断言 |
| 用 workflow 触发器/跳过 ASan 来变绿 | 不修改 CI 设计；ASan 必须重跑并如实报告 |
| 改写历史以消除 home path 计数 | 协议禁止；owner 决定 |

## 8. 实施阶段与验收

### P0 — 合并后状态与计划

- 更新 `docs/ai/project-state.md`：0018 merged at `cc9b333`（PR #1）；
  main 的 `ci`/`image` run id 与耗时（待 poll 完成后填入）；ASan 311/312
  条件记录；历史 home path 7→10 记录；0019 active。
- 写本计划到 `docs/exec-plans/active/0019-ats-live-smoke-and-asan-timeout.md`。
- 验收：文档 diff 自洽；`git diff --check`。

### P1 — ATS live smoke（H-2）

- 运行 §6.1 的 dry-run；保存原始日志；记录三个 board 的计数/完整性/批次。
- 更新 `docs/ai/project-state.md` 的 ATS live smoke 行（日期 + 计数 + 命令 +
  日志路径）；旧 217/10/160 标记为 historical/unverified，不删除。
- 验收：三条原始输出行；计数与日志一致；没有 POST/数据库写入。

### P2 — sanitizer-aware 超时修复

- 新增两个小头文件（service/backend），修改 10 个测试文件；不改断言。
- 验收：
  - `cmake --build build/presets/debug -j$(nproc)` rc=0；
  - `ctest -R 'ResumeHttpTest|JobHttpTest|UserHttpTest'` 通过；
  - `git diff --check` rc=0；
  - 检查 10 个测试文件都使用 helper（grep 无裸 `tv_sec = 3`/`= 2`）。

### P3 — ASan 重跑

- 隔离栈 `jobpilot-asan-0019`，跑完整 ASan CTest。
- 验收：`100% tests passed, 0 tests failed out of 312`；sanitizer 诊断 0；
  teardown 证据；若失败，保留原始行并分类。

### P4 — L4

- `build/l4-0019/run.sh` 完整矩阵；记录 debug/asan/ubsan pass 行、Python
  479+190、review isolation、teardown。
- 验收：三项 CTest 全绿（期望 ASan 312/312）；构建全 rc=0；sanitizer 诊断 0；
  开发栈未被触碰。

### P5 — 文档、守卫、REQUEST

- 更新 project-state、plan progress；运行 preflight/no-submission/history/
  coverage/diff-check；推送 `feat/0019-ats-live-smoke`；owner 开 PR 触发 `ci`
  （必要时 `image` 不触发，属正常）；核对 run id。
- 写 `REQUEST-0019-closeout.md`，停止等待 VERDICT；不合并、不开始下一阶段。

## 9. 验证与回滚

- 验证命令见 §8；L4 证据在 `build/l4-0019/`；ASan 重跑日志在
  `build/0019/asan-rerun.log`；ATS live smoke 日志在
  `build/0019/ats-live-smoke.log`。
- 回滚：`git revert` 0019 提交；测试超时修复是 test-only，回滚后恢复原超时；
  不涉及 schema/生产代码。ATS live smoke 不写库，无需数据回滚。
- 历史计数不回滚、不重写；只记录。

## 10. 当前进度

```text
P0 合并后状态 + 计划   [x] 0018 merged cc9b333; main ci/image green; plan written
P1 ATS live smoke      [x] Greenhouse 232 / Lever 10 / Ashby 167, all complete
P2 ASan 超时修复       [x] 10 HTTP test clients use HttpTestTimeoutSeconds; Debug rc=0
P3 ASan 重跑           [x] 312/312, 108.02 s, sanitizer diagnostics 0
P4 L4                  [x] Debug/ASan/UBSan 312/312; builds rc=0; 0 diagnostics
P5 文档/CI/REQUEST     [x] REQUEST-0019 + PR #2 ci 39203694811 green; merged 0c24c45
```

P0 证据：

```text
main merge: cc9b333 (PR #1, merge commit)
main ci:    39200537331 completed/success 5m36s (build 4m08s + suite 1m21s)
main image: 39200537315 completed/success 2m51s (container 2m47s)
```

P1 证据（`build/0019/ats-live-smoke.log`）：

```text
[greenhouse] gitlab: 232 job(s), board_complete=True, 2 batch(es), publish_complete=False, diagnostics=0 [dry-run]
[lever] leverdemo: 10 job(s), board_complete=True, 1 batch(es), publish_complete=True, diagnostics=0 [dry-run]
[ashby] ramp: 167 job(s), board_complete=True, 1 batch(es), publish_complete=True, diagnostics=0 [dry-run]
```

P2 证据：

```text
cmake --build build/presets/debug -j$(nproc)  -> rc=0
ctest -R 'ResumeHttpTest|UserHttpTest|JobHttpTest|ApplicationHttpTest|AiHttpTest'
  -> 100% tests passed, 0 tests failed out of 33
./build/presets/debug/backend/test_http_server -> rc=0
```

P3 证据（`build/0019/asan-rerun.log`）：

```text
100% tests passed, 0 tests failed out of 312
Total Test time (real) = 108.02 sec
sanitizer_diagnostics=0
```

隔离栈 `jobpilot-asan-0019`（13818/16818）已拆除：2 容器、1 卷、1 网络移除；
端口释放；`TEARDOWN_OK`。

P4 证据（`build/l4-0019/`，project `jobpilot-l4-0019`，13918/16918，
scratch `jobpilot_l4_0019_scratch`）：

```text
probe: test_bcrypt 7808ms
debug: 100% tests passed, 0 tests failed out of 312 (91.69 s)
asan:  100% tests passed, 0 tests failed out of 312 (101.06 s)
ubsan: 100% tests passed, 0 tests failed out of 312 (63.76 s)
sanitizer diagnostics: debug 0, asan 0, ubsan 0
builds: debug/asan/ubsan/service-release/root-release all rc=0
python: source_adapters Ran 479 OK; ai_worker Ran 190 OK
review: dev [ SKIPPED ] (6 passed), scratch [ PASSED ] 7 tests
teardown: project containers/volumes/networks removed; ports free;
          dev containers jobpilot-mysql/jobpilot-redis untouched
```

Post-merge main CI (2026-10-19):

```text
ci    39205581874 completed/success 5m32s (build 4m02s + suite 1m23s)
image 39205581917 completed/success 3m06s (container 3m01s)
```

## 11. 已知问题与后续

- ASan 残余不确定性：失败瞬间的服务端 bcrypt 耗时未打点；修复后用 15 s
  sanitizer 超时验证。如果仍失败，需要进一步打点或把该测试移到独立进程/串行
  约束，不在本计划内直接弱化断言。
- 历史 home path 7→10：owner 决定接受、披露或另行处理；禁止改写历史。
- ATS 计数随招聘季变化；0019 记录的是 2026-10-19 快照，不是永久常量。
- 既有 docs/03 / docs/04 差异维持 record-only（CURRENT_STATE 已记录）。
- 0019 不新增来源、不改 workflow、不改 P1。
