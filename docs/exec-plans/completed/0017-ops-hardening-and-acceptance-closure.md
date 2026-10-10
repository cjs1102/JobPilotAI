# ExecPlan 0017: 运维硬化 + 验收闭环（OPS HARDENING AND ACCEPTANCE CLOSURE）

> Status: **Completed and archived**（2026-10-10）。四个单元全部落地，各自定点验证后提交
> （`e6cae75` A、`4a7995e` B、`38f29fd` C+D、`b2a55be` D 的修红、`b1d1b89` 收口文档），
> 计划本身见 `a4d1f5f`，只读 CI 工具见 `tools/dev/ci-status.sh`。
> 阶段收口的单次 L4 **第二次尝试全绿**：debug / asan / ubsan 各
> `100% tests passed, 0 tests failed out of 309`、sanitizer 诊断 0、Python `Ran 446` +
> `Ran 190` 均 `OK`、回填隔离 dev `[ SKIPPED ]` + scratch `[ PASSED ] 7 tests`（§10.2）。
> 第一次尝试因宿主比 0016 慢 3.7 倍而记为 environment-limited，harness 因此加了前置探针
> 闸门，该次记录完整保留为历史。四次推送的 CI 全部 `success`，integration job 从 5m21s
> 降到 1m16s–1m49s（§10.3）。
>
> 基线 HEAD：`a8dd333`（开工时 = `origin/main`，工作区干净，无进行中计划）。
>
> Task level: **阶段末一次 L4**（§8.3）。单个单元的验证按 `docs/ai/agent-workflow.md`
> §2.1 取最小集合；本阶段不改生产代码，改动面是 CI 配置、测试用例、运维脚本与文档。
>
> 依据：`docs/ai/agent-workflow.md` §2/§8；验证等级 `docs/quality/README.md:27-59`；
> ExecPlan 必需章节 `docs/exec-plans/README.md`。
>
> 上游证据：0016 已归档（`completed/0016-open-source-packaging.md`），其 L4 在
> HEAD `0a6e59e` 全绿；CI 运行 `38057642118`（`9238b97`）三个 job 全绿；
> 最近一次运行 `38058123275`（`a8dd333`）待 owner 在 Actions 页确认。

## 1. 目标

在不改变任何产品契约、不新增功能面的前提下，把"每次推送的成本"和"仍然挂在账上的
验收项"一次性收口：

1. **0017-A**：CI 不再为同一棵树编译两次；`image` job 不再对无关改动收费。
2. **0017-B**：把 309 个 C++ 用例与两个 Python 套件按"断言目标"审计一遍，给出带证据的
   重叠报告，并对**可机械证明被包含**的用例做有界合并，用映射表证明覆盖未降级。
3. **0017-C**：新增 `tools/dev/capture-acceptance.sh`，让"真机书签 JSON → 发布 → 导入 →
   公共搜索可见 → 读回"这条链路在隔离栈里一条命令跑完，并打印 0014 §19.5 模板所需的
   脱敏证据行（合成 JSON 端到端全绿即为本单元验收）。
4. **0017-D**：把发布清单固化到 `docs/RELEASE_CHECKLIST.md`，能自动查的项写成脚本，
   逐项可勾选、自动项带原始输出。

### 1.1 非目标（本阶段不做）

- 不改 `docs/03` 数据库契约、`docs/04` API 契约、`rule_v1` 权重、48 KiB 描述上限、
  0015A 审核闸门。
- 不改 `backend/{include,src}/jobpilot/{network,http}/`（P1 冻结）。
- 不转公开仓库、不改 About/描述、不轮换 PAT——这三件是 owner 动作，本阶段只把它们写进
  §11 与 `docs/RELEASE_CHECKLIST.md` 的"仅 owner"分组。
- 不启动 0014 Step 5（需新来源或书面许可）、不解冻 0015 D6（需 §17 记录 + 仅措辞合并测试）。
- 不立项 0018（最小本地看板），不写新功能。
- 不做 `jobpilot_review_scratch` 的回填 apply、不做开发库 `jobpilot` 的回填 apply。

## 2. 背景与当前状态（文件级证据）

- CI 成本：`.github/workflows/ci.yml:57-60`（`build` job）与 `:125-128`（`integration`
  job）各执行一次 `cmake --preset release` + `cmake --build --preset release`，同一棵树
  编译两遍；`integration` 还额外跑一遍 Conan venv + `bootstrap.sh`（`ci.yml:116-123`）。
  `image` job（`ci.yml:165-182`）对任何 push/PR 都跑，包括只改文档的提交。
- 产物可复用性（本阶段需要证明的前提）：`build/presets/release` 共 88 MiB（`service`
  78 MiB + `backend` 11 MiB）；单测二进制的 `readelf -d` 只列出 `libmysqlclient.so.21`
  与系统库，`RUNPATH` 指向 `~/.conan2/p/**/lib` 但没有对应 `NEEDED` 条目，即
  Conan 依赖是静态链入的——这**是 A 单元要用全部 56 个测试二进制复验的假设**，不是结论。
