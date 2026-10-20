# JobPilotAI 当前状态核验报告

> 核验日期：2026-10-19（Asia/Shanghai）
> 核验基线：`main` @ `928d335`（与 `origin/main` 一致）
> 核验方式：按 `docs/CLAUDE_CODE_HANDOFF.md` §23 逐项实际运行；本文件是新增未跟踪文件，未修改任何代码、数据库、CI 或既有文档。
>
> **Resolution note (2026-10-20, 0021).** DB-1/DB-2/DB-3 and
> API-1/API-2/API-3/API-4 are fixed in the live contracts (`docs/03`/`docs/04`)
> and in the handoff’s dated corrections. DB-4/H-2 (`user_skills`) is
> **deferred** to a later docs cleanup. H-1 is corrected with a dated handoff
> note; H-3 is resolved by 0019’s ATS live smoke; H-4 is folded into API-4;
> H-5 is resolved by 0018’s nowcoder source; H-6/H-7/H-8 were consistent.
> The original findings below are preserved as the 2026-10-19 snapshot; this
> note does not rewrite them.

## 背景

本报告用于 Claude Code 接手 JobPilotAI 的第一阶段核验。交接文档写于 2026-10-11，其事实清单要求“每项实际运行，不许假设”。本报告记录真实命令、真实输出摘要、与 handoff 的差异，以及无法核实项的原因。

工作区在核验开始时是干净的（`git status --porcelain` 为空）；核验结束后仅新增 `CURRENT_STATE.md` 与 `EXEC_PLAN.md` 两个未跟踪文件。

## 目标

1. 核对 Git 基线、CI 状态与 §23 的硬事实。
2. 核对 `docs/04` ↔ `service/src/router.cpp`、`docs/03` ↔ `database/schema/*.sql`、README 能力清单 ↔ 代码。
3. 给出 §5 能力矩阵的更新。
4. 明确无法核实项、原因与用户可执行的补充命令。
5. 为第二阶段 `EXEC_PLAN.md` 提供事实基础。

## 结论摘要

| 维度 | 结论 |
|---|---|
| Git 基线 | `HEAD == origin/main == 928d335`；工作区干净；跟踪文件 425（handoff 写 423 + 2 份 handoff 文档 = 425，吻合） |
| CI | `35f1386` 与 `928d335` 的 `ci` + `image` 四次运行全部 `completed/success`；run id 与耗时见 §2 |
| C++ 测试 | `ctest -N` = **312**；coverage map `PASS (312 -> 312)` |
| Release preflight | 默认环境 7 pass / 0 fail / 1 skip（actionlint 不在 PATH）；补 actionlint 1.7.7 后 **8 pass / 0 fail / 0 skip / 5 manual** |
| 历史扫描 | 164 commits，**0 credential-shape findings**；info: 7 个 home path、14 个 RFC1918 字符串 |
| 禁止投递路径 | `no_submission_path.py` rc=0 |
| `apply_url` | `database/`、`service/` 无匹配；仅 `providers/public_page.py` 的 JSON-LD 候选键列表与文档提及 |
| 来源 allowlist | Python 与 C++ 都恰好是 `greenhouse` / `lever` / `ashby` |
| `docs/04` vs 路由 | **有差异**：路由 36 条（27 非内部 + 9 内部）；文档汇总 30 个方法条目；2 条文档路由未注册，1 条路由（`/hello`）未进文档；handoff 的 “22 public + 8 internal” 与两者都不符 |
| `docs/03` vs schema | **有差异**：§1 写 11 张表，实际 13 张；§3 “锁定版 DDL” 缺 0015A 的 3 个 `jobs` 列、2 个索引、`job_review_items` 与 `resume_artifacts`（后两者在 §9.2/§10.6 有记录） |
| README vs 代码 | 能力清单与代码一致；未发现 README 夸大 |
| 牛客通路 | **已复核**：`/jobs/school/jobs` 免登录 SSR、robots 允许、单次最多 20 条、无翻页；两种记录形态均确认；详见 §8 |
| 未运行项 | ASan/UBSan（`7ba62c8` 之后）、TSan、完整 L4、live publish 均未在本次核验中运行；不得声称通过 |

