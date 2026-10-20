# ExecPlan 0023 — Release readiness

> 状态：**ACTIVE（2026-10-20 批准；VERDICT-0023-plan PASS 附条件）**
> 分支：`feat/0023-release-readiness`
> 基线：`main@a346aa9`（0022 merge `38bd484`，PR #5；post-merge state `a346aa9`）
> 时间盒：2 个工作日；发布准备，不做产品化功能。
> 0022 合并后 main CI：`ci` `39446877179`（5m58s）与 `image` `39446876743`（2m58s）双绿；
> PR #5 的 `Web UI smoke (Python fallback)` 步骤 `39445774298` 为 success。
> 路线：owner `STATUS-release-roadmap.md` 的 0023；0024（owner 发布动作）不在本阶段。
> 历史披露 A（owner 2026-10-20 决定）：接受，不改写历史；SECURITY.md 记录事实。
> 批准记录：CoC deferred；PyYAML 6.0.3 + hashes；MariaDB image CI 必须绿；
> TSan 最小修复在范围内、超范围停止；libbcrypt 回退 hash+unresolved；docs checker
> deferred；benchmark 9+3×3 样本/2h；L4 一次（不含 TSan）；owner 合并。
> 按 P1→P8 小步实施；stage close 按 `docs/ai/agent-workflow.md` §2.1 跑一次 L4。

## 1. 背景与当前状态

0022 最小 Web UI 已合并（PR #5，merge `38bd484`）。项目现在是：C++17 后端 +
MySQL 8 + Redis 7 + Python source_adapters/ai_worker + 原生无构建 Web UI；P1
网络/HTTP 层保持冻结。发布路线图（supervisor `STATUS-release-roadmap.md`）要求
0023 把仓库从“可运行”推进到“可公开、可复现、可贡献”：

- 缺 `SECURITY.md`、`CONTRIBUTING.md`（`CODE_OF_CONDUCT.md` 可选）。
- Python 依赖仍只有 `PyYAML>=6.0`，没有 lock；CI 依赖 runner 的系统 PyYAML。
- 发布镜像仍安装 Oracle `libmysqlclient21`；MariaDB Connector/C 的许可/构建证据已
  完成，但三行切换未应用（handoff §5 row 33 / V8）。
- TSan 预设存在，但当前提交没有首跑记录（handoff §5 row 29 / V6）。
- vendored `third_party/libbcrypt` 没有上游 commit（PROVENANCE 明确记录）。
- `docs/03`/`docs/04` 在 0021 对账后没有常驻 checker（0021 deferred）。
- `docs/benchmarks/README.md` 是 2026-10-06 的单点基线；发布材料需要新的
  thread/connection 扫描、DB-backed `GET /api/v1/jobs` 样本和 CPU/RSS 记录。
- Web UI 是本地 demo（HTTP/localStorage），不得宣称生产级；不涉及生产 CSRF/HTTPS
  承诺。

**A 历史披露（owner 决定）**：当前 `tools/checks/history_secret_scan.sh` 的历史
扫描结果是 **190 commits, 0 credential-shape findings**，另有绝对 home path ×10、
RFC1918 ×14（不打印值）；历史包含旧作者邮箱。决定是**接受、不重写历史、不 force
push**；SECURITY.md 必须写清事实与决定，不写法律结论。

## 2. 目标

1. 发布社区文档：`SECURITY.md`（含 A 披露）、`CONTRIBUTING.md`；可选 CoC。
2. Python 可复现：为两个包加 lock（PyYAML 精确版本 + hashes），CI 用 lock 安装。
3. 应用 MariaDB Connector/C 切换，并由 image CI + `ldd` 验证运行镜像。
4. 跑并记录 TSan 首轮（`tsan` + `tsan-external`），处理真实发现或如实记录限制。
5. 补 libbcrypt 上游版本/内容哈希证据，消除“无 revision”空白。
6. （可选）永久 docs checker，防止 0021 对账再次漂移。
7. 刷新发布性能材料：thread/connection 扫描、CPU/RSS、DB-backed jobs 样本。
8. 更新 README/index/RELEASE_CHECKLIST/project-state，形成可审阅的 release readiness 证据。

## 3. 非目标