- Docker Hub 在本机不可达（`completed/0016-open-source-packaging.md` §17），本机已有
  `jobpilot-backend:verify`（194 MB）；`tools/dev/demo-gate.sh:127-133` 已支持用
  `JOBPILOT_BACKEND_IMAGE` 指定本地镜像，并用独立 compose project/端口/卷隔离。
- 可用性：仓库私有 ⇒ Actions API 在本机未认证时返回 404；本机没有 `gh`、没有 token、
  `/tmp` 无历史凭据文件。**因此 CI 运行的"绿"与"耗时"最终只能由 owner 在 Actions 页确认**
  （§8.2 给出本地可做的等价证据与明确标注的差距）。
- 0014 的真机书签验收仍是空的：`completed/0014-...md:1047-1060`（§19.5 模板）需要
  `created/updated/total` 与公共搜索可见性两行，`:1065-1084`（§19.6）记录了 owner 已完成
  的"提取"一步与限制。既有 `source_adapters/tools/verify_manual_capture_e2e.py` 需要
  一个已经跑起来的 service，且它自己合成 capture，并不吃 owner 的真机 JSON。
- 测试规模：`ctest --test-dir build/presets/release -N` ⇒ `Total Tests: 309`，来自 56 个
  `test_*` 二进制；label 集合为 `ai application auth concurrency external integration job
  resume tsan user`。`ctest --print-labels` 只列 label，`external`/`integration` 共 17 个，
  59 个无 label（`ci.yml:5-10` 已把这一点写进注释）。
- 发布清单目前不存在：仓库根有 `LICENSE`、`THIRD_PARTY_NOTICES.md`，`docs/` 下没有
  `RELEASE_CHECKLIST.md`；0016 P5 的敏感信息扫描与链接检查是一次性人工动作，没有脚本。

## 3. 范围

### 3.1 In scope

- `.github/workflows/ci.yml`、新增 `.github/workflows/image.yml`。
- `docs/quality/test-overlap-audit.md`、`docs/quality/test-merge-map.json`、
  `tools/checks/test_coverage_map.py`，以及被审计判定可合并的测试文件（`service/tests/`）。
- `tools/dev/capture-acceptance.sh`、`docs/examples/capture-acceptance-sample.json`。
- `docs/RELEASE_CHECKLIST.md`、`tools/checks/release_preflight.py`。
- 阶段收口的 `docs/ai/project-state.md` / `AGENTS.md` 状态行、`docs/quality/README.md`
  的 L4 记录。

### 3.2 Out of scope（明确不做）

- 任何生产代码（`backend/src`、`service/src`、`service/include`、两个 Python 包的
  `jobpilot_sources/*`（除测试外）、`ai_worker/jobpilot_ai/*`）。
- 任何 schema/`ALTER`/回填 apply。
- 任何第三方 GitHub Action 的新依赖（不引入 `dorny/paths-filter` 之类；路径过滤用原生
  workflow 触发器）。
- 分支保护、仓库可见性、About/描述、PAT 轮换、Actions 分钟数购买。

## 4. 约束与不变量

1. **数据安全**：开发库 `jobpilot` 只当只读数据；回填只允许 dry-run；隔离验证一律使用
   独立 compose project / 端口 / 卷；**不碰 `jobpilot_review_scratch`**（本阶段 L4 的回填
   隔离 leg 用一个阶段自建、自毁的 MySQL 容器与库名 `jobpilot_review_scratch_0017`）。
2. **连接**：`MYSQL_HOST=127.0.0.1`（禁用 `localhost`，unix socket 会失败）。
3. **秘密**：不打印、不提交任何 token/口令/`.env` 值；`publish-capture` 只按变量名取 token
   （`--token-env`），脚本不得把 token 放进命令行或日志。
4. **Git**：只有 Lead 提交/推送；禁止 `reset`/`clean`/改历史；不丢弃已有修改。
5. **验证纪律**：不重跑已绿的套件；不为了省钱削弱断言；L4 独占、串行、只跑一次。
6. **CI 必须保持可信**：任何 A 单元的改动如果让 `integration` 不再真正执行 309 个用例，
   或让 `image` job 在 main 上缺席，视为失败而非"优化"。

## 5. 选定设计

### 5.1 0017-A：CI 成本

- **A1 产物复用**：`build` job 在 `cmake --build` 之后把 release 构建树打成**单个
  tar.gz**（`tar -czf` 于仓库根，`-C build presets/release`），用
  `actions/upload-artifact@v4` 上传（`compression-level: 0`，`retention-days: 1`）。
  `integration` job 加 `needs: build`，用 `actions/download-artifact@v4` 取回并
  `tar -xzf` 到同一路径 `build/presets/release`，然后**删掉** Conan venv、`bootstrap.sh`、
  `cmake --preset`、`cmake --build` 四步，保留 apt（`libmysqlclient-dev` 运行库、
  `default-mysql-client`、`python3`）、schema 装载、授权、数据库记录、`ctest`、Python 套件。
  - 为什么用 tar 而不是直接上传目录：`actions/upload-artifact` 不保证保留可执行位，而
    `ctest` 必须能 exec 测试二进制；tar 在本地就能完整复现上传/下载两端的字节与权限语义。
  - 为什么 `integration` 仍然需要 apt：测试二进制动态依赖 `libmysqlclient.so.21`，schema
    装载需要 mysql 客户端；但**不需要** Conan（§2 的静态链接前提由 A 的验证复验）。
