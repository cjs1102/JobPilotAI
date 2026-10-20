# ExecPlan 0022 — 最小 Web UI（本地 demo）

> 状态：**ACTIVE（2026-10-20 批准；VERDICT-0022-plan PASS 附 6 条条件）**
> 分支：`feat/0022-web-ui`
> 基线：`main@70810ca`（0021 post-merge 状态提交；父提交 `d75b14e` 为 PR #4 merge）
> 时间盒：2–3 个工作日；代码/配置/文档。
> 批准记录：简历前置/详情 match/nginx 固定 tag/localStorage/截图/L4 六项决议；
> 条件 1：integration CI 增加 web-smoke 步骤（不改触发器），本地无 nginx 镜像时
> 用 Python 静态+代理回退 runner，closeout 附实际执行输出；条件 2：grep
> innerHTML + 无 CDN；条件 3：链接仅 http(s)+noopener；条件 4：隔离栈拆除证据；
> 条件 5：local-demo 文档；条件 6：owner 合并、main 双绿、不得启动 0023。
> 功能验证用 isolated compose smoke，stage close 按
> `docs/ai/agent-workflow.md` §2.1 跑一次 **L4**；未获 closeout PASS 前不合并。
> 路线：owner 已批准 **B：最小 Web UI**；本阶段不启动 0023 / 新来源 / 自动投递。

## 1. 背景与当前状态

0018–0021 已完成并合并。当前后端是 36 条已注册路由（27 非内部 + 9 internal），
岗位列表/详情/推荐、可解释匹配、简历上传/版本、投递状态机、内部导入都已可用；
`tools/dev/demo-gate.sh` 证明 API 可以端到端跑通。

**缺口**：仓库没有 Web UI，也没有静态文件服务。`handoff §20 V1` 的结论是 C++
HTTP 服务没有 static handler；P1 网络/HTTP 层行为冻结，不能为了 UI 改
`router.cpp` 或 `backend/{network,http}`。

两个必须正面处理的 API 事实：

1. **收藏需要简历版本**：`POST /api/v1/applications` 的 `resume_version_id`
   **必填**（docs/04 §6.1，服务端校验版本属于当前用户）。因此“收藏”不能只发
   `job_id`；最小 UI 必须包含一个**一次性的简历上传/版本选择**步骤，或在未上传时
   明确提示。本计划选择前者，不修改后端。
2. **匹配解释来自列表**：`with_match=true` 只作用于
   `GET /api/v1/jobs`，`GET /api/v1/jobs/recommended` 也返回匹配键；详情
   `GET /api/v1/jobs/{id}` **没有**匹配键。详情页显示列表带入的匹配对象；
   直接刷新/深链时显示“匹配解释请回到列表查看”，不新增后端接口。

## 2. 目标

1. 用 **nginx:alpine 静态容器 + 反向代理**托管原生 HTML/CSS/JS UI，不改 P1。
2. 最小闭环：登录/注册 → 岗位列表/筛选 → 可解释匹配 → 详情 → 打开原始页面 →
   收藏 → 申请状态板。
3. 认证最小实现：登录/注册、token 存放、刷新/401 处理；明确本地 demo 安全边界。
4. 无前端构建步骤；文件结构清晰、可直接阅读和修改。
5. 可复现验证：isolated compose 冒烟、README 截图步骤、`docs/guides/local-demo.md`。
6. 不引入新后端行为、schema、来源或自动投递路径。

## 3. 非目标

- **不做**自动投递、自动填表、登录墙自动化、任何 submit 路径；只打开
  `apply_url`/`source_url` 让用户手动申请。
- **不改 P1**：`service/src/router.cpp`、`backend/{include,src}/jobpilot/{network,http}/`
  不动；不把静态文件服务塞进 C++。
