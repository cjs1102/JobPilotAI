# Local Web UI demo (0022)

This is the smallest end-to-end UI for JobPilotAI: job search and filters,
explainable `rule_v1` matching, job detail with original/apply links, favourite,
and an application status board. It is a **local demo**, not a production
frontend.

## 依赖

- Docker with the Compose v2 plugin.
- A built backend image (`docker compose --profile app build backend`) or the
  local binary plus MySQL/Redis.
- The Web UI is plain HTML/CSS/JS: no Node, bundler, or npm install.
- The compose `web` profile uses **`nginx:1.27-alpine`** (pinned tag). A host
  that cannot pull Docker Hub can use the stdlib Python fallback below; it serves
  the same `web/` files and proxies `/api`/`/health` to the backend.

## 启动（nginx 形态）

```bash
cp .env.example .env
# fill MYSQL_ROOT_PASSWORD and MYSQL_PASSWORD

docker compose up -d                              # MySQL + Redis
docker compose --profile app build backend        # backend image
docker compose --profile app --profile web up -d  # backend + nginx UI
```

Open **http://127.0.0.1:8081**. The UI is served by nginx and proxies
`/api/*` and `/health` to `backend:8080`; there is no CORS and no second origin.

Port overrides: `JOBPILOT_WEB_HOST_PORT` (default 8081),
`JOBPILOT_BACKEND_HOST_PORT` (default 8080). Image override:
`JOBPILOT_WEB_IMAGE`.

### 镜像记录

`docker/nginx/default.conf` is the reviewed config. The compose image tag is
`nginx:1.27-alpine`. On 2026-10-20 Docker Hub resolved that tag to the
multi-arch manifest digest
`sha256:65645c7bb6a0661892a8b03b89d0743208a18dd2f3f17a54ef4b76fb8e2f2a10`;
the image could not be pulled on this VM (`EOF` from registry-1.docker.io), so
the local checks used the nginx **binary** (1.24.0) plus the same config. After
a successful CI pull, record the platform digest here with:

```bash
docker image inspect nginx:1.27-alpine --format '{{index .RepoDigests 0}}'
```

Do not treat a different local nginx version as the pinned image; the smoke
runner can use a local nginx **binary** for config/routing checks, and prints
`WEB_SMOKE_SERVER=nginx` when it does.

## 无 nginx 镜像时的回退

On the development VM Docker Hub is not reachable. Run the backend normally and
serve the UI with the checked-in stdlib runner:

```bash
docker compose up -d
docker compose --profile app up -d
python3 tools/dev/web_static_proxy.py \
  --root web --port 8081 --backend http://127.0.0.1:8080
```

Open `http://127.0.0.1:8081`. This fallback is for development/CI smoke only; it
does not replace nginx in a deployment.

## 演示流程

1. **注册** a local account (or **登录** an existing one). The token is kept in
   the browser localStorage; the server session is valid for 24 hours.
2. Open **申请状态板 → 简历设置** and upload a PDF/TXT/MD resume. This is
   required: `POST /api/v1/applications` requires `resume_version_id`, so a
   favourite cannot be created without a resume version.
3. Open **岗位**, search `C++`, try location/salary/education/job-nature/sort
   filters. Logged-in users see `score`, matched/missing skills, `reasons[]`,
   `deal_breakers[]` and deadline urgency.
4. Open **推荐** for the authenticated `GET /jobs/recommended` view. Matching is
   deterministic `rule_v1`; no AI key is required.
5. Open a job detail. Use **打开申请链接** when `apply_url` exists, otherwise
   **打开原始公告** (`source_url`). These links only open a new tab; there is no
   submission or form-filling path.
6. **收藏** the job (after the resume step), then open **申请状态板**. Advance
   `收藏 → 待投递 → 已投递 → 笔试 → 一面 → 二面 → Offer`, or mark **拒绝**.
   The backend enforces the state machine; an invalid transition shows the
   server error.
7. Reload the page: the resume version and applications persist; the token is
   re-validated with `GET /users/me`.

The sample dataset `docs/examples/job-import.json` uses `example.invalid` URLs;
they are intentionally fake. The UI does not derive an apply URL from
`source_url`.

## 截图步骤（可选）

Use synthetic/demo data only; do not put personal data in a screenshot.

1. Register a demo user, upload a synthetic resume, import
   `docs/examples/job-import.json` through the internal token or use an
   existing demo stack.
2. Capture **岗位列表** with a match score/reason, **岗位详情** with the
   apply/original links, and **申请状态板** with at least one application.
3. Suggested filenames: `web-list.png`, `web-detail.png`, `web-status.png`.
   The repository `.gitignore` ignores `*.png`; 0022 deliberately does **not**
   add images to the README. Add owner-approved screenshots in a later change if
   desired.
4. Use the browser/OS screenshot tool; do not include cookies, tokens, email
   addresses, or real resumes.

## 故障排查

| 现象 | 处理 |
|---|---|
| `502` / `backend unavailable` | Backend not running or not healthy: `curl http://127.0.0.1:8080/health`; check `docker compose --profile app logs backend` |
| `nginx:1.27-alpine` pull fails | Use the Python fallback above, or set `JOBPILOT_WEB_IMAGE` to a locally available nginx image; do not claim the nginx smoke passed if it did not run |
| `401` / `20001` / `20002` | Session expired; log in again. The UI clears the token and returns to login |
| 收藏提示没有简历 | Open **申请状态板 → 简历设置**, upload a PDF/TXT/MD, then retry |
| `40004` on favourite | The job is already in the application list (duplicate protection) |
| `40005` on status change | Invalid state-machine transition; refresh and use the offered next step |
| Search returns nothing | Check filters, or import sample jobs through the internal API; public reads only show `clean` rows |
| Port already in use | Override `JOBPILOT_WEB_HOST_PORT` / `JOBPILOT_BACKEND_HOST_PORT` |

## 冒烟与验证

```bash
# Full isolated stack, Python static+proxy fallback (no nginx image needed)
JOBPILOT_BACKEND_IMAGE=jobpilot-backend:local tools/dev/web-smoke.sh

# Same smoke through a local nginx binary/config (when available)
NGINX_BIN=/path/to/nginx tools/dev/web-smoke.sh --server nginx

# Optional real Firefox UI leg (geckodriver on PATH; not a CI dependency)
NGINX_BIN=/path/to/nginx tools/dev/web-smoke.sh --server nginx --browser firefox

# CI-style: backend already running, use the fallback server
tools/dev/web-smoke.sh --mode local \
  --backend-url http://127.0.0.1:8080 --web-port 18081

docker compose --profile app --profile web config -q
python3 tools/checks/release_preflight.py
git diff --check
```

The smoke uses an isolated compose project/ports/volumes and tears them down;
it never points at the developer stack's database.

## 安全边界

- This is a **local demo**: HTTP only, no TLS, no production authentication
  claims. The token is in localStorage and is readable by same-origin script;
  the UI uses a strict CSP, DOM text insertion, and no third-party scripts/CDNs
  to reduce XSS risk.
- `apply_url`/`source_url` are rendered only when they are absolute `http(s)`
  URLs, with `target="_blank"` and `rel="noopener noreferrer"`.
- There is no auto-apply/auto-submit/form-filling path; the UI only opens the
  original page so the human can apply manually.
- Do not use real credentials or personal resumes in a shared demo environment.