- **A2 `image` job 过滤**：把 `image` job 移入 `.github/workflows/image.yml`：
  `push: branches: [main]` 恒跑；`pull_request: paths: [Dockerfile, docker-compose.yml,
  docker/**, tools/dev/**, .github/workflows/image.yml]`。job 名保持
  `container image and the end-to-end gate`（保持可用于分支保护的检查名）。
- **A3 明确不做**：`concurrency` 取消旧运行（会让"三次运行对比"的观测对象不稳定，且可能
  取消 owner 正在确认的那次运行）；`ci.yml` 的 PR 路径忽略（docs-only PR 跳过必跑检查会
  卡住分支保护）。

### 5.2 0017-B：测试重叠审计

- **B1 报告**：`docs/quality/test-overlap-audit.md`。方法：`ctest -N` 全量清单 + 每个用例
  →二进制→源文件的映射 + 按"断言目标"（被测函数/契约不变量/边界）归类；对每个
  "多层重复覆盖"的断言，给出：涉及用例名、各自 `file:line`、断言原文、为什么其中一个
  被另一个包含、建议动作（合并/保留/仅记录）。
- **B2 映射 + 机械校验**：`docs/quality/test-merge-map.json`（`removed` →
  `survivor` + `reason` + `evidence`）与 `tools/checks/test_coverage_map.py`（stdlib）：
  校验 (a) 每个 `removed` 名字在 `ctest -N` 里确实不存在、(b) 每个 `survivor` 确实存在、
  (c) 报告里的计数与 map 一致、(d) JSON 结构合法；任一违反 exit≠0。这样"覆盖未降级"
  是可执行的断言，而不是散文。
- **B3 有界合并**：只合并**同一二进制、同一输入、断言集合严格包含**的用例；
  上限 20 个用例，超出部分只写进报告"建议但未执行"。判定不确定的一律保留。
- 明确不做：跨二进制重构、把多个用例合成参数化 gtest、删除任何 `external`/`integration`
  标签下的独立失败模式用例。

### 5.3 0017-C：书签发布链路验收脚手架

- 新增 `tools/dev/capture-acceptance.sh`，用法：

  ```text
  tools/dev/capture-acceptance.sh --capture <capture.json> [--keep-evidence <file>]
  ```

  默认隔离栈与 demo gate 同构但独立：`jobpilot-capture` project、API `:18081`、
  MySQL `:13307`、Redis `:16380`、独立卷；镜像默认 `jobpilot-backend:local`，可用
  `JOBPILOT_BACKEND_IMAGE=jobpilot-backend:verify` 指向本机已有镜像；token 只存在于进程
  环境（脚本自生成，仅注入内部导入头）。
- 链路：configure → start → `python3 -m jobpilot_sources publish-capture --file <json>
  --base-url http://127.0.0.1:18081 --token-env … --json` → 断言 `created/updated/total`
  → 公共搜索可见性（`GET /api/v1/jobs?keyword=…`）→ `GET /api/v1/jobs/{id}` 读回断言
  （`source=manual`、`source_url` 归一化一致、freshness 为 null、F13 deadline 字段语义）
  → F14（碰撞观测被 `skipped_manual` 跳过且读回字节一致）→ teardown（只 `down -v` 本项目）。
- 输出：§19.5 模板的脱敏证据行（站点类别只写 `manual capture file`，不写平台；不含
  token、不含 URL 以外的隐私字段），人类仍需确认的行留空。证据落盘时同时写
  `--keep-evidence` 指定的文件（默认 `/tmp/capture-acceptance-<stamp>.txt`）。
- 合成输入：`docs/examples/capture-acceptance-sample.json`（`example.invalid` URL、
  明确标注 synthetic），owner 只要把自己的真机 JSON 传一次即可闭环。

### 5.4 0017-D：发布清单

- `docs/RELEASE_CHECKLIST.md`：分组 = 许可证与第三方 notices / README 面向外部 /
  敏感信息扫描 / CI 与 Actions 分钟数 / 仓库元数据（About、描述、话题）/ 仅 owner 动作。
  每项：怎么查、期望值、证据形式（自动项必须是原始输出）。
- `tools/checks/release_preflight.py`（stdlib、离线、exit≠0 即失败）负责自动项：
  `LICENSE` 存在且非空；`THIRD_PARTY_NOTICES.md` 覆盖 `service/conanfile.txt` 里的每个
  依赖名；跟踪文件中无秘密模式（`ghp_`/`github_pat_`/`BEGIN … PRIVATE KEY`/`password=`
  等，`.env.example` 与已知占位排除）；README 有英文 Quick Start 与文档化命令；
  相对链接存在；`.github/workflows/*.yml` 通过 actionlint（不可用则 SKIP，不得算 green）；
  无被跟踪的构建产物/`.env`。Actions 分钟数与仓库元数据是手动项，脚本输出
  `MANUAL:` 行并在清单里给出链接。