## 详细说明

### 1. Git 基线

```text
$ git status -sb
## main...origin/main

$ git rev-parse HEAD origin/main
928d335ec09b84658ab73ef64a57e3f1b14c092d
928d335ec09b84658ab73ef64a57e3f1b14c092d

$ git log --oneline -10
928d335 docs(readme): point the documentation map at the handoff documents
35f1386 docs(handoff): freeze the project state for a Claude Code takeover
e9d75c2 docs(release): refresh the history scan count at the close-out commit
8ad3e81 docs(licence): record the MySQL client evaluation and the honest gaps
63d33ae fix(ci): keep the service credentials literal
1ec48e1 fix(ci): drop comments from the folded service options scalar
5c777f6 docs(release): record the review-gate fix, the artifact audit and the access facts
32a810b docs(quality): refresh the coverage map for three added cases
e23f98e ci: keep the ephemeral service credentials out of command lines
7ba62c8 fix(job): keep jobs queued for review out of every public read

$ git ls-files | wc -l
425

$ git status --porcelain | wc -l
0
```

- handoff §3 写“423 before this handoff; +2 with the handoff documents”，与 425 一致。
- `35f1386..HEAD` 只有一个提交 `928d335`，只改 `docs/README.md`（+11 行）；代码未变。
- `docs/exec-plans/active/` 为空；`completed/` 有 20 个计划。

### 2. CI 状态

Token 文件 `~/.config/jobpilot/github-token` 存在，权限 `-rw------- dev:dev 94 bytes`；本次核验未读取其内容。`tools/dev/ci-status.sh --check` 返回：

```text
login=cjs1102 type=User scopes=None
repo=cjs1102/JobPilotAI private=True default_branch=main
rc=0
```

| Commit | Workflow | Run ID | 结果 | 总耗时 | 关键 job 耗时 |
|---|---|---|---|---|---|
| `35f1386` | ci | `39152747183` | completed/success | 5m40s | clean build 4m08s；full test suite 1m24s |
| `35f1386` | image | `39152747158` | completed/success | 3m04s | container image and the end-to-end gate 3m00s |
| `928d335`（HEAD） | ci | `39152821882` | completed/success | 5m53s | clean build 3m55s；full test suite 1m51s |
| `928d335`（HEAD） | image | `39152821870` | completed/success | 2m56s | container image and the end-to-end gate 2m51s |

结论：交接基线 `35f1386` 与当前 HEAD `928d335` 的 `ci`、`image` 均绿；未发现失败或取消的运行。

### 3. §23 事实清单逐项核验

| # | 事实 | 实际命令 | 输出摘要 | 判定 |
|---|---|---|---|---|
| 1 | 跟踪文件数与工作区干净 | `git ls-files \| wc -l`；`git status --porcelain` | 425；0 行 | **吻合**（handoff 423 + 2） |
| 2 | CTest 用例数 | `ctest --test-dir build/presets/debug -N \| tail -1` | `Total Tests: 312` | **吻合** |
| 3 | Coverage map | `python3 tools/checks/test_coverage_map.py --test-dir build/presets/debug --map docs/quality/test-merge-map.json` | `test_coverage_map: PASS (312 -> 312, removed=0, survivors=0, live=312)`，rc=0 | **吻合** |
| 4 | Release preflight | `python3 tools/checks/release_preflight.py` | 默认：`summary: 7 pass, 0 fail, 1 skip, 5 manual`（actionlint 不在 PATH）；设 `ACTIONLINT=/tmp/.../actionlint` 后：`summary: 8 pass, 0 fail, 0 skip, 5 manual`，rc=0 | **环境受限后吻合**；actionlint 1.7.7 已临时下载到 `/tmp`，未写入仓库 |
| 5 | 历史密钥扫描 | `tools/checks/history_secret_scan.sh` | `history scan: 164 commits, 0 credential-shape finding(s)`；`HISTORY_SCAN_CLEAN`；info: home path x7、RFC1918 x14 | **吻合**（handoff 162 commits；多出的 2 个是 35f1386/928d335） |
| 6 | 禁止投递路径 | `python3 tools/checks/no_submission_path.py` | rc=0；`14 forbidden tokens, 6 route fragments checked per file` | **吻合** |
| 7 | `apply_url` 不存在 | `grep -rn 'apply_url' database/ service/` | 无匹配，grep rc=1 | **吻合** |
| 8 | 来源 allowlist 恰好三项 | Python `SUPPORTED_PROVIDERS` / `PROVIDERS`；C++ `IsSupportedObservationSource` | `("greenhouse","lever","ashby")`；C++ 为三个 `source ==` 比较 | **吻合** |
| 9 | `docs/04` vs 路由 | 解析 `service/src/router.cpp` 与 `docs/04` §9 | 路由 36 条；文档汇总 30 个方法条目；差异见 §4.1 | **有差异** |
| 10 | `docs/03` vs schema | 解析 `docs/03` §3 DDL 与 `database/schema/*.sql` | 文档 11 表；schema 13 表；差异见 §4.2 | **有差异** |
| 11 | README 能力清单 vs 代码 | 逐项 grep/读代码 | 见 §4.3 | **一致** |