- 不做新来源、爬虫、AI provider、简历定制 UI、AI 任务 UI、通知、RBAC。
- 不做复杂注册流程（邮箱验证、找回密码、OAuth、验证码）、移动端适配、图表。
- 不引入 React/Vue/Vite/Webpack/Tailwind 或任何构建步骤。
- 不宣称生产级认证/HTTPS/多租户；这是本地 demo。
- 不改 `docs/03` schema；不改 `docs/04` 既有 API 行为；不改 CI workflow。
- 不引入浏览器自动化测试框架；提供可选的 geckodriver/Firefox smoke，
  常规冒烟仍用 curl + 手工浏览器验收。

## 4. 约束与不变量

- **P1 冻结**：任何网络/HTTP 行为变化都不属于本阶段。
- **同源**：浏览器只访问 nginx 的 8081；`/api/*` 与 `/health` 由 nginx 代理到
  `backend:8080`，因此不需要 CORS，也不改后端 header/路由。
- **API 是唯一数据源**：UI 不直接连 MySQL/Redis，不重新打分、不补造数据。
- **不提交 secret**：token 只存在浏览器；不写入仓库、日志、URL query。
- **本地 demo 定位**：HTTP、无 TLS、localStorage token、无 refresh token；
  文档明确写“不用于生产”。
- **无构建**：原生 ES modules + 静态 CSS；浏览器直接加载源文件。
- **文档同步**：README、`docs/guides/local-demo.md`、`docs/PROJECT_DOCUMENTATION_INDEX.md`、
  `docs/ai/project-state.md` 在对应阶段更新；设计变更先改 plan/文档再改代码。

## 5. 设计决策

### 5.1 托管方式（a）— nginx:alpine 静态容器 + 反向代理

**选择**：在现有 `docker-compose.yml` 增加一个 opt-in `web` service：

```yaml
  web:
    profiles: ["web"]
    image: ${JOBPILOT_WEB_IMAGE:-nginx:1.27-alpine}
    container_name: ${JOBPILOT_WEB_CONTAINER_NAME:-jobpilot-web}
    restart: unless-stopped
    depends_on:
      backend:
        condition: service_healthy
    ports:
      - "${JOBPILOT_WEB_HOST_PORT:-8081}:80"
    volumes:
      - ./web:/usr/share/nginx/html:ro
      - ./docker/nginx/default.conf:/etc/nginx/conf.d/default.conf:ro
```

运行命令（backend 仍走既有 `app` profile）：

```bash
docker compose --profile app --profile web up -d
# UI: http://127.0.0.1:8081
```

请求流：

```text
browser :8081
  ├── /            -> nginx static /usr/share/nginx/html
  ├── /api/*       -> proxy_pass http://backend:8080  (path preserved)
  └── /health      -> proxy_pass http://backend:8080
```

`docker/nginx/default.conf` 的锁定内容：

```nginx
server {
    listen 80;
    server_name _;
    root /usr/share/nginx/html;
    index index.html;

    # PDF/TXT/MD upload: backend accepts up to 10 MiB; leave headroom.
    client_max_body_size 12m;

    add_header X-Content-Type-Options nosniff always;
    add_header Referrer-Policy no-referrer always;
    add_header X-Frame-Options DENY always;
    add_header Content-Security-Policy "default-src 'self'; script-src 'self'; style-src 'self'; img-src 'self' data:; connect-src 'self'; font-src 'self'; base-uri 'none'; frame-ancestors 'none'; form-action 'self'" always;

    location /api/ {
        proxy_pass http://backend:8080;
        proxy_http_version 1.1;
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
        proxy_set_header X-Forwarded-Proto $scheme;
        proxy_set_header Authorization $http_authorization;
        proxy_read_timeout 60s;
    }

    location = /health {
        proxy_pass http://backend:8080;
        proxy_http_version 1.1;
        proxy_set_header Host $host;
        proxy_set_header X-Forwarded-Proto $scheme;
    }

    location / {
        try_files $uri $uri/ /index.html;
    }
}
```

**被否方案与理由**：