- 不做产品化：通知、自动投递、多租户、SaaS、SLA、托管服务。
- 不改 P1 网络/HTTP 行为；不改 `router.cpp`；不新增业务 API。
- 不切 Public、不改 GitHub About/分支保护、不处理 owner 发布动作（0024）。
- 不重写 git 历史、不 force push、不重建仓库（A 决定）。
- 不新增来源、不扩展牛客矩阵、不做 Moka。
- 不引入生产级认证/HTTPS；Web UI 仍是本地 demo。
- 不把 benchmark 数字写成硬性回归阈值或跨机器承诺。
- 不做新的 C++ 功能；TSan 若发现真实 race，只做最小修复 + 回归测试。

## 4. 约束与不变量

- **A 决定**：历史不改写；SECURITY.md 只陈述事实与决定，不打印敏感值。
- **P1 冻结**：除 TSan 发现的真实 race 的最小修复外，不碰网络/HTTP 层；若需要
  P1 行为变化，另立计划。
- **无 secret**：文档/配置/日志不得写入 token、密码、个人邮箱、home path 值；
  history scanner 只记录计数。
- **可复现**：Python lock 必须有精确版本 + hashes；MariaDB 镜像由 CI 构建验证；
  benchmark 记录 commit/日期/环境/wrk 版本。
- **独立测量**：benchmark 用独立 compose project/端口/volume/DB；不得用开发栈。
- **文档同步**：README、RELEASE_CHECKLIST、THIRD_PARTY_NOTICES、project-state 与
  实际改动同步；closeout 附原始输出。
- 合并由 owner；worker 不推 main、不合并、不启动 0024。

## 5. 工作流与设计

### 5.1 社区/安全文档（P1）

**`SECURITY.md`（必需）**：

- Supported versions：`main` + latest release（无 LTS 承诺）。
- Reporting：GitHub private vulnerability reporting / Security Advisories；不要在
  公开 issue 贴漏洞；不提供个人邮箱。
- Scope：local-first/self-hosted；Web UI 是 local demo（HTTP、localStorage token、
  无生产认证）；无自动投递/提交路径；不承担托管 SLA。
- Secrets policy：仓库不接收 token/密码/个人数据；preflight 的 secret/local-path
  扫描是强制门。
- **History metadata disclosure（A）**：写 2026-10-20 扫描事实（190 commits、
  0 credential-shape、home path ×10、RFC1918 ×14、旧作者邮箱存在），明确“owner
  接受、历史不重写、不 force push”；不打印具体路径/地址；不构成法律意见。
- Dependencies/limitations：PyYAML、MariaDB Connector/C、libbcrypt provenance、
  local demo 限制；链接到 `THIRD_PARTY_NOTICES.md`。

**`CONTRIBUTING.md`（必需）**：

- Prerequisites、`tools/dev/bootstrap.sh`、CMake presets、测试/labels、L4 规则。
- P1 freeze、Controller→Service→DAO、docs-first、代码规范链接（`AGENTS.md`,
  `docs/06-coding-standard.md`, `docs/ai/agent-workflow.md`）。
- Python lock 安装命令；benchmark 不默认跑。
- Commit/PR：小步、测试证据、no secrets、docs 同步、owner merge、分支命名。
- 不提交生成物/build；不回写历史。

**`CODE_OF_CONDUCT.md`（可选）**：只有在 owner 给出可用的执行联系方式时才加
Contributor Covenant 2.1；否则显式 deferred（理由：没有可用的举报渠道比一个空
CoC 更诚实）。本计划默认 deferred，closeout 记录。

**README/index/RELEASE_CHECKLIST**：加 Security/Contributing 链接；release
checklist 的 SECURITY/CONTRIBUTING 缺失项改为完成；索引新增文档行。

### 5.2 Python lock（P2）

- 新增：
  - `source_adapters/requirements.lock`
  - `ai_worker/requirements.lock`
- 内容：`PyYAML==6.0.3`（PyPI 当前可用）及该版本 sdist/wheel 的 `sha256` hashes。
  生成方式（临时 venv，不提交工具依赖）：
  ```bash
  python3 -m venv /tmp/jobpilot-lock
  /tmp/jobpilot-lock/bin/pip install --upgrade pip pip-tools
  /tmp/jobpilot-lock/bin/pip-compile --generate-hashes \
      --output-file source_adapters/requirements.lock source_adapters/pyproject.toml
  /tmp/jobpilot-lock/bin/pip-compile --generate-hashes \
      --output-file ai_worker/requirements.lock ai_worker/pyproject.toml
  ```
  若 `pip-compile` 不可用，用 `pip download --no-deps` + `pip hash` 生成同样的
  exact-pin/hash 文件；不提交 pip-tools 到运行时依赖。