补充运行（不属于 §23 必查项，但用于确认）：

```text
$ for f in SECURITY.md CONTRIBUTING.md CODE_OF_CONDUCT.md; do ...; done
SECURITY.md: MISSING
CONTRIBUTING.md: MISSING
CODE_OF_CONDUCT.md: MISSING
```

- HTTP 层没有静态文件服务：`backend/src/http/` 只有 request/response/parser/router/server/multipart，未发现 MIME/static/sendfile 处理器。V1 “无静态服务” 得到确认。
- `/hello` 仍在 `router.cpp` 中注册（POST，回显请求体），只出现在历史文档 `docs/05`/`docs/08`，不在 `docs/04` §9 的接口汇总中。

### 4. 文档-代码差异

> 按交接要求：记录差异，不修改代码或文档去“对齐”。

#### 4.1 `docs/04-api-design.md` vs `service/src/router.cpp`

**实际路由清单（36 条）：**

- 非内部 27 条 = 业务公开 25 条 + `/health` + `/hello`。
- 内部 9 条：`/internal/jobs/import`、`/internal/jobs/source-sync`、`/internal/ai/tasks/{claim, {id}/complete, {id}/fail, {id}/input}`、`/internal/jobs/review-queue`、`/internal/jobs/review/backfill`、`/internal/jobs/review/{id}`。

**差异：**

| ID | 文档 | 代码 | 说明 |
|---|---|---|---|
| API-1 | `docs/04` §7.2 与 §9 列出 `POST /api/v1/ai/resume/optimize`、`POST /api/v1/ai/interview/generate` | `router.cpp` 未注册这两条 | 0015 计划 §17 称它们是 “reserved contract-only endpoints”；但 `docs/04` §7.2/§9 未标注“未实现/预留”，读者会误以为可用 |
| API-2 | `docs/04` §9 汇总只列 2 条 internal（import、source-sync） | 实际 9 条 internal | 其余 7 条在 §5.6 与 §10.3 有定义，但汇总表不完整 |
| API-3 | `docs/04` §9 未列 `/hello` | `router.cpp` 注册 `POST /hello` | `/hello` 是 P1 测试/回显路由，只在历史文档 `docs/05`/`docs/08` 出现 |
| API-4 | handoff §12 写 “22 public routes + 8 internal routes” | 实际 27 非内部 + 9 内部（或 25 业务公开 + 9 内部） | handoff 的计数与代码、`docs/04` 都不一致；本条属于 handoff 事实错误，不是代码错误 |

#### 4.2 `docs/03-database-design.md` vs `database/schema/*.sql`

程序化对比结果：

```text
doc tables 11: ai_tasks, applications, interview_records, job_requirements, jobs,
               notifications, profiles, resume_versions, resumes, skills, users
schema tables 13: 上述 11 + job_review_items, resume_artifacts
only schema: job_review_items, resume_artifacts
DIFF jobs: schema_only = review_status, reviewed_at, review_note
missing in doc indexes: idx_deadline, idx_review_status
```