## 6. 被否决的备选

| 备选 | 否决理由 |
|---|---|
| ccache / `actions/cache` 缓存对象文件 | 首次运行仍然全量编译，收益依赖缓存命中；integration 仍要跑 configure+link，省不掉 Conan 解析。作为 A1 失败时的回退方案记录在案。 |
| 合并 `build` 与 `integration` 为一个 job | 省得更多，但会抹掉"干净 runner 上仅编译"这个独立信号，也正是 0016 记录的那次 `ctest` 路径错配能被发现的场景；违反 §4.6。 |
| `dorny/paths-filter` 做 job 级路径过滤 | 引入新的第三方 Action 供应链面；原生 workflow 触发器足够。 |
| `concurrency: cancel-in-progress` | 会让"三次运行耗时对比"失去稳定观测对象，并可能取消 owner 正在确认的运行。 |
| 直接删除重叠测试而不留映射 | 覆盖降级无法证明；违反 §4.5。 |
| 用 `verify_manual_capture_e2e.py` 充当 C 的验收脚本 | 它自己合成 capture，不吃 owner 的真机 JSON，且要求手工起 service；不能闭环 §19.5。 |
| 让 C 复用开发栈（`jobpilot-mysql`/`:18080`） | 违反数据隔离红线（§4.1）。 |

## 7. 分阶段实施（单元、写范围、依赖、验收）

所有单元互相独立；Lead 串行提交与推送。写范围是建议性隔离，不是锁。

| 单元 | 写范围 | 依赖 | 验收标准 |
|---|---|---|---|
| A  CI 成本 | `.github/workflows/` | 无 | actionlint 两个文件 0 issue；本地复现集成 job 的全部步骤（tar 往返 + 全 309 用例在 release preset 下全绿）；给出"改造前=两次全树编译 vs 改造后=一次+产物传输"的耗时对比（本地测量，明确标注非 runner 墙钟）；推送后 owner 在 Actions 页确认三个 job 全绿。 |
| B  重叠审计 | `docs/quality/test-overlap-audit.md`、`docs/quality/test-merge-map.json`、`tools/checks/test_coverage_map.py`、`service/tests/` | 可与 A 并行（分析阶段不编译） | 报告每条建议都有 `file:line`+断言原文证据；`test_coverage_map.py` 在合并后 exit=0，且**负控制**（故意破坏的 map：留存的 removed 名 / 不存在的 survivor / 计数不一致）必须 exit≠0；被合并用例的二进制重跑全绿；L4 里 CTest 计数 = `309 - 已合并数` 且 0 失败。**若严格包含规则下 k=0**（无候选可证明被包含），验收改为：报告给出全部候选各自失败的条款 + 检查器 + 负控制原始输出 + L4 证明 309/309 仍绿；此时"覆盖未降级"由 309→309 与检查器共同证明，不制造合并。 |
| C  验收脚手架 | `tools/dev/capture-acceptance.sh`、`docs/examples/capture-acceptance-sample.json` | 需要可用的本地镜像（已有 `jobpilot-backend:verify`） | 合成 JSON 端到端全绿（publish→import→公共搜索可见→读回断言→F14），evidence 文件含 §19.5 所需的 `created/updated/total` 与可见性两行，且不含 token；`bash -n` 与 `shellcheck`（若可用）无 error。 |
| D  发布清单 | `docs/RELEASE_CHECKLIST.md`、`tools/checks/release_preflight.py` | 无 | 清单每项可勾选；自动项运行脚本有原始输出并 exit=0；手动项（Actions 分钟数、About/描述）明确标注为 owner 动作。 |

顺序建议：**A 先落地**（收益最高，且它的推送本身就是 CI 观测样本）→ B → C → D →
阶段收口（§8.3）。

## 8. 验证与回滚

### 8.1 单元定点验证

- A：`actionlint .github/workflows/ci.yml .github/workflows/image.yml`；本地按 workflow
  的**原样命令**跑一遍：`tools/dev/bootstrap.sh`（全新 `CONAN_HOME`）→ `cmake --preset
  release` → `cmake --build --preset release` → `tar -czf` → 删除构建树 → `tar -xzf` →
  按 integration job 环境变量跑 `ctest --test-dir build/presets/release`（309 用例）。
  另用全部 56 个测试二进制复验 `readelf -d` 无 Conan `NEEDED`。
- B：合并后重跑受影响二进制（label 或直接二进制名），再跑 `test_coverage_map.py`。
- C：脚本自身就是验证路径，端到端跑一次并保存 evidence 文件。
- D：运行 `tools/checks/release_preflight.py`，保存原始输出。

### 8.2 干净环境复现方案（每单元的"外人可复现"路径）