- CI `integration`：
  - apt 增加 `python3-venv`；
  - 创建临时 venv，`pip install --require-hashes -r` 两个 lock；
  - Python suites 用 venv 的 python；不依赖 runner 系统 PyYAML。
- `pyproject.toml` 的 `PyYAML>=6.0` floor 保留；lock 是复现层，不改变包元数据。
- 文档：CONTRIBUTING 写 lock 安装/更新命令；RELEASE_CHECKLIST 的 Python 行从
  “no lock” 改为 pin + hash；README Quick Start 可保留系统安装，但贡献者用 lock。

### 5.3 MariaDB Connector/C 切换（P3）

`Dockerfile` 三处（与 handoff V8 / RELEASE_CHECKLIST §2 已评估的方案一致）：

```diff
- libmysqlclient-dev \
+ libmariadb-dev libmariadb-dev-compat \
...
- libmysqlclient21 \
+ libmariadb3 \
```

- builder 需要 `libmariadb-dev-compat` 提供 `mysqlclient.pc`/`mysql_config`，现有
  `pkg_check_modules(MYSQLCLIENT REQUIRED IMPORTED_TARGET mysqlclient)` 不改。
- runtime 只装 `libmariadb3`；不装 MariaDB server。
- `.github/workflows/image.yml` 在构建后、demo gate 前增加：
  ```bash
  docker run --rm --entrypoint sh "${JOBPILOT_BACKEND_IMAGE:-jobpilot-backend:local}" -c \
    'ldd build/presets/release/service/jobpilot_service | grep -q "libmariadb.so.3" \
     && ! ldd build/presets/release/service/jobpilot_service | grep -q "libmysqlclient"'
  ```
- image job 继续跑 `tools/dev/demo-gate.sh`（真实 compose + MySQL 8.0）。
- 文档：`THIRD_PARTY_NOTICES.md`、`docs/RELEASE_CHECKLIST.md`、README 技术栈；
  只陈述 LGPL 客户端事实，不做法律结论；旧 Oracle 证据保留为历史。
- 本机不能拉 Docker Hub；本地只做 `docker compose config`/构建不可用时的说明，
  **实际 image 证据必须来自 owner 创建的 PR 的 `image` job**；合并前确认绿。

### 5.4 TSan 首跑（P4）

在冻结代码上，用隔离 MySQL/Redis（project `jobpilot-l4-0023`，端口 14023/17023）：

```bash
cmake --preset tsan
cmake --build --preset tsan -j4
MYSQL_HOST=127.0.0.1 MYSQL_PORT=14023 MYSQL_USER=root MYSQL_PASSWORD=root \
MYSQL_DATABASE=jobpilot REDIS_HOST=127.0.0.1 REDIS_PORT=17023 \
  ctest --preset tsan
MYSQL_... ctest --preset tsan-external
```

- 保存 `build/l4-0023/tsan.log`、`tsan-external.log`、build log。
- 通过标准：项目代码无可复现 race。若 TSan 报告项目 race：最小修复 +
  相关 L1/L2 回归，再重跑 TSan；若是第三方/环境噪声，逐条分类并记录证据。
- 不把 TSan 加入每次 CI/L4；本阶段只记录首跑。更新 handoff §5 row 29/§13、
  RELEASE_CHECKLIST、project-state。

### 5.5 libbcrypt 上游版本（P5）

- 先解析上游：
  ```bash
  git ls-remote https://github.com/rg3/libbcrypt.git HEAD
  curl -sS https://api.github.com/repos/rg3/libbcrypt/commits?per_page=1
  ```
  本机当前对 GitHub TLS 失败（`gnutls_handshake() failed` /
  `SSL_ERROR_SYSCALL`）；若仍失败，记录**确切失败**，不编造 commit。
- 计算 vendored 源文件 SHA-256 manifest：
  ```bash
  find third_party/libbcrypt -type f \( -name '*.c' -o -name '*.h' \) -print0 \
    | sort -z | xargs -0 sha256sum
  ```
- `third_party/libbcrypt/PROVENANCE.md` 增加：上游 URL、尝试日期、结果（commit 或
  “unresolved from this host”）、文件 hash manifest、本地修改状态。
