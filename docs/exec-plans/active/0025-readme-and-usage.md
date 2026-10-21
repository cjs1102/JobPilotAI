# ExecPlan 0025 — README and external usage guide

> 状态：**IMPLEMENTATION（closeout pending；计划已由 PR #7 合入）**
> 分支：`docs/readme-demo`（实施）；计划原始分支 `docs/0025-readme-and-usage`
> 基线：`main@7acc58f`（计划 PR #7；0023 merge `539202f`）
> 类型：**docs + developer demo tooling**（不碰 core/API/schema/CI）；L0 + shell smoke，不跑 L4；不启动 0024。

## 1. 背景

当前 `README.md` 是 271 行的内部阶段视角：中文正文夹英文 Quick Start，按
“C++ 服务 / 0014 契约 / 0015 Phase / 0022 Web UI”组织，包含计划编号、历史
contract 表和内部运维细节。外部读者无法快速回答“这是什么、怎么跑、怎么用、
我能信什么”。

0022 已产出可用于展示的真实 Firefox 截图：

- `/tmp/0022-browser-final/web-detail.png`（1366×682，详情页 + 可解释匹配）
- `/tmp/0022-browser-final/web-status.png`（1366×682，申请状态板）

## 2. 目标

1. 用**外部使用者视角**重写英文主 README，并附两张真实截图；中文另立
   `README.zh-CN.md`，语言切换链接，**不混合段**。
2. 新增 `tools/dev/demo-up.sh`：Docker 检查 → 缺 `.env` 时随机生成密码 →
   compose up → build backend → 启动 UI（nginx 不可用回退
   `web_static_proxy.py`）→ 默认调用 seed → 打印访问地址。
3. 新增 `tools/dev/demo-seed.sh` 与 `docs/examples/demo-jobs.json`：导入
   10–20 条示例岗位（含 C++ 后端），首次打开即有内容，`source_url` 幂等。
4. Quick Start 改成**一条命令启动**（`tools/dev/demo-up.sh`），并写无 Docker
   Hub 时的回退说明。
5. 写清真实使用路径：采集/导入 → 搜索筛选 → 匹配解释 → 收藏/状态板 →
   手动去原页面申请；明确本地 demo、无自动投递、数据不出本机。
6. “What's included”按用户价值组织，不用 0014/0015/0022 等内部编号做标题。
7. Verification 写 312 测试、ASan/UBSan/TSan、CI、benchmark **13.4k** 口径
   （含 4 vCPU 环境与“非阈值”说明）。
8. Limits & non-goals 如实写：国内来源仅牛客、20 条/查询、聚合岗 apply_url
   未做、Moka 未验证、无通知/自动投递。
9. 更新 docs index 与 project-state；写 closeout request。

## 3. 非目标

- 不改核心代码、API、schema、CI workflow、Dockerfile、benchmark 数据或
  `docs/03`/`docs/04` 契约；两个脚本只放在 `tools/dev/`，不接入生产服务。
- 不新增产品功能、来源、自动投递、通知或外部依赖。
- 不重写历史文档/计划；不删除或修改 0022/0023 的 closeout 证据。
- 不提交真实个人数据；截图与 seed 数据使用合成 `example.invalid`/demo 用户。
- 不跑 L4；脚本验证用 `bash -n` + isolated demo smoke + L0/链接检查。

## 4. 改动文件

| 文件 | 改动 |
|---|---|
| `README.md` | 英文主 README，按外部使用者视角重写；截图、Quick start、How to use、What's included、Verification、Limits、Community/License |
| `README.zh-CN.md` | 中文镜像；独立段落，不在英文 README 混排；顶部语言切换 |
| `docs/images/web-detail.png` | 从 `/tmp/0022-browser-final/web-detail.png` 复制；配图注“详情页=可解释匹配” |
| `docs/images/web-status.png` | 从 `/tmp/0022-browser-final/web-status.png` 复制；配图注“状态板=申请追踪” |
| `tools/dev/demo-up.sh` | 一条命令启动：Docker 检查、缺 `.env` 随机生成密码、compose up、build backend、启动 UI（nginx 不可用回退 `web_static_proxy.py`）、默认 seed、打印 URL |
| `tools/dev/demo-seed.sh` | 导入 10–20 条示例岗位（含 C++ 后端）；等待 backend、校验 created/updated/rejected/needs_review、`source_url` 幂等 |
| `docs/examples/demo-jobs.json` | 12 条合成示例岗位；含 C++ 后端、Linux/MySQL、不同城市；`example.invalid` URL；部分带显式 apply_url |
| `docs/PROJECT_DOCUMENTATION_INDEX.md` | README 描述、README.zh-CN、images 行；0025 active/closeout 行 |
| `docs/ai/project-state.md` | 0025 active/completed；docs-only L0 证据；语言/截图说明 |
| `docs/exec-plans/active/0025-readme-and-usage.md` | 本计划 |

