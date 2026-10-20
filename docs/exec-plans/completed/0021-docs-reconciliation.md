# ExecPlan 0021 — docs/03–04 对账（DB/API 差异处置）

> 状态：**COMPLETED（2026-10-20 合并；VERDICT-0021-closeout PASS 附条件）**
> 分支：`feat/0021-docs-reconciliation`
> 合并：`d75b14e`（PR #4 merge commit）；最终 head `940ad4c`
> 基线：`main@26c69d7`（0020 post-merge 状态提交；父提交 `c07c35a` 为 PR #3 merge）
> 时间盒：1 个工作日；docs-only。
> 批准记录：VERDICT-0021-plan 追加条件 1–7；closeout 附全量 CURRENT_STATE
> 覆盖表、schema/router 检查原始输出、preflight 与 `git diff --check`，并声明
> 本阶段 L0、不引用 0020 测试证据。
> 收口条件：DB-4/H-2（`user_skills`）deferred；permanent docs checker 登记为
> 后续候选；下一阶段由 owner 指定（路线 B：最小 Web UI）。

## 1. 背景与当前状态

`docs/ai/CURRENT_STATE.md`（2026-10-19 交接核验）记录了 7 条文档-代码差异。
0021 的范围严格限定为其中的 **DB-1/DB-2/DB-3、API-1/API-2/API-3/API-4**：

- **DB-1**：`docs/03` §1 写“表数量 11”；schema 13 张表；§2 ER 图也只有 11 个实体。
- **DB-2**：`docs/03` §3“锁定版 DDL”的 `jobs` 缺 0015A 的
  `review_status` / `reviewed_at` / `review_note` 与
  `idx_review_status` / `idx_deadline`；§5 索引策略与 §9.2 增量章节已包含它们。
- **DB-3**：`docs/03` §3 缺 `job_review_items` 与 `resume_artifacts`
  两张表的 `CREATE TABLE`（§9.2/§10.6 有增量记录，但 §3 不是当前全量）。
- **API-1**：`docs/04` §7.2/§9 列出
  `POST /api/v1/ai/resume/optimize` 与
  `POST /api/v1/ai/interview/generate`，`router.cpp` 未注册；文档未标“预留/未实现”。
- **API-2**：`docs/04` §9 internal 汇总只列 import/source-sync 2 条；实际
  9 条 internal（其余 7 条在 §5.6/§10.3 有定义）。
- **API-3**：`router.cpp` 有 `POST /hello`，`docs/04` 不列；它是 P1 回显/自测路由。
- **API-4**：`docs/CLAUDE_CODE_HANDOFF.md` §12 写 “22 public + 8 internal”；
  实际 27 非内部（25 业务公开 + `/health` + `/hello`）+ 9 internal。

0018–0020 已合并；0020 的 `apply_url` 已落地。CURRENT_STATE 是
**日期化报告**，本计划不重写其发现；0021 只修正 live contract 文档，并给历史
文档加日期化注记。

### 1.1 本次实测的差异（2026-10-20，只读）

```text
schema tables: 13
docs/03 §3:     11  (missing: job_review_items, resume_artifacts)
docs/03 §2 ER:  11 entities (same two missing)
jobs columns:   schema 31 / docs §3 28
                missing: review_note, review_status, reviewed_at
jobs indexes:   missing: idx_deadline, idx_review_status
router:         36 = 27 non-internal + 9 internal
docs/04 §9:     30 method entries
                missing implemented: 7 internal + POST /hello
                path alias: docs {task_id} vs router {id}
                reserved but documented: resume/optimize, interview/generate
handoff §12:    22 public + 8 internal (stale)
```

## 2. 目标

1. 让 `docs/03` §1/§2/§3 与 `database/schema/*.sql` 的当前 schema 一致。
2. 让 `docs/04` §7.2/§9 准确表达：已实现路由、预留路由、internal 路由、
   dev-only `/hello`。
3. 修正 `docs/CLAUDE_CODE_HANDOFF.md` §12 的路由计数，并明确它是历史记录
   （日期化 correction，不删除原句）。