| 方案 | 否决理由 |
|---|---|
| C++ 服务直接托管静态文件 | P1 冻结；当前 router 无 static handler；会改网络/HTTP 行为，超出 0022 |
| 静态文件另起端口，浏览器直连 `:8080/api` | 后端没有 CORS；要改 P1/全局 header；同源代理更简单、无 CORS |
| Node/Vite/React 构建 | 引入 Node、依赖、构建产物和锁文件；违反“最小、无构建步骤” |
| 把 `web/` COPY 进新 nginx 镜像 | 多一个 image build/发布路径；本地 demo 用只读 bind mount 足够；生产打包留待未来 |
| Python `http.server` 开发服务器 | 无反向代理、无安全 header、请求体/缓存行为不适合做验收；nginx 是标准静态容器 |
| 在 C++ 里加反向代理/CORS | 同样触碰 P1，且没有必要 |

**已知环境限制**：本 dev host 无法从 Docker Hub 拉基础镜像（handoff §9）。`web`
service 需要 `nginx:1.27-alpine`；若本地没有该镜像且无法拉取，冒烟必须如实报告
“environment-limited”，不能在 closeout 写成通过。可预拉/使用 `JOBPILOT_WEB_IMAGE`
指定的本地 nginx 镜像，或把冒烟放到可拉镜像的 runner。

### 5.2 页面范围（b）— 最小闭环

单页 shell + hash 路由，5 个视图：

| 视图 | 内容 | 最小交互 |
|---|---|---|
| 登录/注册 | 两个 tab；email/username/password | 注册后自动登录；错误内联显示 |
| 岗位列表 | 筛选栏 + 结果列表 + 分页 | keyword/location/salary_min/salary_max/education_max/job_nature/open_only/sort；搜索/重置/上一页/下一页 |
| 推荐 | `GET /api/v1/jobs/recommended`，复用列表渲染 | 登录后可用；显示 score/reasons/deal_breakers |
| 岗位详情 | 标题/公司/地点/薪资/描述/技能要求/freshness/deadline | 打开原始公告（`source_url`）、打开申请链接（`apply_url`，可空）；收藏 |
| 申请状态板 | 当前用户投递列表 + 简历设置 | 展示状态；推进到下一状态；拒绝；刷新；上传/选择简历版本 |

匹配展示：

- 列表/推荐：每行显示 `score`、`score_version`、`matched_skills`、
  `missing_skills`、`reasons[]`、`deal_breakers[]`、`deadline_urgency`（有则显示）。
- 详情：显示从列表/推荐带入的 match 对象；深链/刷新无 match 时显示
  “匹配解释来自列表；请返回列表查看”，不请求新接口。
- 未登录：列表不带 `with_match`；推荐视图显示登录提示。

筛选参数（与 docs/04 §5.1 一致，非法值由后端 400，UI 显示错误）：

```text
keyword, location, salary_min, salary_max, education_max,
job_nature, open_only, sort, page, page_size
```

列表响应分页：`data.list`、`data.total`、`data.page`、`data.page_size`；
默认 `page=1&page_size=20`，上一页/下一页根据 `total` 计算。

**明确不做**：复杂注册流程、移动端专门布局、框架、构建步骤、图表、
简历定制 UI、AI 任务 UI、通知、角色/权限、自动填表/提交。

### 5.3 认证最小实现（c）

端点（docs/04 §3，已实现，不改）：

```text
POST /api/v1/users/register  {username,email,password} -> {user_id}
POST /api/v1/users/login     {email,password} -> {token,user_id}
GET  /api/v1/users/me        Authorization: Bearer <token>
```

Token 与刷新策略：

- 登录成功后把 `token` 存 `localStorage["jobpilot.token"]`；用户信息从
  `/users/me` 读取，不把密码或完整用户对象写入 localStorage。
- 页面启动时若有 token，调用 `GET /users/me` 验证；成功进入应用，失败
  （HTTP 401 / code 20001/20002）清除 token 并显示登录页。
- **没有 refresh token**：服务端 TTL 24h；过期即重新登录。页面刷新通过
  `/me` 重新校验，不做静默续期。