| ID | 文档 | 代码/schema | 说明 |
|---|---|---|---|
| DB-1 | §1 “表数量：11” | 13 张表 | `job_review_items`（0015A）与 `resume_artifacts`（0015D）已在 schema 落地 |
| DB-2 | §3 “建表 DDL（锁定版）” 的 `jobs` 无 `review_status` / `reviewed_at` / `review_note`，无 `idx_review_status` / `idx_deadline` | `03_job.sql` 有这些列与索引 | §9.2 有 0015A 增量记录，但 §3 的“锁定版”没有同步；读者若只看 §3 会得到旧 schema |
| DB-3 | §3 没有 `job_review_items` / `resume_artifacts` 的建表语句 | schema 有；§9.2/§10.6 有定义 | 同 DB-2：增量章节完整，但 §3 不是当前全量 |
| DB-4 | handoff §11 写 `user_skills` | 实际表名 `skills`（`docs/03` §3 也写 `skills`） | handoff 命名错误；schema 与 `docs/03` 一致 |

> 结论：schema 是当前事实；`docs/03` 的增量章节（§9.2/§10.6）是准确的，但 §1 的表数量与 §3 的“锁定版 DDL”是旧快照。这是**文档内部不一致**，不是 schema 缺陷。

#### 4.3 README 能力清单 vs 代码

| README 声明 | 代码证据 | 判定 |
|---|---|---|
| 用户/鉴权、画像/技能、简历、岗位搜索/详情/推荐、投递、内部导入、source-sync | `router.cpp` 对应路由均存在 | 一致 |
| 0015A `clean` 默认可见性 + 内部复核面 | `job_visibility.h`、`job_quality.cpp`、`router.cpp` review 路由 | 一致 |
| `source_url`、`education_max`/`job_nature`/`deadline_*`/`sort` | `job_controller.cpp` / `job_dao.cpp` | 一致 |
| 48 KiB 描述上限 | `job_ingestion_service.cpp:311`、`job_quality.h:29`、`test_job_quality.cpp` | 一致 |
| `rule_v1` 可解释键（7 个） | `job_controller.cpp:715-720`、`match_result_json.cpp`、`job_search_filter.h` | 一致 |
| `tailor_rule_v1` / `fact_check_status=2` | `tailor_rule.h`、`resume_artifact_dao.cpp`、`resume_controller.cpp` | 一致 |
| Python 适配器只有 greenhouse/lever/ashby | `config.py:15`、`providers/__init__.py:14`、`source_observation.cpp:309-311` | 一致 |
| 0014 框架（robots/fetch/HTML/JSON-LD/RSS/sitemap/SSRF/预算） | `robots.py`、`fetch.py`、`htmljob.py`、`feeds.py`、`decoding.py`、`normalize_cn.py`、`salary.py`、`timestamps.py` | 一致 |
| 人工采集（bookmarklet + publisher） | `capture/`、`cli.py publish-capture` | 一致 |
| 只读 HTML 报表 | `tools/job_report/render_report.py` | 一致 |
| 禁止投递路径检查 | `tools/checks/no_submission_path.py` | 一致 |

**未发现 README 把未实现能力写成已实现。** README 的“未实现/非目标”段落准确覆盖了国内来源、自动投递、LLM 润色、截止时间生命周期等。

#### 4.4 handoff 与其他文档/代码的差异

| ID | handoff 说法 | 实际 | 判定 |
|---|---|---|---|
| H-1 | §5 row 16：应用列表 “lacks company/source_url” | 代码返回 `company`（`application_controller.cpp:608`），`docs/04` §6.2 也列了 `company`；只有 `source_url` 确实缺 | handoff 部分错误 |
| H-2 | §11：13 张表，含 `user_skills` | 实际表名 `skills`；13 张表数量正确 | handoff 命名错误 |
| H-3 | §5 row 2 / §20 V5：源适配器 “fixture-driven only; no live board run is recorded” | `docs/ai/project-state.md:557` 记录 “Opt-in live smoke: Greenhouse 217, Lever 10, Ashby 160”；仓库内未找到对应的原始证据文件 | **文档互相矛盾**；live smoke 结果无法核实 |
| H-4 | §12：22 public + 8 internal routes | 实际 27 非内部 + 9 内部 | handoff 计数错误 |
| H-5 | §10：牛客 “no proven list / manual only” | 2026-10-19 复核发现 `/jobs/school/jobs` 免登录 SSR、robots 允许、单次最多 20 条；见 §8 | handoff 结论已过时（当时未验证该路径） |
| H-6 | §3：423 tracked before handoff + 2 | 425 | 一致 |
| H-7 | §13：312 tests / coverage map 312 | 312 / PASS | 一致 |
| H-8 | §17：history 0 credential shape；7 home paths / 14 RFC1918 | 164 commits，0 findings；7 / 14 | 一致（commit 数因两个新提交增加） |