4. 在 `docs/PROJECT_DOCUMENTATION_INDEX.md` 增加 DC-10 记录本次修正；
   在 `CURRENT_STATE.md` 增加 dated resolution note（不改写原发现）。
5. 用可复现的检查命令验证 docs 与 schema/router 的一致性。

## 3. 非目标

- **不改任何代码、schema、API 行为、CI workflow**；不删除 `/hello`，不实现
  预留路由。
- 不改 `docs/02`、`docs/05`、`docs/07`、`docs/08` 等历史文档（它们已有
  Superseded banner）。
- 不重写 `CURRENT_STATE.md` 的原始发现；只加 dated resolution。
- 不新增 docs checker/CI guard（可选 follow-up；见 §10）。
- 不做 0022（Web UI）或任何功能开发。
- 不改写 git 历史、不 force push、不直接推 main（合并由 owner 执行）。

## 4. 约束与不变量

- `database/schema/*.sql` 是 schema 的当前事实；`docs/03` 是 contract，
  必须镜像 schema，不得引入 schema 中不存在的列/表。
- `service/src/router.cpp` 是路由事实；`docs/04` 是 contract，必须列出
  已实现路由，并显式标注预留/开发路由。
- 历史文档加 **日期化注记**，不删除原文；CURRENT_STATE 的原始差异条目保留。
- 所有修改是 docs-only（L0）；不运行/不需要 L4。
- 合并后 main 保持绿；未经 VERDICT 不合并。

## 5. 研究证据（当前事实）

### 5.1 schema（2026-10-20）

```text
database/schema/01_user.sql        users, profiles, skills
02_resume.sql                      resumes, resume_versions
03_job.sql                         jobs, job_requirements, job_review_items
04_application.sql                 applications
05_ai.sql                          ai_tasks, interview_records
06_notification.sql                notifications
07_resume_artifact.sql             resume_artifacts
=> 13 tables
```

`jobs` 当前列（schema 03_job.sql）包含 0014 六列、0015A 三列
（`review_status` / `reviewed_at` / `review_note`）、0020 `apply_url`；
索引包含 `idx_review_status`、`idx_deadline`（另有既有索引）。
`docs/03` §3 已含 0020 `apply_url`，所以 DB-2 **只缺 0015A**，不要重复加
`apply_url`。

### 5.2 router（2026-10-20）

- 非内部 27 条：25 条业务公开 + `GET /health` + `POST /hello`。
- 内部 9 条：import、source-sync、4×AI tasks、review-queue、review/{id}、
  review/backfill。
- `/hello` 是 P1 回显/自测路由，不属于公共 API contract。
- 2 条预留路由（resume/optimize、interview/generate）未在 router 注册；
  调用返回 404。

### 5.3 现有文档放置

- `docs/03` §1 表数量、§2 ER、§3 锁定 DDL、§5 索引策略、
  §9.2/§10.6 增量。
- `docs/04` §7.2 预留 AI 路由、§9 汇总、§5.6 review、§10.3 internal AI。
- `docs/CLAUDE_CODE_HANDOFF.md` §12 路由计数；§3 row 5 与 §11 仍有
  0020 之前的 `apply_url` 陈述（见 §10-4）。
- `docs/PROJECT_DOCUMENTATION_INDEX.md` DC-1..DC-9。
- `docs/ai/CURRENT_STATE.md` §4.1/§4.2 差异表。

## 6. 逐条处置