- 同步 `THIRD_PARTY_NOTICES.md`/RELEASE_CHECKLIST 的 libbcrypt 行。若 owner 能提供
  commit，优先写入真实 commit；否则以 hash manifest 作为可复现证据，并在 closeout
  明示未解析上游 revision。

### 5.6 可选永久 docs checker（P6）

**默认 optional/deferred**；若 P1–P5 完成后时间允许，再加：

- `tools/checks/docs_contract_check.py`（stdlib）：
  - schema `database/schema/*.sql` ↔ `docs/03` §1/§3（13 表、列/索引）；
  - `service/src/router.cpp` ↔ `docs/04` §9（36 实现路由、2 reserved、9 internal、
    dev-only `/hello`）；
- 接入 `tools/checks/release_preflight.py` 或 CI `integration`。
- 不改变 P1/API；只读检查。
- 若不做，closeout 记录 deferred 理由与 0021 的临时脚本引用。它不影响 0023 的
  发布 readiness 结论。

### 5.7 压测刷新（P7）

- 扩展 `backend/benchmarks/run_http_wrk.sh`：支持第 5 个参数/环境
  `JOBPILOT_BENCHMARK_PATH`（默认 `/health`），URL = `http://127.0.0.1:$port$path`；
  保持旧调用兼容。
- 新增 `backend/benchmarks/run_benchmark_matrix.sh`：
  - 独立 compose project `jobpilot-bench-0023`，MySQL/Redis 独立端口/volume/DB；
    schema load；release service 二进制；internal token；
  - 生成 1000 条合成岗位（唯一 `source_url`）并用 `POST /internal/jobs/import`
    导入；不提交生成数据；
  - health：threads 2/4/8 × connections 32/128/512；每条 5s warmup + 10s sample，
    3 次取中位数；`wrk --latency` 记录 QPS、p50/p99；
  - DB-backed `GET /api/v1/jobs?page=1&page_size=20`：threads 2/4/8 × 32；
    记录同样的 QPS/p99，注明 MySQL 同机；
  - 每次 sample 并行 `pidstat -u -r -p $service_pid 1`，记录平均 CPU%/RSS；
    每连接内存 = `(peak RSS - idle RSS) / connections`（近似，注明口径）；
  - 原始输出写 `build/benchmarks-0023/`（gitignored）；teardown 独立 project。
- 重建 benchmark preset，重跑 microbench；更新 `docs/benchmarks/README.md`：
  日期 2026-10-2x、commit、wrk 4.1.0、host/kernel、方法、结果表；旧 2026-10-06
  数据保留为历史。
- 不设 pass/fail 阈值；数字注明环境，不跨机器承诺。

### 5.8 文档/状态（P8）

- `README.md`：Security/Contributing 链接；MariaDB 技术栈；Python lock；
  benchmark 指引；不写生产级认证。
- `docs/PROJECT_DOCUMENTATION_INDEX.md`：SECURITY/CONTRIBUTING/benchmark/0023 行。
- `docs/RELEASE_CHECKLIST.md`：SECURITY/CONTRIBUTING、Python lock、MariaDB、
  TSan、libbcrypt、benchmark、A 决定；更新证据日期。
- `THIRD_PARTY_NOTICES.md`：MariaDB/libbcrypt/PyYAML lock。
- `docs/ai/project-state.md`：0023 active/completed；A 决定；MariaDB/TSan/lock/
  benchmark 证据；release roadmap 位置。
- `docs/CLAUDE_CODE_HANDOFF.md`：dated corrections for V6/V8/§5 rows 29/33/§11
  Python lock/libbcrypt；保留原文。

## 6. 文件清单

新增：

```text
SECURITY.md
CONTRIBUTING.md
source_adapters/requirements.lock
ai_worker/requirements.lock
backend/benchmarks/run_benchmark_matrix.sh
docs/exec-plans/active/0023-release-readiness.md
# optional: CODE_OF_CONDUCT.md, tools/checks/docs_contract_check.py
```

修改：

```text
Dockerfile
.github/workflows/image.yml
.github/workflows/ci.yml          # python3-venv + lock install; optional checker
backend/benchmarks/run_http_wrk.sh
third_party/libbcrypt/PROVENANCE.md
THIRD_PARTY_NOTICES.md
docs/RELEASE_CHECKLIST.md
docs/benchmarks/README.md
README.md
docs/PROJECT_DOCUMENTATION_INDEX.md
docs/ai/project-state.md
docs/CLAUDE_CODE_HANDOFF.md
# optional: docs checker integration
```