- A：`tools/dev/bootstrap.sh` + 预设命令（README Quick Start 记载）；产物往返用同一条
  tar 命令，任何人可在本机照抄。**差距声明**：runner 墙钟与"CI 是否真绿"无法在本机
  取得（无 token/无 `gh`），只能由 owner 在 Actions 页确认；本机提供的是命令级等价证据。
- B：`ctest -N` + `tools/checks/test_coverage_map.py`（离线，无 DB/无网络）。
- C：`tools/dev/capture-acceptance.sh --capture docs/examples/capture-acceptance-sample.json`
  （需要 Docker 与本机镜像；不需要外网）。
- D：`python3 tools/checks/release_preflight.py`（离线）。

### 8.3 阶段收口：一次 L4

本阶段未改生产代码，但 (a) owner 明确要求"阶段末只跑一次完整 L4"，且 (b) 0016 的 L4 之后
构建环境确实被改动过——release 构建树被 A 单元的产物往返验证替换过，Conan 工具链一度指向
A 的临时 `CONAN_HOME`（已用 `tools/dev/bootstrap.sh` 默认参数恢复为 `~/.conan2`）——符合
`agent-workflow.md` §2.1 "环境被失效" 的例外。B 单元结论 k=0、未改任何测试源码，因此
C++/测试代码在 L4 期间是冻结的（L4 之后只有 CI YAML/文档/新增离线检查器提交，按 §2.4 不
使代码测试失效）。

隔离：harness `build/l4-0017/run.sh`（gitignored）使用阶段自有的 compose 项目
`jobpilot-l4-0017`（MySQL `:13399` / Redis `:16399` / 独立卷，schema 由
`database/schema` 首次初始化装载）。开发库 `jobpilot`、`jobpilot_review_scratch` 与开发
容器全程不被触碰；回填隔离的 dev leg 用该容器内的 `jobpilot` 库证明守卫会 SKIP，scratch leg
用同容器内新建、同样装载了 schema 的 `jobpilot_l4_scratch` 库。

矩阵：`debug`/`asan`/`ubsan` 全量 CTest（期望 `100% tests passed, 0 tests failed out of 309`，
sanitizer 诊断 0）、`service/` 与 root Release 构建、两个 Python 套件、回填隔离两 leg。
日志写 `build/l4-0017/`，摘要写进 §10 与 `docs/quality/README.md`。

### 8.4 回滚

- A：`git revert` 对应提交即可恢复双编译；产物复用若在 runner 上失败（权限/路径），
  回退到 ccache 方案或原双编译，**不留下红的 CI**。
- B：合并提交 revert 即恢复用例；映射文件随之 revert（`test_coverage_map.py` 在校验
  旧状态时会失败，回滚必须成套）。
- C/D：纯新增文件，revert 或删除即可，无运行时影响。

## 9. 停止条件（遇到即停并给证据）

1. 真实契约冲突、数据安全风险、秘密泄露、无法解释的失败。
2. A 的产物复用被证明会让 `integration` 少跑用例或跑不了用例，且 30 分钟内无法修好。
3. B 的任何一个合并候选无法用"同一输入、断言严格包含"证明 ⇒ 该候选改为"仅记录"。
4. C 的隔离栈起不来（镜像缺失且 registry 不可达）⇒ 报告环境限制，不降级到开发栈。
5. 本机出现新的资源/环境故障（磁盘 < 2 GB、MySQL/Redis 容器不可用）⇒ 停。
6. 探测/运行预算：单次本地构建+全量 ctest 超过 20 分钟视为异常，先查日志再决定，
   不静默重跑。

## 10. 进度复选框

- [x] P0 计划落地并提交（本文件，`a4d1f5f`）
- [x] P1 = 0017-A：workflow 改动 + 本地复现证据 + 推送（`e6cae75`）
- [x] P2 = 0017-B：报告 + 映射 + 校验器 + 负控制（`4a7995e`，k=0）
- [x] P3 = 0017-C：`capture-acceptance.sh` + 合成 JSON 端到端证据（`38f29fd`）
- [x] P4 = 0017-D：`RELEASE_CHECKLIST.md` + `release_preflight.py` 原始输出（`38f29fd`，
      并按 §11 的记录修掉它在已推送树上抓到的私网地址，`b2a55be`）
- [x] P5 = L4（第二次尝试全绿：debug / asan / ubsan 各 309/309、Python 446 + 190、回填隔离
      dev `SKIPPED` + scratch 7/7；第一次尝试 environment-limited 保留为历史，见 §10.2）
      + 状态文档同步 + 计划归档 + CI 结果入档（§10.3）

### 10.1 单元证据