不改 `.gitignore`（当前未忽略 `*.png`；若实施时发现被忽略，再加
`!docs/images/*.png` 并说明）。使用 `git check-ignore` 验证。

## 5. README 结构（英文主文件）

1. Title + one-sentence positioning.
2. Two screenshots with relative paths and captions:
   - `docs/images/web-detail.png` — explainable `rule_v1` match on a detail page.
   - `docs/images/web-status.png` — application/favourite status board.
3. **Quick start**:
   - Prerequisites: Docker Compose, CMake/C++17 for local build, Python 3.
   - `.env`: set `MYSQL_ROOT_PASSWORD` and `MYSQL_PASSWORD`.
   - `docker compose up -d` (MySQL/Redis).
   - `docker compose --profile app build backend`.
   - `docker compose --profile app --profile web up -d` → `http://127.0.0.1:8081`.
   - No Docker Hub: backend + `python3 tools/dev/web_static_proxy.py --root web
     --port 8081 --backend http://127.0.0.1:8080`.
   - Pointer to `docs/guides/local-demo.md` for troubleshooting.
4. **Try the demo**: register → upload resume (required for favourites) → search
   (e.g. `C++`) → read match reasons/deal-breakers → detail/open original link →
   favourite → status board.
5. **How to use** (real path):
   - Collect/import: public ATS adapters + nowcoder; manual capture; sample
     import via internal API. Link to adapter/import docs; do not print tokens.
   - Search/filter; recommended view.
   - Read the deterministic explanation; no LLM requirement.
   - Favourite and track status; the actual application is manual in the
     original page.
   - Local demo statement: data stays on the operator’s machine; no auto-submit.
6. **What's included** by user value (source ingestion, search/ranking,
   explainable match, resume tailoring drafts, application tracking, local Web
   UI, optional AI worker, Docker deployment, reporting/checks). No stage IDs.
7. **Verification**:
   - 312 C++ tests; ASan/UBSan/TSan recorded; CI (ci + image) green at 0023
     merge; `release_preflight`, `no_submission_path`, coverage map.
   - Benchmark: exclusive 2026-10-21 run, health 13.4–16.1k req/s on 4 vCPU,
     DB jobs 180–194 req/s; host-specific, **not a threshold**; link to
     `docs/benchmarks/README.md`.
8. **Limits & non-goals**:
   - China sources: nowcoder only; 20 rows/query, no pagination, `complete=false`.
   - Aggregator apply_url not implemented; Moka unverified.
   - No notifications, no auto-apply/auto-submit, no multi-tenant/SaaS.
   - Local demo auth: HTTP/localStorage; not production auth/TLS.
   - No claim of legal compliance.
9. **Community**: Security/Contributing/Code-of-Conduct deferral/License.
10. Short “Documentation” pointer to `docs/README.md`; no internal plan tables.

`README.zh-CN.md` mirrors the same sections in Chinese, with a top language
switch (`English | 简体中文`). No paragraph mixes English and Chinese beyond
code/identifiers/URLs.

## 6. Demo scripts (developer tooling, no core changes)

### `tools/dev/demo-up.sh`

- Check `docker`, `docker compose`, `curl`, `python3`; fail with a clear message.
- `DEMO_ENV_FILE` default `.env`. If missing, generate a new file with
  `MYSQL_ROOT_PASSWORD`, `MYSQL_PASSWORD`, `INTERNAL_API_TOKEN` using
  `openssl rand -hex 24` (fallback: Python `secrets.token_hex(24)`), plus the
  existing compose defaults. Never print the secret values; preserve an
  existing `.env` byte-for-byte.