| ID | 位置 | 处置 | 具体改动 |
|---|---|---|---|
| DB-1 | `docs/03` §1 + §2 | **修正** | §1 “表数量 11” → **13**；§2 ER 增加 `job_review_items`、`resume_artifacts` 及关系 |
| DB-2 | `docs/03` §3 `jobs` DDL | **修正** | 加 `review_status TINYINT NOT NULL DEFAULT 0`、`reviewed_at DATETIME NULL`、`review_note VARCHAR(255) NULL`；加 `KEY idx_review_status (review_status, id)`、`KEY idx_deadline (deadline)`；与 schema 的 0020 `apply_url` 共存；§5 保持现状并加交叉引用 |
| DB-3 | `docs/03` §3 | **修正** | 加 `CREATE TABLE job_review_items (...)`（`outcome VARCHAR(24)`）与 `CREATE TABLE resume_artifacts (...)`，内容与 schema 文件一致；放在对应域；保留 §9.2/§10.6 增量章节并标注“§3 是 fresh-install 全量，§9/§10 是升级合同” |
| API-1 | `docs/04` §7.2 + §9 | **标注预留** | §7.2 明确“以下两条为预留契约，当前未注册，调用返回 404”；§9 设“预留/未实现”小表（2 条），不计入已实现计数 |
| API-2 | `docs/04` §7.3 + §9 internal | **修正** | 补齐 9 条 internal：import、source-sync、AI claim/complete/fail/input、review-queue、review/{id}、review/backfill；标注 `X-Internal-Token`、token 为空时不注册、§5.6/§10.3 交叉引用；§7.3 与 §9 的 `{task_id}` 统一为 `{id}`（router 实际参数名） |
| API-3 | `docs/04` §9 | **标注 dev-only** | 列入 `POST /hello` 行，说明“P1 回显/自测，dev-only，不属于公共 API contract”；不删代码，不计入业务公开计数 |
| API-4 | handoff §12 + index DC-10 | **标注历史 + 修正 live 计数** | handoff §12 保留原句，加 dated correction：“22+8 为历史误计；当前 27 非内部 + 9 internal；见 docs/04 §9”；index 加 DC-10；docs/04 §9 的计数写实际值 |

### 6.1 API 计数口径（写进 docs/04 §9）

- **业务公开**：25 条（`/api/v1/...` 的用户/简历/岗位/投递/AI match）。
- **运行/开发**：`GET /health`、`POST /hello`（dev-only）。
- **非内部合计**：27 条（25 + health + hello）。
- **internal**：9 条。
- **预留未实现**：2 条（resume/optimize、interview/generate），不计入已实现。
- 路径参数统一写 `{id}`；`{task_id}` 仅作为历史别名时需注明。

### 6.2 CURRENT_STATE / index

- `CURRENT_STATE.md` 顶部加一条 dated resolution：
  “2026-10-20：DB-1/2/3、API-1/2/3/4 已在 0021 处置；原始发现保留为历史。”
  不改写 §4.1/§4.2 的原始表格。
- `PROJECT_DOCUMENTATION_INDEX.md` 的 DC 表加 DC-10：
  “handoff §12 route count 22+8 vs live 27+9；2026-10-20 修正 live docs +
  dated correction；原始 handoff 句子保留。”
- `docs/ai/project-state.md` 增加 0021 active/closeout 状态（批准后）。

## 7. 影响文件

| 文件 | 改动 |
|---|---|
| `docs/03-database-design.md` | §1 表数量；§2 ER；§3 jobs 列/索引 + 两张表；§3/§9/§10 交叉引用 |
| `docs/04-api-design.md` | §7.2 预留标注；§7.3 + §9 的 `{task_id}`→`{id}`；§9 汇总（internal 9、dev-only /hello、预留 2、计数口径） |
| `docs/CLAUDE_CODE_HANDOFF.md` | §12 dated correction（保留原句）；如批准，§3 row 5/§11 的 `apply_url` dated correction |
| `docs/PROJECT_DOCUMENTATION_INDEX.md` | DC-10；必要时 0021 行 |
| `docs/ai/CURRENT_STATE.md` | 顶部 dated resolution note（不改原发现） |
| `docs/ai/project-state.md` | 0021 active/closeout 状态 |
| `docs/exec-plans/active/0021-docs-reconciliation.md` | 本计划 |
| `docs/exec-plans/completed/0021-docs-reconciliation.md` | 合并后归档（post-merge） |

不改：`database/schema/*.sql`、`service/`、`source_adapters/`、`.github/`、
`tools/`（除非 owner 另行要求 docs checker）。

## 8. 验证命令

### 8.1 schema vs docs/03