**A（`e6cae75`）**：actionlint 1.7.7 对两个 workflow 文件 rc=0；56/56 测试二进制
`readelf -d` 无 Conan `NEEDED`（静态链接，故 integration 不需要 Conan 缓存）；从零构建
（全新 `CONAN_HOME`，`-j4`）bootstrap 83.41s + configure 0.74s + compile 127.11s；
tar 4.26s（28,689,456 B / 1117 members，源自 88 MiB 树）+ extract 0.62s，解包后 56 个二进制
仍可执行；在**解包后的树**上按 integration job 的环境跑完整套件：`100% tests passed, 0
tests failed out of 309`（40.83s，0 skipped）、`Ran 446 tests`+`OK`、`Ran 190 tests`+`OK`。
本机墙钟对比（`build/ci-0017/cost-table.md`）：466.61s → 260.23s，每推送少 206.38s
（重复的 bootstrap+configure+compile 211.26s 被 4.88s 的 tar 往返替代）；`image` job 不再
对 docs-only 推送计费。**runner 墙钟与"三次运行对比"由 owner 在 Actions 页确认**：本机无
token、无 `gh`，匿名 API 对私有仓库返回 404。

**B（`4a7995e`）**：`ctest -N` = 309（289 个 gtest 用例来自 37 个文件，另 20 个是单用例
`main()` 二进制）；1,533 个同二进制用例对经四个机械过滤器 + 1 次人工循环等价扫描得到 66 个
候选（50 个无序对），**全部**至少违反严格合并规则的一条 ⇒ k=0，未改任何测试源码
（`git status --porcelain -- service/tests backend/tests` 为空），因此无需重编；47 个
RECORD-ONLY 重叠按 13 个断言目标分组，每条都给 `file:line` + 断言原文。正控制：
`test_coverage_map: PASS (309 -> 309, removed=0, survivors=0, live=309)`（exit 0）；负控制
（removed 仍在活列表 / survivor 不存在 / 计数漂移 / 缺 `proof` / 非法 JSON / 缺证据）各自
exit 1 并在报告 §7.2 附原始输出；Lead 独立复验了前两条。CI 的 integration job 现在跑该守卫。

**C（`38f29fd`）**：`tools/dev/capture-acceptance.sh` 在隔离栈（project
`jobpilot-capture`，API :18081 / MySQL :13307 / Redis :16380 / 独立卷，镜像
`jobpilot-backend:verify`）上端到端 9 leg 全绿：dry-run（clean=1）→ 发布
（created=1 updated=0 total=1）→ 重复发布幂等（created=0 updated=1 total=1）→ 公共搜索可见
（matched=1 rows=1 id=1）→ 详情读回 17 字段一致（含 F13 date-only deadline 按北京日期渲染、
`<script>` 已被剥离、`last_seen_at`/`freshness` 为 null）→ F14 碰撞观测被
`skipped_manual=1` 跳过且详情字节一致 → 脱敏 §19.5 证据块落盘且不含 token，退出后本项目
外容器列表不变，最后一行 `CAPTURE_ACCEPTANCE_OK`。证据
`build/capture-0017/section-19.5-evidence.txt`（含 `created/updated/total` 与可见性两行）。
**owner 只需把真机 JSON 过一遍该脚本即可闭环 0014 §19.5。**

**D（`38f29fd` 落地，`b2a55be` 修红）**：`python3 tools/checks/release_preflight.py` 在干净
clone 上 `7 pass, 0 fail, 0 skip, 5 manual`（`ACTIONLINT=…` 指向 actionlint 1.7.7）。负控制：
向 clone 注入形如 `ghp_…` 的假 token ⇒ `FAIL secrets` 指名 `file:line`，rc=1。
**真实收获**：该检查在已推送的树上抓到 `docs/quality/test-overlap-audit.md:4` 的开发机
私网地址（0016 P5 曾手工 redact 同类地址），已 redact 并推送；手动项（Actions 分钟数、
About/描述、仓库可见性、PAT 轮换）由脚本打印 `MANUAL:` 行并在清单 §5/§6 展开。

### 10.2 L4（阶段收口，一次）——**第二次尝试全绿**

宿主恢复后重跑（日志 `build/l4-0017/run2.log`；harness 现带前置闸门：纯 CPU 的
`test_bcrypt` 超过 14 s 直接 exit 3，不再产出误导性失败）：

```text
probe: test_bcrypt 9050ms                                  （0016 基线 7130ms）
builds: debug / asan / ubsan  configure rc=0 + build rc=0；service release rc=0；
        root-release configure rc=0 + rc=0
debug: 100% tests passed, 0 tests failed out of 309        Total Test time = 114.83 sec
asan : 100% tests passed, 0 tests failed out of 309        Total Test time = 130.21 sec
ubsan: 100% tests passed, 0 tests failed out of 309        Total Test time =  74.41 sec
sanitizer diagnostics: debug 0 / asan 0 / ubsan 0
python: source_adapters `Ran 446 tests` + `OK`；ai_worker `Ran 190 tests` + `OK`
review dev    : REVIEW_DB_ISOLATION connected=jobpilot scratch_env=<unset>
                → [ SKIPPED ] 1 test（apply/revert 用例），其余 6 个通过
review scratch: REVIEW_DB_ISOLATION connected=jobpilot_l4_scratch
                scratch_env=jobpilot_l4_scratch → [ PASSED ] 7 tests
teardown: containers outside the project after = jobpilot-mysql jobpilot-redis（隔离成立）
```