- 任意请求收到 401/20001/20002：清 token、清内存用户、显示登录页；保留
  hash 目标可选，不自动重放写请求。
- 登出：清 token、清 `resumeVersionId`、清用户缓存，回到登录页；没有服务端
  logout 端点，不伪造。
- 注册：`POST /register` 成功后立即 `POST /login`；注册接口不返回 token。

**安全边界（本地 demo，不宣称生产级）**：

| 风险 | 处理 |
|---|---|
| XSS | 所有 API/用户文本用 `textContent`/DOM 创建，禁止 `innerHTML` 插入数据；CSP `script-src 'self'`；无第三方脚本/CDN |
| Token 存储 | localStorage 便于刷新保留；明确 XSS 可读风险；CSP + 不用 `innerHTML` 降低风险；不写 URL/日志 |
| 开放跳转 | `apply_url`/`source_url` 仅允许 `http:`/`https:`，用 `new URL` 校验；`target=_blank` + `rel=noopener noreferrer` |
| 自动投递 | 不存在 submit 按钮/接口；只打开原页面，由用户手动申请 |
| 传输 | 本地 HTTP，无 TLS；不宣称生产可用 |
| 凭据 | 不把密码/token 写入仓库、console、URL query、错误信息 |
| 错误回显 | 只显示后端 `message` 文本；不渲染 HTML |
| 请求体 | 简历上传 10 MiB 上限；nginx `client_max_body_size 12m` 留余量 |

### 5.4 前端结构（d）— 原生 HTML/CSS/JS，无构建

- 原生 ES modules（`<script type="module" src="js/app.js">`），现代浏览器直接加载；
  **无包管理器、无 bundler、无编译步骤**。
- 单页 shell：`index.html` 内含登录视图、应用头部、四个内容视图和
  `<template>`/容器；模块用 DOM API 渲染。
- 状态只在内存 + 两个 localStorage key：`jobpilot.token`、
  `jobpilot.resumeVersionId`（可选）；用户/简历对象刷新时重新拉取。
- 路由：`#/login`、`#/jobs`、`#/jobs/{id}`、`#/recommended`、`#/applications`；
  未知 hash → `#/jobs`（未登录 → `#/login`）。
- 可访问性基础：label + input 关联、按钮文字、焦点样式、语义标题；不做完整
  WCAG 审计。
- 无内联 style/script（CSP 要求）；颜色/间距在 `styles.css`。

### 5.5 API 映射

| UI 动作 | API | 鉴权 | 备注 |
|---|---|---|---|
| 注册 | `POST /api/v1/users/register` | 否 | 成功后 login |
| 登录 | `POST /api/v1/users/login` | 否 | 存 token |
| 启动校验 | `GET /api/v1/users/me` | Bearer | 401 → 登录页 |
| 搜索 | `GET /api/v1/jobs?...` | 否 | 登录时加 `with_match=true` |
| 推荐 | `GET /api/v1/jobs/recommended?...` | Bearer | 返回匹配键 |
| 详情 | `GET /api/v1/jobs/{id}` | 否 | 无 match 键；用带入对象 |
| 简历上传 | `POST /api/v1/resumes/upload` (multipart: file,title) | Bearer | 返回 resume_id |
| 版本列表 | `GET /api/v1/resumes/{id}/versions` | Bearer | 取 `list[].id` = version_id |
| 收藏 | `POST /api/v1/applications` `{job_id,resume_version_id}` | Bearer | 状态 0；重复 40004 |
| 状态板 | `GET /api/v1/applications?page=&page_size=` | Bearer | 当前用户 |
| 推进/拒绝 | `PUT /api/v1/applications/{id}/status` `{status}` | Bearer | 0→1→…→6；非终态→7；40005 |

**简历前置**：收藏前必须有 `resumeVersionId`。UI 在“申请状态板”提供
“简历设置”：选择 PDF/TXT/MD + title → upload → 拉版本列表 → 选一个版本
（默认最新）→ 存 `resumeVersionId`。若未设置，收藏按钮显示
“请先上传简历”，点击跳到简历设置；不修改后端、不伪造版本。