不改：`service/`、`backend/` 业务代码（TSan 真实 race 的最小修复除外）、
`database/`、`source_adapters/` 运行时代码（lock 文件除外）、P1 网络/HTTP。

## 7. 任务拆分

### P0 — 计划与评审

- [x] 写本计划。
- [ ] 写 `~/jobpilot-review-bus/REQUEST-0023-plan.md`；停止等待 VERDICT。
- [ ] 未批准前不写代码/配置/文档。

### P1 — SECURITY/CONTRIBUTING（+可选 CoC）

- [ ] 写 `SECURITY.md`（含 A 披露、scope、reporting、no-secrets、dependencies）。
- [ ] 写 `CONTRIBUTING.md`（build/test/P1 freeze/lock/commit）。
- [ ] 决定 CoC：仅在有执行联系方式时加；否则 deferred。
- [ ] README/index/RELEASE_CHECKLIST 链接；preflight links。
- [ ] Commit：`docs(release): add security and contribution guides`.

### P2 — Python lock

- [ ] 生成两个 `requirements.lock`（PyYAML 6.0.3 + hashes）。
- [ ] CI integration 加 `python3-venv`、lock install、venv Python suites。
- [ ] 本地用 lock venv 跑 481 + 190；记录版本/rc。
- [ ] 文档更新。
- [ ] Commit：`build(python): pin the runtime dependency with hashes`.

### P3 — MariaDB V8

- [ ] Dockerfile 三行切换；image.yml 加 `ldd` assertion。
- [ ] 本地 `docker compose --profile app build backend`（如镜像可用）/
  `docker compose config`；无镜像则记录 environment-limited。
- [ ] Owner 创建 PR 后确认 `image` job + demo gate 绿；记录 run id。
- [ ] 文档/notices/checklist 更新。
- [ ] Commit：`build(image): use the MariaDB client library`.

### P4 — TSan

- [ ] 构建 tsan preset；启动隔离 MySQL/Redis（14023/17023）。
- [ ] 跑 `ctest --preset tsan` 与 `tsan-external`，保存原始日志。
- [ ] 分类发现；真实 race 最小修复 + 回归；否则记录限制。
- [ ] 更新 handoff/checklist/project-state。
- [ ] Commit：`test(tsan): record the first sanitizer run`。

### P5 — libbcrypt provenance

- [ ] 尝试 GitHub/git ls-remote/镜像；记录成功 commit 或确切失败。
- [ ] 生成源文件 SHA-256 manifest；更新 PROVENANCE/NOTICES/checklist。
- [ ] Commit：`docs(licence): record the libbcrypt provenance`.

### P6 — optional docs checker

- [ ] Go/no-go：若 P1–P5 完成且时间允许，写 `docs_contract_check.py` + preflight/CI。
- [ ] 运行 0021 的 schema/router 检查；记录 36/27/9/2 与 13 表。
- [ ] 若不做，closeout 明确 deferred；不阻塞 0023。
- [ ] Commit（如做）：`test(docs): add the permanent contract checker`.

### P7 — benchmark refresh

- [ ] 扩展 `run_http_wrk.sh` path；新增 `run_benchmark_matrix.sh`。
- [ ] 独立栈 + 1000 合成岗位；health 9 组合 + jobs 3 组合；pidstat CPU/RSS。
- [ ] 重跑 microbench；更新 `docs/benchmarks/README.md`。
- [ ] 保存原始输出到 `build/benchmarks-0023/`；teardown 证据。
- [ ] Commit：`docs(bench): refresh the release performance baseline`.

### P8 — closeout

- [ ] 文档/状态/RELEASE_CHECKLIST 全部同步。
- [ ] 跑一次 L4（Debug/ASan/UBSan + Release builds + Python lock suites）。
- [ ] preflight/history/no_submission/coverage/diff check；image CI（PR）绿。
- [ ] 写 `REQUEST-0023-closeout.md`，附原始输出；停止等待 VERDICT。
- [ ] 不合并、不启动 0024。

## 8. 验收命令