```bash
python3 - <<'PY'
import re, glob
from pathlib import Path

schema = {}
for path in glob.glob('database/schema/*.sql'):
    text = Path(path).read_text(encoding='utf-8')
    for m in re.finditer(r'CREATE TABLE(?: IF NOT EXISTS)?\s+`?(\w+)`?\s*\(', text):
        schema[m.group(1)] = text

doc = Path('docs/03-database-design.md').read_text(encoding='utf-8')
sec3 = doc.split('## 3. 建表 DDL', 1)[1].split('## 4.', 1)[0]
doc_tables = set(re.findall(r'CREATE TABLE\s+`?(\w+)`?\s*\(', sec3))
assert len(schema) == 13, sorted(schema)
assert doc_tables == set(schema), (sorted(set(schema) - doc_tables),
                                   sorted(doc_tables - set(schema)))
assert '| 表数量 | 13 |' in doc
assert 'job_review_items' in doc.split('## 2.', 1)[1].split('## 3.', 1)[0]
assert 'resume_artifacts' in doc.split('## 2.', 1)[1].split('## 3.', 1)[0]

def body(text, name):
    m = re.search(
        r'CREATE TABLE(?: IF NOT EXISTS)?\s+`?%s`?\s*\((.*?)\)\s*ENGINE'
        % re.escape(name), text, re.S)
    assert m, name
    return m.group(1)

def cols(text, name):
    out = []
    for line in body(text, name).splitlines():
        line = line.strip().rstrip(',')
        if re.match(r'^(PRIMARY|UNIQUE|KEY|INDEX|CONSTRAINT|FOREIGN)\b',
                    line, re.I):
            continue
        m = re.match(r'`?([A-Za-z_]\w*)`?\s+', line)
        if m:
            out.append(m.group(1))
    return set(out)

need_cols = cols(schema['jobs'], 'jobs')
have_cols = cols(sec3, 'jobs')
assert need_cols <= have_cols, sorted(need_cols - have_cols)
need_idx = set(re.findall(r'(?:UNIQUE KEY|KEY)\s+`?(\w+)`?',
                          body(schema['jobs'], 'jobs')))
have_idx = set(re.findall(r'(?:UNIQUE KEY|KEY)\s+`?(\w+)`?',
                          body(sec3, 'jobs')))
assert need_idx <= have_idx, sorted(need_idx - have_idx)
print('DOCS_SCHEMA_OK', len(schema), len(need_cols), len(need_idx))
PY
```

### 8.2 router vs docs/04（修复后的断言）

```bash
python3 - <<'PY'
import re
from pathlib import Path

router = Path('service/src/router.cpp').read_text(encoding='utf-8')
pairs = re.findall(r'HttpMethod::k(Get|Post|Put|Delete),\s*\n\s*"([^"]+)"',
                   router)
impl = {(m.upper(), p) for m, p in pairs}
non_internal = {x for x in impl if not x[1].startswith('/internal/')}
internal = {x for x in impl if x[1].startswith('/internal/')}
assert len(impl) == 36, len(impl)
assert len(non_internal) == 27, len(non_internal)
assert len(internal) == 9, len(internal)

doc = Path('docs/04-api-design.md').read_text(encoding='utf-8')
summary = doc.split('## 9. 接口清单汇总', 1)[1].split('## 10.', 1)[0]
doc_routes = set()
for line in summary.splitlines():
    m = re.match(
        r'\|\s*(GET|POST|PUT|DELETE|GET/PUT|GET/POST)\s*\|\s*([^ |]+)',
        line)
    if m:
        for method in m.group(1).split('/'):
            doc_routes.add((method, m.group(2)))

missing = impl - doc_routes
reserved = {
    ('POST', '/api/v1/ai/resume/optimize'),
    ('POST', '/api/v1/ai/interview/generate'),
}
extra = doc_routes - impl
assert not missing, sorted(missing)
assert extra == reserved, sorted(extra)
assert internal <= doc_routes
assert ('GET', '/api/v1/ai/tasks/{id}') in doc_routes
assert not any('{task_id}' in p for _, p in doc_routes)
print('DOCS_API_OK', len(impl), len(non_internal), len(internal))
PY
```

### 8.3 stale-claim grep + L0