**收藏重复**：后端 40004；UI 显示“已收藏/已存在”，并刷新状态板。

**状态标签**：`0 收藏 / 1 待投递 / 2 已投递 / 3 笔试 / 4 一面 / 5 二面 /
6 Offer / 7 拒绝`。UI 只提供“推进到下一状态”（0–5）和“拒绝”（0–6→7）；
服务端仍做最终校验，40005 内联显示。

**链接**：详情页两个按钮：
- `apply_url` 非空 → “打开申请链接”；
- `source_url` 非空 → “打开原始公告”；
- 都为空 → 显示“来源未提供链接”，不推导、不拼接。
合成数据 `docs/examples/job-import.json` 的 URL 是 `example.invalid`，只用于
跳转行为验收，不代表真实岗位。

### 5.6 验证（e）

1. **静态/配置检查**：
   ```bash
   docker compose --profile app --profile web config -q
   python3 tools/checks/release_preflight.py
   git diff --check
   ```
2. **隔离 compose 冒烟** `tools/dev/web-smoke.sh`：
   - 独立 project/端口/容器名/volume，绝不碰 dev stack；
   - 启动 MySQL + Redis + backend + web（`--profile app --profile web`）；
   - 等待 `GET http://127.0.0.1:$WEB_PORT/health` 通过 nginx 返回；
   - `GET /` 返回 HTML 且包含 `JobPilot`；
   - 通过 nginx 注册/login；上传一份 `.txt` 简历；取 `version_id`；
   - 用 `X-Internal-Token` 导入 `docs/examples/job-import.json`；
   - `GET /api/v1/jobs?keyword=C%2B%2B` 有行；`GET /api/v1/jobs/{id}` 详情；
   - `POST /api/v1/applications` 用 `resume_version_id` 收藏；
   - `PUT` 推进一个合法状态；`GET /api/v1/applications` 断言状态；
   - 失败时打印 web/backend 日志并 teardown；成功打印 `WEB_SMOKE_OK`。
   - 若 nginx 镜像不可得，脚本必须失败并说明 environment-limited，不能假绿。
3. **手工浏览器验收**：
   - 注册 → 登录 → 上传简历 → 搜索/筛选 → 推荐 → 匹配理由/deal-breaker →
     详情 → 打开 `source_url`/`apply_url` → 收藏 → 状态板推进/拒绝 → 刷新后仍在；
   - 未登录访问列表（无匹配）和推荐（登录提示）；
   - 401/过期路径：手工删除 localStorage token 后刷新，应回登录页。
4. **README 截图步骤**：在 README 的 Web UI 小节写清楚启动、URL、演示顺序、
   建议截图（列表、匹配理由、详情链接、状态板）、只使用合成数据、截图前
   清除个人信息；具体命令与路径见 `docs/guides/local-demo.md`。
5. **docs/guides/local-demo.md**：前置条件、`docker compose` 命令、demo 账号/
   简历上传、五个视图 walkthrough、故障排查（502/401/40004/40005/镜像拉取）、
   安全边界与非目标。
6. **stage close**：按 `docs/ai/agent-workflow.md` §2.1 跑一次 **L4**
   （Debug/ASan/UBSan、service/root Release builds、Python suites），
   加 web smoke + preflight + diff check；本阶段不新增并发，不跑 TSan。
   L4 证据必须与实际 head 对应；若 UI 改动后重跑，说明失效范围。

## 6. 文件结构

```text
web/
  index.html              # shell: auth + app views, semantic containers
  css/
    styles.css            # layout, cards, forms, status badges, responsive basics
  js/
    app.js                # boot, hash router, nav, auth state, view lifecycle
    api.js                # fetch wrapper, token, error envelope, 401 handling
    auth.js               # register/login/me/logout bindings
    dom.js                # el/text/link/clear/safeUrl/statusLabel/format helpers
    jobs.js               # filters, list/recommended, match, detail, favourite
    applications.js       # resume setup, application list, status transitions
docker/nginx/
  default.conf            # static root + /api and /health reverse proxy + headers
tools/dev/
  web-smoke.sh            # isolated compose smoke; prints WEB_SMOKE_OK
docs/guides/
  local-demo.md           # setup, walkthrough, screenshots, troubleshooting
```