### 5. §5 能力矩阵更新

只列出有新增证据或状态变化的行；未列出的行本次未重新运行，沿用 handoff 的记录并标注“未复跑”。

| # | 能力 | handoff 状态 | 本次核验状态 | 证据 / 说明 |
|---|---|---|---|---|
| 2 | greenhouse/lever/ashby 适配器 | PARTIAL | **PARTIAL（未变）** | 代码与 fixtures 存在；live smoke 的 217/10/160 只有 project-state 文本，无原始证据；不能据此升级为 DONE |
| 3 | 中国来源 | BLOCKED / none registered | **BLOCKED（候选已验证，未注册）** | 牛客 `/jobs/school/jobs` 复核通过（§8）；allowlist 仍只有三项；待 EXEC_PLAN 批准后注册 |
| 5 | `apply_url` | NOT_IMPLEMENTED | **NOT_IMPLEMENTED（确认）** | `database/`/`service/` 无匹配；只有 `public_page.py` 的 JSON-LD 候选键 |
| 16 | 投递跟踪 | DONE | **DONE；列表含 `company`，缺 `source_url`** | `application_controller.cpp:608`；`docs/04` §6.2 |
| 20 | C++ HTTP 服务 | DONE, P1 frozen | **DONE；无静态文件服务（V1 确认）** | `backend/src/http/` 无 static/MIME/sendfile 处理器 |
| 24 | Web UI | NOT_IMPLEMENTED | **NOT_IMPLEMENTED（确认）** | 无模板/静态资源；无静态服务 |
| 26 | CI | DONE | **DONE（新证据）** | 35f1386 / 928d335 的 ci + image 四次全绿 |
| 27 | ASan | NEEDS_VERIFICATION after `7ba62c8` | **NEEDS_VERIFICATION（本次未运行）** | 最后一次记录是 309/309，早于 review-gate 修复与 312 用例；不得声称通过 |
| 28 | UBSan | 同上 | **NEEDS_VERIFICATION（本次未运行）** | 同上 |
| 29 | TSan | NEEDS_VERIFICATION | **NEEDS_VERIFICATION（仍未运行）** | 无新记录 |
| 30 | Python 测试 | DONE | **DONE（记录，未复跑）** | 0017 记录 446 + 190；本次未 re-run 已绿套件（遵守 agent-workflow §2） |
| 33 | MySQL client licence | PARTIAL | **PARTIAL（确认未切换）** | `Dockerfile:21,60` 仍用 `libmysqlclient-dev` / `libmysqlclient21`；`libmariadb` 未出现 |
| 34 | SECURITY.md | NOT_IMPLEMENTED | **NOT_IMPLEMENTED（确认缺失）** | 文件不存在 |
| 35 | CONTRIBUTING.md | NOT_IMPLEMENTED | **NOT_IMPLEMENTED（确认缺失）** | 文件不存在 |
| 38 | Secret / history | PARTIAL | **PARTIAL（确认）** | 0 credential-shape；7 home path、14 RFC1918；历史重写仍禁止 |
| — | 牛客通路（V3） | handoff §20 V3 未验证 | **已验证（候选）** | robots 允许、免登录、SSR、20 条上限；未审阅 ToS/法律意见；未注册 |

### 6. 无法核实 / 未运行项