隔离：全过程只用阶段自有的 compose 项目 `jobpilot-l4-0017`（MySQL `:13399` /
Redis `:16399` / 独立卷），开发库 `jobpilot`（跑前跑后 4 行 jobs）、
`jobpilot_review_scratch` 与开发容器均未被触碰；回填 scratch leg 用的是该容器内新建、
同样装载了 `database/schema` 的 `jobpilot_l4_scratch` 库。

#### 第一次尝试：environment-limited（保留为历史）

harness：`build/l4-0017/run.sh`（gitignored）。隔离：阶段自有的 compose 项目
`jobpilot-l4-0017`（MySQL `:13399` / Redis `:16399` / 独立卷，schema 由 `database/schema`
首次初始化装载；scratch 库 `jobpilot_l4_scratch` 建在同一容器内）。开发库 `jobpilot`
（跑前跑后都是 4 行 jobs）、`jobpilot_review_scratch` 与开发容器全程未被触碰。

已取得的原始行（全部落在 `build/l4-0017/`）：

```text
builds: debug / asan / ubsan  configure rc=0 + build rc=0；service release rc=0；
        root-release configure rc=0 + rc=0
python: source_adapters `Ran 446 tests` + `OK`；ai_worker `Ran 190 tests` + `OK`
ctest(debug): 97% tests passed, 9 tests failed out of 309
              Total Test time (real) = 267.68 sec
```

9 个失败**全部**是带 2 秒 socket 超时的 HTTP 用例（`UserHttpTest.*` 4 个、
`ResumeHttpTest.*` 5 个），失败信息是 `C++ exception ... "registration failed"`：注册请求
超过了客户端 2 秒超时（`service/tests/test_user_http.cpp:97-98`）。

根因是**宿主机当前比 0016 那次 L4 慢约 3.7 倍**，与代码、契约、隔离方式无关：

```text
纯 CPU、不连数据库的 bcrypt_test（同一 preset、同一二进制）：
  0016 L4      Passed    7.13 sec
  本次探针     elapsed=26.58 user=26.05          → 3.7x
同一批 HTTP 用例：
  0016         Passed 2.16 / 2.22 / 2.21 sec      （刚好在 2 s 超时之下）
  本次         Failed 3.38 / 5.42 / 4.38 sec      （越过超时）
全量 debug：0016 81.95 s（测试秒数合计 81.4）；本次 267.68 s（298 个已完成用例合计 198.4）
系统侧：cpu.max=max（无 CPU 配额）、亲和 4 核、PSI cpu some avg10 8.9、
        除本次测试自身外没有其他用户进程占用 CPU
```

探针命令：`/usr/bin/time ./build/presets/debug/backend/test_bcrypt`；**健康基线 = 7.1 s**
（`build/l4-0017/probe-bcrypt.log`）。约 25 分钟后（其他一切结束、系统空闲）复测仍是
`elapsed=28.96 user=28.13`（`probe-bcrypt2.log`），即该环境状况当时没有恢复，重跑只会重复
同样的 9 个环境性失败。**该状况随后自行恢复**：第三次探针 `elapsed=9.05`（`probe-bcrypt3.log`），
同一天的第二次重跑因此全绿（见本节开头）。

回填隔离的两条 leg 本次**未取得**：停止 harness 时它的 EXIT trap 已经 `down -v` 拆掉了隔离
栈，随后手工重跑两条 leg 时 MySQL 不可达（`mysql_real_connect failed: Can't connect to
MySQL server on '127.0.0.1:13399' (111)`），属于拆栈后的操作顺序错误，不是用例结论。

**处置（不削弱断言、不伪造绿色）：** 本阶段没有改任何生产代码与测试代码（B 的结论 k=0，
`git status --porcelain -- service/tests backend/tests` 为空），因此按
`docs/ai/agent-workflow.md` §2.1 规则 1 复用 0016 在同一代码上取得的绿色 L4
（Debug/ASan/UBSan `100% tests passed, 0 tests failed out of 309`，sanitizer 诊断 0），
并把本次能做的部分全部做实（上面的 builds、两个 Python 套件、debug 结果与根因证据）。
宿主健康后重跑一次即可补齐：`bash build/l4-0017/run.sh`（约 10 分钟；**先跑探针**，
`test_bcrypt` 明显慢于 7.1 s 时不要跑，那只会产出环境噪声）。
**该次尝试按 environment-limited 记录；重跑已在同一天完成并全绿（见本节开头），
最终结论以绿色为准。**

### 10.3 推送与 CI（owner 提供只读 token 后，用 `tools/dev/ci-status.sh` 取回）

四次推送的 run 全部 `completed/success`。Actions 分钟 = 各 job 耗时之和：