修改：

```text
docker-compose.yml        # opt-in web profile (no change to default services)
.env.example              # JOBPILOT_WEB_HOST_PORT=8081 (and optional image/name)
README.md                 # Web UI quick start + screenshot steps + capability update
docs/PROJECT_DOCUMENTATION_INDEX.md  # web/ guide rows
docs/ai/project-state.md  # 0022 active/completed evidence
```

不改：`service/`、`backend/`、`database/`、`source_adapters/`、`.github/`、
`docs/03`、`docs/04`（除非发现真实契约错误，另立计划）。

## 7. 实施任务

### P0 — 计划与评审（本文件）

- [x] 写本计划。
- [ ] 写 `~/jobpilot-review-bus/REQUEST-0022-plan.md`，停止等待 VERDICT。
- [ ] 未获批准前不写代码/配置。

### P1 — 托管骨架（nginx + compose + 静态壳）

**Files**: `docker-compose.yml`, `.env.example`, `docker/nginx/default.conf`,
`web/index.html`, `web/css/styles.css`.

- [ ] 加 `web` profile service（第 5.1 节），不改默认 `docker compose up -d`
      的 MySQL/Redis 行为，不改 `backend` service。
- [ ] 写 nginx 配置（静态 root、`/api/`、`/health`、12m body、CSP/安全 header）。
- [ ] 写最小 `index.html`（品牌、登录占位、应用占位）和 `styles.css`；
      不写业务逻辑。
- [ ] 验证：
      `docker compose --profile app --profile web config -q`；
      启动 isolated/本地栈后 `curl -fsS http://127.0.0.1:8081/` 返回 200；
      `curl -fsS http://127.0.0.1:8081/health` 返回后端 health；
      `curl -fsS 'http://127.0.0.1:8081/api/v1/jobs?page=1&page_size=1'` 返回 JSON。
- [ ] Commit：`feat(web): add the nginx static host and compose profile`.

### P2 — API 客户端与认证

**Files**: `web/js/api.js`, `web/js/auth.js`, `web/js/dom.js`, `web/js/app.js`,
`web/index.html`.

- [ ] `api.js`：`apiFetch(path, {method, body, auth, multipart})`；统一解析
      `{code,message,data}`；`code !== 0` 抛 `ApiError`；401/20001/20002 清
      token 并触发 `auth:expired`；JSON 与 multipart 分开处理。
- [ ] `auth.js`：login/register/me/logout；注册后自动 login；token 存
      `jobpilot.token`；启动校验；表单内联错误。
- [ ] `app.js`：hash 路由 `#/login`、`#/jobs`、`#/recommended`、
      `#/jobs/{id}`、`#/applications`；未登录保护。
- [ ] 验证：浏览器注册/登录/刷新/登出；curl 通过 nginx 验证 login/me；
      删除 token 后刷新回登录页。
- [ ] Commit：`feat(web): add the auth flow and API client`.

### P3 — 岗位列表、筛选、匹配

**Files**: `web/js/jobs.js`, `web/js/dom.js`, `web/index.html`, `web/css/styles.css`.

- [ ] 筛选表单与 query 构建（第 5.2 节参数）；搜索/重置/分页。
- [ ] 列表渲染：标题/公司/地点/薪资/freshness/deadline/source/apply；
      登录时 `with_match=true`；匹配键渲染 score、reasons、deal_breakers。
- [ ] 推荐视图：`GET /jobs/recommended`（需登录）；复用行渲染。
- [ ] 未登录/401/空结果/错误状态。
- [ ] 验证：`web-smoke.sh` 的 search/detail API 部分；手工筛选/分页/匹配。
- [ ] Commit：`feat(web): render the job list, filters and match reasons`.