| 项 | 原因 | 用户可执行的补充命令 |
|---|---|---|
| ASan / UBSan（`7ba62c8` 之后，312 用例） | 本次核验只读，不运行重型测试；按 agent-workflow §2 不应在未改代码时重跑全矩阵 | `cmake --preset asan && cmake --build --preset asan -j$(nproc) && ctest --preset asan`（需 MySQL/Redis 凭据；建议在阶段收口的唯一 L4 中运行） |
| TSan | 无记录，且不在 0017 L4 矩阵 | `cmake --preset tsan && cmake --build --preset tsan -j$(nproc) && ctest --preset tsan` |
| Live source adapter（V5） | project-state 的 217/10/160 无原始证据；本次未发起 ATS live 调用 | 配置真实 board 后 `cd source_adapters && python3 -m jobpilot_sources sync --config ... --dry-run`；先 dry-run，再决定是否发布 |
| AI live provider（V4） | 仍只有 mock 测试 | 用本地 OpenAI-compatible stub 跑 task lifecycle |
| Moka 公共列表（V2） | 本次未探测 Moka 自身列表；只看到牛客聚合记录里的 `platform=moka` 与 `jobDetailUrl` | 另行按 robots-first、5 分钟预算探测 |
| MariaDB 切换（V8） | 未应用；Dockerfile 仍用 Oracle client | 应用 3 行切换后由 `image` job 验证 |
| GitHub About/Topics、branch protection、Actions minutes | owner-only；本次只核对了 private/default_branch | `tools/dev/ci-status.sh --check` 已确认 repo 为 private；其余按 `docs/RELEASE_CHECKLIST.md` |
| 牛客 ToS / 法律意见 | 只核对了 robots.txt 与页面可访问性；这不是法律意见 | owner 决定是否接受；保留 robots 证据与来源登记表 |
| `nowcoder-search` CLI（bun） | 本 VM 上没有该 CLI，`bun` 也未安装；仓库内无此工具 | 若在 Windows 侧存在，需提供位置与版本；否则不建议作为计划依赖 |
| actionlint 默认可用性 | 不在 PATH；本次临时下载到 `/tmp` | `go install github.com/rhysd/actionlint/cmd/actionlint@latest` 或设置 `ACTIONLINT=/path/to/actionlint` |

### 7. 与 handoff 的差异汇总

1. **CI 前进**：handoff 写 `e9d75c2` 的 ci/image 绿；实际 `35f1386` 与 `928d335` 也全绿，且有逐 job 耗时。
2. **牛客结论过时**：handoff §10/§20 V3 说牛客无可用列表、仅人工采集；实际 `/jobs/school/jobs` 是免登录 SSR 列表，robots 允许。注意：这不等同于“可以自动采集”的最终法律判断，仍需来源登记与 owner 决策。
3. **路由计数错误**：handoff 的 22 + 8 与实际 27 + 9 不符；`docs/04` 汇总也不完整。
4. **应用列表字段**：handoff 说缺 `company`/`source_url`；实际只缺 `source_url`。
5. **表名**：handoff 写 `user_skills`；实际 `skills`。
6. **live smoke 矛盾**：handoff 说没有 live board run；project-state 记录了三组数字，但无原始证据。
7. **docs/03 锁定版 DDL 滞后**：表数量 11 vs 13；`jobs` 缺 0015A 三列与两个索引；缺两张新表。增量章节本身是准确的。
8. **docs/04 汇总表滞后**：两条 reserved AI 路由未标注未实现；internal 汇总只列 2/9。
9. **`/hello` 未文档化**：router 中仍有，`docs/04` 不列，历史文档才提。
10. **preflight 的 actionlint 依赖是环境项**：默认 7/0/1；补工具后 8/0/0。交接要求“8 pass”时应说明 actionlint 需预装。

### 8. 牛客可行性复核（2026-10-19）

> 本节是对另一会话结论的独立复核。所有请求均为只读 GET、诚实 UA、无登录、无 cookie 复用、无 CAPTCHA/签名绕过；共 9 次请求，在 agent-workflow §8.2 的 5 分钟候选预算内。原始 HTML/响应头保存在 `/tmp/jobpilot-nowcoder-probe-2026-10-19/`（仓库外，未提交）。

诚实 UA：