- Start the isolated/default compose stack with optional overrides:
  `DEMO_PROJECT` (default `jobpilot-demo`), `DEMO_API_PORT` (18080),
  `DEMO_WEB_PORT` (18081), `DEMO_MYSQL_PORT` (13306), `DEMO_REDIS_PORT`
  (16379), `DEMO_BACKEND_IMAGE` (default local image). `docker compose up -d`
  MySQL/Redis, build the backend, then start `--profile app`.
- Try the `web` profile (nginx). If the image is unavailable or the service
  fails, start `python3 tools/dev/web_static_proxy.py --root web --port
  "$DEMO_WEB_PORT" --backend "http://127.0.0.1:$DEMO_API_PORT"` in the
  background (PID/log under `build/`), wait for `/` and `/health`, and print the
  fallback URL.
- Unless `--no-seed` is passed, run `tools/dev/demo-seed.sh` after the backend
  is healthy; print `DEMO_READY url=http://127.0.0.1:<port>` and the `.env`
  path, never passwords. A failed seed fails the command.
- `--no-seed` exists for tests/advanced users; the README’s one-command Quick
  Start uses the default (seed included).

### `tools/dev/demo-seed.sh`

- Source `DEMO_ENV_FILE`/`.env` read-only; require `INTERNAL_API_TOKEN`.
- Wait for `http://127.0.0.1:${DEMO_API_PORT:-8080}/health`.
- Import `docs/examples/demo-jobs.json` (12 synthetic jobs, including C++
  backend roles) through `POST /internal/jobs/import` with the internal token;
  batch size 1–100; no database writes outside the API.
- `source_url` is the existing idempotency key, so re-running updates rather
  than duplicating. Validate `created + updated <= total`, `rejected == 0`,
  `needs_review == 0`; print `DEMO_SEED_OK total=<n> created=<n> updated=<n>`.
- Never print the token or password; non-zero exit on any failed assertion.

### One-command README Quick Start

```bash
./tools/dev/demo-up.sh
# open the printed http://127.0.0.1:<port>
# reseed later: ./tools/dev/demo-seed.sh
```

The no-Docker-Hub fallback is automatic inside `demo-up.sh`; the README still
documents the explicit `web_static_proxy.py` command for advanced users.

## 7. Acceptance / verification commands

```bash
git check-ignore -v docs/images/web-detail.png docs/images/web-status.png || true
file docs/images/web-detail.png docs/images/web-status.png   # 1366x682 PNG
python3 - <<'PY'
from pathlib import Path
import re
for name in ('README.md', 'README.zh-CN.md'):
    text = Path(name).read_text()
    for needle in ('docs/images/web-detail.png', 'docs/images/web-status.png'):
        assert needle in text, (name, needle)
    # external README must not use internal stage numbers as headings
    for line in text.splitlines():
        if line.startswith('#'):
            assert not re.search(r'\b00(1[0-9]|2[0-9])\b', line), line
print('README_LINKS_AND_HEADINGS_OK')
PY
grep -nE '^#{1,6} .*(0014|0015|0022|0023)' README.md README.zh-CN.md && exit 1 || true
grep -nE '\b00(1[0-9]|2[0-9])\b' README.md README.zh-CN.md && exit 1 || true
bash -n tools/dev/demo-up.sh tools/dev/demo-seed.sh
python3 -m json.tool docs/examples/demo-jobs.json >/dev/null
python3 - <<'PY'
import json
jobs=json.load(open('docs/examples/demo-jobs.json'))['jobs']
assert 10 <= len(jobs) <= 20, len(jobs)
assert any('C++' in j.get('title','') for j in jobs)
assert len({j['source_url'] for j in jobs}) == len(jobs)
print('DEMO_FIXTURE_OK', len(jobs))
PY
# isolated script smoke (override ports/project/env; backend image may be local)
DEMO_PROJECT=jobpilot-demo-0025 \
DEMO_ENV_FILE=/tmp/jobpilot-demo-0025.env \
DEMO_API_PORT=18180 DEMO_WEB_PORT=18181 \
DEMO_MYSQL_PORT=13326 DEMO_REDIS_PORT=16399 \
DEMO_BACKEND_IMAGE=jobpilot-backend:verify \
  tools/dev/demo-up.sh
curl -fsS http://127.0.0.1:18181/ >/dev/null
curl -fsS 'http://127.0.0.1:18181/api/v1/jobs?page=1&page_size=20' >/dev/null
DEMO_PROJECT=jobpilot-demo-0025 DEMO_ENV_FILE=/tmp/jobpilot-demo-0025.env \
DEMO_API_PORT=18180 DEMO_WEB_PORT=18181 tools/dev/demo-seed.sh
# teardown the isolated project explicitly (scripts do not remove data volumes)
docker compose -p jobpilot-demo-0025 down -v
python3 tools/checks/release_preflight.py      # links; 7/0/1 without actionlint
git diff --check
```