```bash
# docs / community
python3 tools/checks/release_preflight.py
grep -n 'SECURITY\|CONTRIBUTING' README.md docs/PROJECT_DOCUMENTATION_INDEX.md

# history disclosure (counts only)
tools/checks/history_secret_scan.sh

# Python lock
python3 -m venv /tmp/jobpilot-lock
/tmp/jobpilot-lock/bin/pip install --require-hashes -r source_adapters/requirements.lock
/tmp/jobpilot-lock/bin/pip install --require-hashes -r ai_worker/requirements.lock
(cd source_adapters && /tmp/jobpilot-lock/bin/python -m unittest discover -s jobpilot_sources/tests -t .)
(cd ai_worker && /tmp/jobpilot-lock/bin/python -m unittest discover -s jobpilot_ai/tests -t .)

# MariaDB image (CI runner; local may be environment-limited)
docker compose --profile app build backend
docker run --rm --entrypoint sh jobpilot-backend:local -c \
  'ldd build/presets/release/service/jobpilot_service | grep -q libmariadb.so.3'
tools/dev/demo-gate.sh

# TSan
cmake --preset tsan && cmake --build --preset tsan -j4
# ctest --preset tsan / tsan-external against isolated MySQL/Redis

# docs checker (if implemented)
python3 tools/checks/docs_contract_check.py

# benchmark (isolated; see plan)
backend/benchmarks/run_benchmark_matrix.sh

# stage close
python3 tools/checks/release_preflight.py
python3 tools/checks/no_submission_path.py
git diff --check
```

## 9. 回滚

- 每个工作流独立可回滚：撤销 SECURITY/CONTRIBUTING/lock/MariaDB/benchmark 文档即可。
- MariaDB：恢复 Dockerfile 两行和 image workflow 的 `ldd` 步骤；镜像回退到 Oracle
  client；无数据/schema 变化。
- Python lock：删除 lock 与 CI venv 步骤；pyproject floor 不变。
- TSan：只改测试/文档；若真实修复 C++，按修复范围回滚并重跑对应测试。
- Benchmark：只改脚本/文档；原始数据在 gitignored build。
- 无 DB migration、无 P1/业务 API 行为变化；`git revert` 安全。
- 不重写历史、不 force push；合并由 owner。

## 10. 风险与开放问题

1. **CoC**：没有专用举报渠道；默认 deferred，除非 owner 提供联系/执行方式。
2. **Python lock**：`pip-compile`/PyPI 在 CI 可用；hash 文件需覆盖 runner 平台；
   若 CI 安装失败，先修 lock，不得回退到无 hash 的浮动安装。
3. **MariaDB image**：本机无法拉 Docker Hub；image CI 是唯一真实证据，PR 合并前
   必须绿；本地只做 config/文档。
4. **TSan**：可能报真实 race 或第三方噪声；时间盒 0.5–1 天；真实项目 race 必须
   修复或明确阻断发布，不得静默忽略。
5. **libbcrypt**：GitHub 在本机不可达；若 owner/网络无法提供 upstream commit，
   以文件 hash manifest 记录，closeout 明确未解析 revision。
6. **Benchmark**：4 核 VM、负载敏感；数字只作发布材料，跨机器不承诺；必须独占运行，
   不与 L4 并行。
7. **Docs checker**：optional；不做不阻塞；做则不能引入 P1/新依赖。
8. **A 历史披露**：owner 已决定不改写；SECURITY 只写事实，不写法律结论；不打印
   路径/地址值。

## 11. 当前进度

```text
P0 计划 + REQUEST        [ ] 等待 VERDICT-0023-plan
P1 SECURITY/CONTRIBUTING [ ] 未开始
P2 Python lock           [ ] 未开始
P3 MariaDB V8            [ ] 未开始
P4 TSan                  [ ] 未开始
P5 libbcrypt             [ ] 未开始
P6 docs checker (opt)    [ ] 未开始
P7 benchmark             [ ] 未开始
P8 closeout + L4         [ ] 未开始
```

## 12. 批准后动作

1. 状态改 ACTIVE；按 P1→P8 小步实施，每步 commit + 最小验证。
2. 先文档/依赖，再 MariaDB/TSan/benchmark；不碰 P1/业务 API。
3. 收口写 `REQUEST-0023-closeout.md`，附原始输出、image CI、TSan、benchmark、
   L4、preflight；停止等待 VERDICT；合并由 owner。