```text
基线 a8dd333  run 38058123275  ci    clean build 3m17s + full test suite 5m21s + image 2m48s = 11m26s
38f29fd  run 38059689900  ci    clean build 2m44s + full test suite 1m16s                    = 4m00s
         run 38059689891  image  container image and the end-to-end gate 3m00s
4a7995e  run 38059807734  ci    clean build 4m18s + full test suite 1m22s                    = 5m40s
         run 38059807647  image  container image and the end-to-end gate 2m29s
b2a55be  run 38060121842  ci    clean build 3m13s + full test suite 1m25s                    = 4m38s
         run 38060121858  image  container image and the end-to-end gate 2m51s
b1d1b89  run 38061519836  ci    clean build 3m56s + full test suite 1m49s                    = 5m45s
         run 38061519743  image  container image and the end-to-end gate 2m49s
2055e05  run 38064348674  ci    clean build 4m16s + full test suite 1m24s                    = 5m40s
         run 38064348650  image  container image and the end-to-end gate 2m45s
```

结论：**integration job 从基线的 5m21s 降到 1m16s–1m49s**——不再装 Conan、不再 configure
和编译，只下载产物、装 schema、跑 309 个 CTest 用例和两个 Python 套件，每次推送省下约
3.5–4 分钟 Actions 分钟；`clean build` 仍是真正的干净编译（2m44s–4m18s，波动来自 runner 与
Conan 缓存冷热），`image` job 已移入自己的 workflow 并按路径/分支过滤。五次运行（含收口文档
提交 `2055e05`）的 `ci` + `image` 合计 7m00s–8m34s，基线同类推送是 11m26s（三者并行，run
墙钟 5m24s）。**新增的 `Coverage map guard` 步骤在五次运行中全部通过**，即 0017-B 的映射在
runner 上也被强制执行。

## 11. 已知问题与后续

- **记录（偏离说明，非缺陷）**：A 单元的本地复现没有另起 compose 栈，而是在常驻
  `jobpilot-mysql` 容器内新建了一个独立库 `jobpilot_ci`（13 张表、0 行业务数据，创建于
  2026-10-10 14:24:50）来跑 integration job 的步骤；开发库 `jobpilot` 与
  `jobpilot_review_scratch` 未被触碰（跑前跑后 `jobpilot.jobs` 都是 4 行）。这比 §4.1 的
  "独立 compose 项目/端口/卷" 宽松一档；本阶段的 L4 与 C 单元都改用阶段自有的 compose 栈
  （`jobpilot-l4-0017` / `jobpilot-capture`）。
  **追加（2026-10-10，owner 授权清理）：** 该遗留库已在逐表精确核对为 0 行（13/13 张表）后
  `DROP DATABASE jobpilot_ci`，开发库与 `jobpilot_review_scratch` 复核不变（`jobpilot.jobs`
  仍 4 行）。
- **[0017-C 发现，真实缺陷；已由 0017A 修复]** `publish-capture` 发出的 `published_at` 会被手工导入
  端点**静默丢弃**：`source_adapters/jobpilot_sources/capture/publisher.py:220-224` 会把
  capture 的发布日期归一化后放进 payload（并把它当作可发布字段，失败时还有 note），但
  `POST /internal/jobs/import` 的契约（`docs/04-api-design.md:599-625`）与解析器
  （`service/src/job/internal_job_import_controller.cpp:391-477`）都不包含 `published_at`，
  未知键被忽略，所以值进不了 MySQL；公开详情里对应的 `posted_at` 恒为 `null`。观测证据：
  合成 capture（`published_at=2026-09-20 08:00:00`）发布成功（created=1）后
  `GET /api/v1/jobs/{id}` 的 `posted_at` 为 null（见 §10 P3 记录）。影响：人工采集岗位
  的发布日期不可读；`docs/04:488` 的"发布 N 天内"过滤要求 `published_at IS NOT NULL`
  （`service/src/job/job_dao.cpp:217-218`），因此人工记录在该过滤下会被排除——具体过滤名
  与端到端影响**未在本阶段测量**。~~修法二选一（属生产代码，本阶段不做）~~ **0017A 已采用
  (a) 并完成**（`completed/0017A-manual-import-published-at.md`：把 `published_at` 纳入手工
  导入契约 + 解析 + DAO，`COALESCE` 保留语义；见 `docs/04` §5.2 与 0017A 的测试证据）。
  备选 (b)（让 publisher 明确 note 并停止发送）未被采用。
- **仅 owner 可做**（本阶段只归档，不执行）：把仓库转为 public；填 About/描述/话题；
  轮换此前用过的 PAT。**CI 确认已完成**：owner 提供了只读 fine-grained token，
  `tools/dev/ci-status.sh` 取回的四次运行的结论与逐 job 耗时见 §10.3。
- **A 的最终证据缺口**：本机无法读 Actions API（404，无 `gh`/token）。若 owner 愿意提供
  只读 token 或直接粘贴三次运行的总耗时，可把 §7 的 A 验收补成 runner 墙钟对比。
- **0014 §19.5 的最终闭环**仍需要 owner 把真机 JSON 过一遍 C 的脚本；本阶段提供的是
  一条命令 + 脱敏证据行，不代替真机运行。
- **0014 Step 5 / 0015 D6** 维持挂起，需 owner 书面许可或新记录。
- 若 B 报告建议的合并不止 20 个，剩余项作为下一阶段的候选，不在本阶段扩张。