### P4 — 详情、链接、简历前置与收藏

**Files**: `web/js/jobs.js`, `web/js/applications.js`, `web/index.html`,
`web/css/styles.css`.

- [ ] 详情渲染：description（`white-space: pre-wrap`）、requirements、
      freshness/deadline、`source_url`/`apply_url` 安全链接。
- [ ] 简历设置：文件选择 + title + upload → `GET /resumes/{id}/versions` →
      保存 `resumeVersionId`；展示当前简历/版本；无简历时禁用收藏并提示。
- [ ] 收藏：`POST /applications {job_id,resume_version_id}`；成功/40004/
      未登录/未设简历的状态；成功后刷新状态板。
- [ ] 详情匹配：使用列表带入的 match；深链提示回列表。
- [ ] 验证：手工完整路径；`web-smoke.sh` 的 upload/favourite 部分。
- [ ] Commit：`feat(web): add job detail, resume setup and favourites`.

### P5 — 申请状态板

**Files**: `web/js/applications.js`, `web/index.html`, `web/css/styles.css`.

- [ ] 列表：`GET /applications`，显示 job_title/company/status/apply_time；
      状态标签；空状态；刷新。
- [ ] 推进/拒绝：`PUT /applications/{id}/status`；0–5 下一状态、0–6→7；
      40005 内联；成功后刷新。
- [ ] 验证：`web-smoke.sh` 的 create/update/list 断言；手工推进/拒绝/刷新。
- [ ] Commit：`feat(web): add the application status board`.

### P6 — 文档与截图步骤

**Files**: `README.md`, `docs/guides/local-demo.md`,
`docs/PROJECT_DOCUMENTATION_INDEX.md`, `docs/ai/project-state.md`.

- [ ] README：Web UI 启动命令、URL、演示顺序、截图步骤、能力清单从
      “无 UI” 更新为最小 UI；保留非目标。
- [ ] `docs/guides/local-demo.md`：第 5.6 节内容；合成数据说明；故障排查；
      安全边界；截图建议。
- [ ] Index：新增 `docs/guides/local-demo.md` 与 web files 行；project-state
      0022 active/closeout。
- [ ] 验证：preflight links；README 命令实际可复制；diff check。
- [ ] Commit：`docs(web): document the local demo and screenshot flow`.

### P7 — 收尾验证与 REQUEST

- [x] 运行 `tools/dev/web-smoke.sh`（isolated，Python fallback 与真实 nginx
      binary 两种 server 均通过；后续再跑一次 frozen head）。
- [x] 真实 Firefox/geckodriver browser smoke（登录、简历、搜索/详情、收藏、状态板）。
- [x] 跑一次 L4：Debug/ASan/UBSan 各 312/312（1 个 review-backfill SKIP），
      Python 481/190 OK，backfill scratch 7 passed；日志在 `build/l4-0022/`。
- [x] `python3 tools/checks/release_preflight.py` → 7/0/1；`git diff --check` rc=0。
- [x] 写 `REQUEST-0022-closeout.md`（含 smoke 原始输出、browser smoke、L4、docs 更新、
      回滚）；停止等待 VERDICT。
- [x] 不合并、不启动 0023。

## 8. 验收命令清单

```bash
# config + static/proxy reachability
docker compose --profile app --profile web config -q
curl -fsS http://127.0.0.1:8081/ | grep -q 'JobPilot'
curl -fsS http://127.0.0.1:8081/health
curl -fsS 'http://127.0.0.1:8081/api/v1/jobs?page=1&page_size=5'

# isolated end-to-end smoke (starts and tears down its own stack)
tools/dev/web-smoke.sh

# release hygiene
python3 tools/checks/release_preflight.py
git diff --check

# stage close (once, frozen head)
# Debug/ASan/UBSan CTest + service/root Release builds + Python suites;
# raw logs saved; no TSan (no new concurrency).
```

手工验收清单（必须真实点击，不以 curl 代替 UI 交互）：