```text
jobpilot-source-adapters/0.1 (+https://github.com/jobpilotai; public job board reader)
```

#### 8.1 robots.txt

```text
GET https://www.nowcoder.com/robots.txt -> HTTP/2 200, text/plain, 619 bytes

User-agent:*
Disallow:/search
Disallow:/nccommon
Disallow:/ab/ab-test-flow
Sitemap: ... (多个 sitemap)
```

用项目自身的 `jobpilot_sources.robots.parse_robots` 复核：

```text
crawl_delay= None explicit_group= True
rules= [(False, '/search'), (False, '/nccommon'), (False, '/ab/ab-test-flow')]
/jobs/school/jobs -> allowed
/jobs/school/jobs?search=C%2B%2B -> allowed
/jobs/detail/404338 -> allowed
/search -> disallowed
/nccommon/x -> disallowed
/ab/ab-test-flow/x -> disallowed
/ -> allowed
```

结论：`/jobs/*` 未被 robots 禁止；`/search` 被禁止（我们使用的是 `/jobs/school/jobs`，不是 `/search`）。robots 未声明 `Crawl-delay`；实现时仍应采用保守的人工/低频策略。

#### 8.2 列表页 SSR 与结果上限

请求：

```text
GET https://www.nowcoder.com/jobs/school/jobs?search=<关键词>&city=<中文城市>
```

观察：

| 查询 | HTTP | 页面大小 | `jobListData` | 结果数 |
|---|---|---|---|---|
| `search=C++&city=深圳` | 200 | 225,035 B | 存在，位于 `app.132.jobListData` | 12 |
| `search=后端`（无 city） | 200 | 367,168 B | 同上 | **20** |
| `search=zzzzqqqqxxxx` | 200 | 52,920 B | 同上，空数组 | 0 |
| `search=后端&page=2` | 200 | 363,889 B | 同上 | 20（与首页 19/20 重叠，非真正翻页） |
| `search=后端&foo=bar` | 200 | 363,891 B | 同上 | 20（未观察到“降级空壳”） |
| `search=后端&pageSize=100` | 200 | 363,093 B | 同上 | 20（上限未变） |

结论：

- 单次查询最多 **20 条**；`page` / `pageSize` 不改变窗口（无可用翻页）。
- 结果顺序在两次请求间有轻微抖动（20 条中 19 条重叠），说明不能把“第一次看到的 20 条”当作稳定全量。
- 关键词无匹配时返回 200 + `jobListData: []`，这是**合法空结果**，不是解析失败。
- 另一会话所称“未知参数导致降级空壳”在本次参数组合下**未复现**；但解析器仍应在 `__INITIAL_STATE__` 缺失或 `jobListData` 不存在/类型不对时 fail-closed，而不是返回空列表。
- `app.132` 是本次构建的模块号；不能硬编码，必须递归查找 `jobListData`。

#### 8.3 两种记录形态

**原生岗（示例 id=404338，6 位）：**

```text
id=404338
jobName=嵌入式软件工程师
companyNameText=Formlabs
jobCity=深圳
salaryText=25-35K·12薪
eduLevel=5000            # 编码含义未知，不能猜
recruitType=1            # 源站详情页映射：1校招 / 2实习 / 3社招
graduationYear=2027届
createTime=1754033527000 # UTC 2025-08-01 07:32:07
updateTime=1792337414000 # UTC 2026-10-18 15:30:14
deliverBegin=1786636800000 # UTC 2026-08-13 16:00:00 (= 北京 2026-08-14 00:00)
deliverEnd=1798732799000   # UTC 2026-12-31 15:59:59 (= 北京 2026-12-31 23:59:59)
jobAddress=广东省深圳市南山区同方科兴科技园E栋6L
ext={"requirements":"...","infos":"..."}   # JSON 字符串，含 JD
```

**内推聚合岗（示例 id=30300000146411，14 位）：**

```text
id=30300000146411
jobTitle=后端开发工程师 / jobName=后端开发工程师
companyName=重庆千里科技股份有限公司 / companyNameText=同
city=None, jobCity=''       # 地点缺失，不能编造
salary=薪资面议 / salaryText=薪资面议
education=本科
platform=moka
description=<p>...职位描述...</p>   # HTML 内联 JD
createTime=1787378565000    # UTC 2026-08-22 06:02:45
```