- Manually read both READMEs for language separation and no unverifiable claims.
- Verify screenshot captions mention detail=match and status=tracking.
- Verify no token/password/personal email/home path/RFC1918 values.
- **L0 + shell smoke**: no L1–L5/L4; scripts are developer tooling, not core code.

## 8. Rollback

- `git revert` the 0025 docs commit; remove `docs/images/` and
  `README.zh-CN.md` if the commit is not yet merged.
- No code/schema/data/CI impact; existing README can be restored from git.
- Do not rewrite history/force-push; owner merges via PR; ruleset blocks direct
  main pushes.

## 9. Open questions / decisions

1. **Bilingual**: plan chooses English `README.md` + Chinese `README.zh-CN.md`
   (separate), since the user explicitly allowed this and the current README
   mixes languages. If owner wants English-only, drop the Chinese file and the
   language switch.
2. **Screenshots**: use the existing Firefox screenshots as-is (synthetic
   `example.invalid` data); no image editing/cropping unless owner asks.
3. **Benchmark wording**: use 13.4k health as the headline, with the 4-vCPU
   environment and “not a threshold”; do not quote the historical 32.7k as a
   current expectation.
4. **No stage IDs in README**: remove all 00xx references, including body text;
   internal details stay in docs/exec-plans and project-state.
5. **One command**: `demo-up.sh` seeds by default, so the README Quick Start is
   one command; `--no-seed` is the opt-out for advanced/test use. Is that the
   desired default?
6. **Passwords**: when `.env` is missing, generate random MySQL/root and
   internal-token values, write them only to the gitignored `.env`, and never
   print them. Existing `.env` is preserved byte-for-byte.
7. **Fallback UI lifecycle**: if nginx is unavailable, `demo-up.sh` starts the
   Python proxy in the background under `build/` and prints its PID/log path;
   no automatic `down`, so the user can browse. A separate cleanup command is
   documented. Acceptable?
8. **Seed fixture**: 12 synthetic jobs (C++ backend/Linux/MySQL, multiple
   cities, some explicit `apply_url` on `example.invalid`) are enough for a
   first-open demo; no real data.

## 10. Progress

```text
P0 plan + REQUEST       [x] 本文件；plan-only PR #7 merged at 7acc58f
P1 demo scripts + seed fixture [x] demo-up.sh / demo-seed.sh / 12-job fixture
P2 screenshots + README + one-command Quick Start [x] screenshots regenerated; README rewritten
P3 README.zh-CN         [x] 独立中文镜像
P4 index/state          [x] docs index + project-state + handoff corrections
P5 L0 + shell smoke + closeout [x] smoke 完成；REQUEST-0025-closeout 已写；owner PR/merge 待办
```

Execution note (2026-10-21): the planned `/tmp/0022-browser-final/*.png`
originals were missing, so both screenshots were regenerated with
`tools/dev/web-smoke.sh --browser firefox` (Python proxy fallback). The exact
originals are not recoverable; the new files are 1366×682 and use the same
synthetic UI. The isolated launcher smoke used `jobpilot-demo-0025`, ports
18180/18181/13326/16399 and `jobpilot-backend:verify`; it printed
`DEMO_READY`, seeded 12 jobs, preserved `.env` on re-run, and the stack was torn
down with `down -v`. The capture no-retention check found 0 review items and
only DAO test fixtures; see `docs/ai/project-state.md` and the handoff
corrections. Closeout request is the only remaining step.