```bash
# 历史原句应保留，但必须同时存在 dated correction。
grep -n '22 public routes + 8 internal' docs/CLAUDE_CODE_HANDOFF.md
grep -n 'Correction 2026-10-20' docs/CLAUDE_CODE_HANDOFF.md
# 不应再有“表数量 11”的 live 声明（CURRENT_STATE 的历史记录除外）。
grep -rn '表数量.*11' docs/ | grep -v 'CURRENT_STATE\|references/project-state-full' || true
grep -n '{task_id}' docs/04-api-design.md && exit 1 || true
git diff --check
python3 tools/checks/release_preflight.py   # links/readme; 8/0/0 with actionlint
```

No L4 (docs-only, L0 per `docs/ai/agent-workflow.md` §2.1). PR CI may run, but
this stage does not claim a code/sanitizer gate.

## 9. 回滚

- `git revert` 0021 的 docs 提交；无代码/schema/API 行为变化，无数据影响。
- CURRENT_STATE 原始发现保留；回滚后差异记录仍在。
- 不重写历史；合并由 owner 执行。

## 10. 开放问题 / 待批准决策

> **VERDICT-0021-plan 决议（2026-10-20）：** DB-3 用 §3 全量 DDL（批准）；
> API-3 `/hello` 只做 dev-only 文档标注（批准，不改 `router.cpp`）；
> API-4 用带日期更正（批准）；0020 导致的 handoff `apply_url` 旧陈述用带日期
> 更正（批准）；DB-4/H-2 `user_skills` 本阶段 deferred（批准，closeout 必须
> 列出）；永久 docs checker 暂缓但登记为后续计划候选（批准）。
> 追加条件：全量 CURRENT_STATE 覆盖表、H-1 处置、原始检查输出、L0 声明、
> CURRENT_STATE 原文保留、计数规则写死、owner 合并后双绿。

1. **DB-3 方式**：在 §3 复制两张表的完整 DDL（推荐），还是只加“见 §9.2/§10.6”
   指针？推荐完整 DDL，让“锁定版”真的可读。
2. **API-3 `/hello`**：文档标注 dev-only（推荐，docs-only），还是从
   `router.cpp` 删除（代码变更，超出 0021）？
3. **API-4 handoff**：加 dated correction（推荐）还是只更新 docs/04 + index
   DC-10、让 handoff 保持原样？
4. **0020 造成的 handoff 旧陈述**：handoff §3 row 5 仍写 `apply_url`
   **NOT_IMPLEMENTED**，§11 仍写 “**No `apply_url` column**”。这两条在 0020
   合并后已过时，但不在原 7 条清单内。推荐加 dated correction（不改矩阵
   原行），或明确留到后续 docs 清理。
5. **DB-4 / H-2 `user_skills`**：CURRENT_STATE 的 DB-4/H-2（handoff 写
   `user_skills`，实际 `skills`）不在本次 7 条清单内。推荐**不做**，保持 0021
   范围，留给后续 docs 清理。
6. **docs checker**：是否为 docs/03/04 增加常驻
   `tools/checks/docs_contract_check.py` 并接入 CI？推荐本次不加
   （0021 docs-only），用 §8 的临时脚本验证；如需要，另立计划。

## 11. 当前进度

```text
P0 计划 + 差异复核          [x] 完成（VERDICT-0021-plan PASS）
P1 docs/03 DB-1/2/3         [x] 完成（13 表、ER、jobs 列/索引、两张表 DDL）
P2 docs/04 API-1/2/3/4      [x] 完成（reserved、9 internal、dev-only /hello、计数）
P3 handoff/index/state 注记 [x] 完成（dated corrections、DC-10、resolution note）
P4 验证 + REQUEST           [x] 完成（schema/router 原始输出、preflight 7/0/1、diff check rc=0；closeout PASS，PR #4 合并 `d75b14e`）
```

## 12. 批准后动作

1. 把本计划状态改为 ACTIVE；先改 docs/03 + docs/04，再改 handoff/index/state。
2. 按 §8 跑检查；docs-only L0；写 `REQUEST-0021-closeout.md`，停止等待 VERDICT。
3. 不合并、不启动 0022（Web UI）。