**详情页复核：**

| 记录 | 详情 URL | SSR | `store.jobDetail.detail` 关键字段 |
|---|---|---|---|
| 原生 404338 | `/jobs/detail/404338` | 有 | `id=404338`、`jobName`、`jobCity`、`ext`、`jobCompany.companyName=Formlabs` |
| 聚合 30300000146411 | `/jobs/detail/30300000146411` | **有** | `globalJobId=30300000146411`、`originId=146411`、`platform=moka`、`jobDetailUrl=https://app.mokahr.com/...`、`description`、`salary`、`education`、`publishedAt`、`openAt`、`status=open` |

**重要更正：** 另一会话称“聚合岗详情页无 SSR”；本次样本的聚合岗详情页**有 SSR**，且包含 `globalJobId` 与外部 Moka 链接。因此第一版适配器不应依赖“聚合岗详情无 SSR”这一假设；本计划也不需要详情页，因为列表本身已带 JD（原生 `ext`、聚合 `description`）。

#### 8.4 登录与合规观察

- 页面无“请先登录”或登录墙标记；响应清空 `username`/`uid` cookie（未登录态）。
- 未使用登录 cookie、未复现签名、未绕过 CAPTCHA/TLS。
- robots 仅禁止 `/search`、`/nccommon`、`/ab/ab-test-flow`；`/jobs/*` 允许。
- **边界**：robots 允许 ≠ 法律意见或 ToS 许可。来源登记表应写明“robots-allowed, login-free, probe-only evidence; ToS review is an owner decision”。

### 9. 示例：复现本次核验

```bash
# 1) Git / 测试计数
cd "$(git rev-parse --show-toplevel)"
git status -sb
git rev-parse HEAD origin/main
git ls-files | wc -l
ctest --test-dir build/presets/debug -N | tail -1

# 2) 守卫
python3 tools/checks/test_coverage_map.py \
  --test-dir build/presets/debug --map docs/quality/test-merge-map.json
python3 tools/checks/release_preflight.py
# 若 actionlint 不在 PATH：
# ACTIONLINT=/path/to/actionlint python3 tools/checks/release_preflight.py
tools/checks/history_secret_scan.sh
python3 tools/checks/no_submission_path.py
grep -rn 'apply_url' database/ service/

# 3) CI
tools/dev/ci-status.sh --limit 5
tools/dev/ci-status.sh --run 39152821882
tools/dev/ci-status.sh --run 39152821870

# 4) 牛客 robots / SSR（只读、诚实 UA、低频）
UA='jobpilot-source-adapters/0.1 (+https://github.com/jobpilotai; public job board reader)'
curl -sS -A "$UA" https://www.nowcoder.com/robots.txt
curl -sS -A "$UA" \
  'https://www.nowcoder.com/jobs/school/jobs?search=C%2B%2B&city=%E6%B7%B1%E5%9C%B3' \
  | grep -o 'window.__INITIAL_STATE__' | head
```

## 总结

- 交接基线、CI、312 用例、coverage map、preflight、历史扫描、禁止投递路径、`apply_url` 缺失、三项 allowlist：**均与 handoff 一致**（preflight 需 actionlint 才能达到 8/0/0）。
- 文档-代码差异集中在三处：`docs/04` 的接口汇总/预留路由、`docs/03` §1/§3 的旧快照、handoff 的若干计数与字段描述。schema 与 README 能力清单本身是准确的。
- 牛客 `/jobs/school/jobs` 是**已复核的合规候选**：免登录、robots 允许、SSR、单次最多 20 条、无翻页、两种记录形态均可用；但结果窗口不稳定、无全量证明，必须 `complete=false`。另一会话关于“未知参数降级空壳”和“聚合岗详情无 SSR”的两条判断在本次样本中未复现/被推翻，计划不能依赖它们。
- 未运行的 ASan/UBSan/TSan、live publish、Moka、AI live provider 等，均明确标注为“未核实”，不得在报告中写成通过。

