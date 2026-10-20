# ExecPlan 0025 — README and external usage guide

> 状态：**PLANNING（等待 VERDICT-0025-plan；批准前不改 README/图片/既有文档）**
> 分支：`docs/0025-readme-and-usage`
> 基线：`main@f788b06`（0023 merged；PR #6 `539202f`）
> 类型：**docs-only，L0**；不改代码/API/schema/CI；不启动 0024。

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
2. Quick start 以 Docker Compose 为主路径（`.env` 两个密码 → compose up →
   build backend → app profile → 8081），并写无 Docker Hub 时的
   `web_static_proxy.py` 回退。
3. 写清真实使用路径：采集/导入 → 搜索筛选 → 匹配解释 → 收藏/状态板 →
   手动去原页面申请；明确本地 demo、无自动投递、数据不出本机。
4. “What's included”按用户价值组织，不用 0014/0015/0022 等内部编号做标题。
5. Verification 写 312 测试、ASan/UBSan/TSan、CI、benchmark **13.4k** 口径
   （含 4 vCPU 环境与“非阈值”说明）。
6. Limits & non-goals 如实写：国内来源仅牛客、20 条/查询、聚合岗 apply_url
   未做、Moka 未验证、无通知/自动投递。
7. 更新 docs index 与 project-state；写 closeout request。

## 3. 非目标

- 不改代码、API、schema、CI workflow、Dockerfile、benchmark 数据或
  `docs/03`/`docs/04` 契约。
- 不新增功能、来源、自动投递、通知或产品化内容。
- 不重写历史文档/计划；不删除或修改 0022/0023 的 closeout 证据。
- 不做中文全文的人工“润色到文学级”；只保证准确、独立、可维护。
- 不提交真实个人数据；截图使用现有合成 data（`example.invalid`、demo 用户）。
- docs-only，不跑 L4；只跑 L0/链接/静态检查。

## 4. 改动文件

| 文件 | 改动 |
|---|---|
| `README.md` | 英文主 README，按外部使用者视角重写；截图、Quick start、How to use、What's included、Verification、Limits、Community/License |
| `README.zh-CN.md` | 中文镜像；独立段落，不在英文 README 混排；顶部语言切换 |
| `docs/images/web-detail.png` | 从 `/tmp/0022-browser-final/web-detail.png` 复制；配图注“详情页=可解释匹配” |
| `docs/images/web-status.png` | 从 `/tmp/0022-browser-final/web-status.png` 复制；配图注“状态板=申请追踪” |
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

## 6. Acceptance / verification commands

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
python3 tools/checks/release_preflight.py      # links; 7/0/1 without actionlint
git diff --check
```

- Manually read both READMEs for language separation and no unverifiable claims.
- Verify screenshot captions mention detail=match and status=tracking.
- Verify no token/password/personal email/home path/RFC1918 values.
- **L0**: no L1–L5/L4; docs-only.

## 7. Rollback

- `git revert` the 0025 docs commit; remove `docs/images/` and
  `README.zh-CN.md` if the commit is not yet merged.
- No code/schema/data/CI impact; existing README can be restored from git.
- Do not rewrite history/force-push; owner merges via PR; ruleset blocks direct
  main pushes.

## 8. Open questions / decisions

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

## 9. Progress

```text
P0 plan + REQUEST       [x] 本文件；等待 VERDICT-0025-plan
P1 screenshots + README [ ] 未开始
P2 README.zh-CN         [ ] 未开始
P3 index/state          [ ] 未开始
P4 L0 checks + closeout [ ] 未开始
```