- [ ] 注册新用户并自动登录；刷新后仍登录；登出后回登录页。
- [ ] 列表搜索 `C++`，筛选 location/salary/education/job_nature/open_only/sort。
- [ ] 登录后列表显示 score/reasons/deal_breakers；推荐视图可用。
- [ ] 详情显示技能要求；`apply_url` 为空时显示原始公告按钮；链接新窗口打开。
- [ ] 未上传简历时收藏被阻止并提示；上传 `.txt` 后收藏成功；重复收藏提示 40004。
- [ ] 状态板显示收藏记录；推进到下一状态；拒绝；刷新后状态保留。
- [ ] 删除 localStorage token 后刷新回登录页；无 console 错误/敏感信息。

## 9. 回滚

- 停止/清理：`docker compose --profile web down`（不删 MySQL/Redis volume）。
- 删除 `web/`、`docker/nginx/`、`tools/dev/web-smoke.sh`、`docs/guides/local-demo.md`，
  移除 `docker-compose.yml` 的 `web` service 和 `.env.example` 的 web 端口。
- 无数据库迁移、无 schema 改动、无后端/P1 改动；回滚不影响既有 API/数据。
- 合并后回滚用 `git revert`；不重写历史、不 force push。

## 10. 风险与开放问题

1. **收藏的前置条件（最大范围风险）**：`POST /applications` 必须带
   `resume_version_id`。本计划选择在 UI 增加一次简历上传/版本选择；若 owner
   希望“收藏不需要简历”，那需要单独批准的后端 API 变更，0022 不做。
2. **详情匹配**：详情 API 无 match 键；本计划用列表带入对象，深链无解释。
   若必须在详情页独立计算，需要新增后端参数/接口，另立计划。
3. **nginx 镜像可得性**：dev host 不能拉 Docker Hub；无本地 nginx 镜像时
   smoke 只能标 environment-limited，不能假绿。
4. **localStorage token**：本地 demo 可接受；XSS 风险用 CSP + 禁止
   `innerHTML` 降低，但不宣称生产安全。若 owner 要求更强隔离，可改
   sessionStorage 或后端 HttpOnly cookie，需另立计划。
5. **无浏览器自动化**：冒烟覆盖页面可达 + API 通过代理 + 关键写操作；
   实际点击路径靠手工验收。是否加 Playwright 由 owner 决定，0022 不引入。
6. **L4 时间成本**：按项目规则 stage close 跑一次；若时间盒紧张，先完成
   功能与 smoke，再在冻结 head 上跑 L4，不并发。
7. **端口冲突**：web 默认 8081，可通过 `JOBPILOT_WEB_HOST_PORT` 覆盖；
   smoke 使用独立端口/项目。
8. **截图是否提交**：本计划只写截图步骤，不提交含个人信息的二进制；如需
   README 内嵌图片，owner 提供合成数据截图后再加。

## 11. 当前进度

```text
P0 计划 + REQUEST           [x] PASS（VERDICT-0022-plan）
P1 托管骨架                 [x] 完成（compose web profile + nginx config）
P2 认证/API client          [x] 完成（api/auth/dom/app）
P3 列表/筛选/匹配           [x] 完成（jobs.js + 筛选/分页/match）
P4 详情/简历/收藏           [x] 完成（detail + resume prerequisite + favourite）
P5 状态板                   [x] 完成（applications.js + 状态推进/拒绝）
P6 文档/截图步骤            [x] 完成（README + local-demo + index/project-state）
P7 smoke + L4 + closeout    [x] 验证完成（nginx/Python/Firefox smoke、L4 312/312 ×3、preflight 7/0/1）；closeout 待评审
```

## 12. 批准后动作

1. 把状态改为 ACTIVE；按 P1→P7 小步实现，每步 commit + 最小验证。
2. 先改配置/静态壳，再改 JS；不碰 P1、不改后端契约。
3. 收口写 `REQUEST-0022-closeout.md`，附 smoke 原始输出、手工验收、L4 日志、
   preflight/diff；停止等待 VERDICT；合并由 owner 执行。
