# JobPilot AI 部署文档 V1.0

> ⚠️ **历史资料 / Historical — Superseded**
>
> 本文档属于 ADR-0007 ~ ADR-0010 之前的旧设计方向，仅作历史参考，
> **不得作为新的实现规范**。现行架构见
> [architecture/open-source-mvp.md](architecture/open-source-mvp.md)，
> 现行决策见 [adr/README.md](adr/README.md)（ADR-0007 ~ ADR-0010）。
>
> Do not use this document for new implementation decisions.

> **Correction (2026-10-10).** The commands below are not runnable as written.
> The topology table names gateway / `ai_service` / RabbitMQ, three
> `docker/…/Dockerfile` files and an `ai_service/requirements.txt` that do not
> exist in this tree, and it starts a `jobpilot_server` binary under `build/bin`
> that was never built — the real target is `jobpilot_service`, produced by the
> CMake presets. For the commands that do work today see the Quick Start in
> [`../README.md`](../README.md), plus
> [`../database/schema/README.md`](../database/schema/README.md) for the database
> and [`quality/README.md`](quality/README.md) for the verification levels.

> 依据架构 V3.0 §11 编写。覆盖：本地开发（WSL）、docker-compose 一键部署、CI、运维要点。

## 1. 开发环境

### 1.1 平台说明（重要）

C++ 后端基于 epoll，**只能在 Linux 运行**。Windows 开发推荐：

| 方式 | 说明 | 推荐度 |
|---|---|---|
| **WSL2 + Ubuntu** | 原生 Linux 内核，兼容 epoll，与服务器一致 | ⭐⭐⭐⭐⭐ |
| 云服务器 + VS Code Remote | 远程开发 | ⭐⭐⭐⭐ |
| 原生 Windows | 需用 mingw+Windows 网络模型改写网络层 | ❌ 不推荐 |

> 本地编辑器可用 Windows 侧（VS Code/CLion + WSL），编译/运行在 WSL 内。

### 1.2 WSL 依赖

```bash
sudo apt update && sudo apt install -y \
  build-essential cmake ninja-build python3 python3-pip git \
  libssl-dev libyaml-dev

# Conan
pip install conan
conan profile detect
```

### 1.3 本地启动（后端）

> V1.1 修正：docker-compose 位于**仓库根目录**（`docker-compose.yml`，context: . 即根目录）；构建**唯一入口**为 `service/`（backend 经 add_subdirectory 一并编译，不再单独 conan install）。

```bash
# 1. 基础设施（在仓库根目录执行；P0 阶段只需 mysql/redis）
cp .env.example .env        # 编辑填写 MYSQL_ROOT_PASSWORD
docker compose up -d mysql redis

# 2. 构建服务（service 目录；engine 经 add_subdirectory 一并编译）
cd service
conan install . -of build --build=missing
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure

# 3. 启动（MYSQL_PASSWORD 必须设置，未设置进程 fail-fast 退出）
export MYSQL_PASSWORD=<与 .env 的 MYSQL_ROOT_PASSWORD 相同>
./build/bin/jobpilot_server --config=config/config.yaml
```

### 1.4 本地启动（AI 服务）

```bash
cd ai_service
python -m venv .venv && source .venv/bin/activate
pip install -r requirements.txt
# 下载 embedding 模型（首次，约100MB）
python -m app.llm.embedder --download
uvicorn app.main:app --port 8000
```

## 2. Docker Compose 部署

### 2.1 拓扑

| 服务 | 镜像/来源 | 端口 | 说明 |
|---|---|---|---|
| gateway | nginx:1.25-alpine | 80/443 | 网关 + 前端静态 |
| backend | 本地构建 | 8080 | C++ 单体 |
| ai_service | 本地构建 | 8000 | Python AI |
| crawler | 本地构建 | - | 定时采集（可选） |
| mysql | mysql:8.0 | 3306 | 业务库 |
| redis | redis:7-alpine | 6379 | 缓存 |
| rabbitmq | rabbitmq:3-management | 5672/15672 | 消息队列 |

### 2.2 docker-compose.yml（仓库根目录）（V1.1 修正：移到根目录、移除 crawler、环境变量与 08 §3 表一致）

```yaml
services:
  mysql:
    image: mysql:8.0
    environment:
      MYSQL_ROOT_PASSWORD: ${MYSQL_ROOT_PASSWORD:?set MYSQL_ROOT_PASSWORD in .env}
      MYSQL_DATABASE: jobpilot
    ports: ["3306:3306"]
    volumes:
      - mysql_data:/var/lib/mysql
      - ./database/schema:/docker-entrypoint-initdb.d:ro
    healthcheck:
      test: ["CMD", "mysqladmin", "ping", "-h", "localhost"]
      interval: 10s
      timeout: 5s
      retries: 5

  redis:
    image: redis:7-alpine
    ports: ["6379:6379"]
    volumes: [redis_data:/data]
    healthcheck:
      test: ["CMD", "redis-cli", "ping"]
      interval: 10s
      timeout: 5s
      retries: 5

  rabbitmq:
    image: rabbitmq:3-management
    ports: ["5672:5672", "15672:15672"]
    volumes: [rabbitmq_data:/var/lib/rabbitmq]
    healthcheck:
      test: ["CMD", "rabbitmq-diagnostics", "-q", "ping"]
      interval: 10s
      timeout: 5s
      retries: 5

  backend:
    build:
      context: .                 # 仓库根目录（compose 在根，context: . 即根）
      dockerfile: docker/backend/Dockerfile
    ports: ["8080:8080"]
    environment:
      MYSQL_HOST: mysql
      MYSQL_USER: root
      MYSQL_PASSWORD: ${MYSQL_PASSWORD:?set MYSQL_PASSWORD in .env}
      MYSQL_DATABASE: jobpilot
      REDIS_HOST: redis
      RABBITMQ_HOST: rabbitmq
    depends_on:
      mysql: { condition: service_healthy }
      redis: { condition: service_healthy }
      rabbitmq: { condition: service_healthy }
    volumes: [storage:/app/storage]

  ai_service:
    build:
      context: .
      dockerfile: docker/ai/Dockerfile
    ports: ["8000:8000"]
    environment:
      MYSQL_HOST: mysql
      MYSQL_USER: root
      MYSQL_PASSWORD: ${MYSQL_PASSWORD}
      MYSQL_DATABASE: jobpilot
      REDIS_HOST: redis
      RABBITMQ_HOST: rabbitmq
      LLM_API_KEY: ${LLM_API_KEY:?set LLM_API_KEY in .env}
      LLM_BASE_URL: ${LLM_BASE_URL:-https://api.deepseek.com}
      LLM_MODEL: ${LLM_MODEL:-deepseek-chat}
    depends_on:
      mysql: { condition: service_healthy }
      redis: { condition: service_healthy }
      rabbitmq: { condition: service_healthy }

  gateway:
    build:
      context: .
      dockerfile: docker/gateway/Dockerfile
    ports: ["80:80"]
    depends_on: [backend]

volumes:
  mysql_data: {}
  redis_data: {}
  rabbitmq_data: {}
  storage: {}
```

### 2.3 启动

```bash
# 在仓库根目录执行（V1.1 修正）
docker compose up -d --build
docker compose ps   # 全部 healthy
```

### 2.4 .env.example（仓库根 .env.example，V1.1 修正：位置移到根 + 补 MYSQL_PASSWORD/LLM 配置）

```bash
# docker compose 在仓库根自动读取 .env
MYSQL_ROOT_PASSWORD=change_me   # mysql 容器 root 密码
MYSQL_PASSWORD=change_me        # backend/ai_service 连接 MySQL 用（与上面相同即可）
LLM_API_KEY=sk-xxxx             # DeepSeek API Key
# 可选：
# LLM_BASE_URL=https://api.deepseek.com
# LLM_MODEL=deepseek-chat
```

## 3. Dockerfile

### 3.1 backend（多阶段，docker/backend/Dockerfile）

```dockerfile
# 构建上下文须为仓库根目录（compose 在根，context: .；COPY backend/ service/ third_party/ 均相对根目录）
FROM conanio/gcc13-ubuntu22.04 AS builder
WORKDIR /src
COPY backend/ ./backend
COPY service/ ./service
COPY third_party/ ./third_party
WORKDIR /src/service
RUN apt-get update && apt-get install -y libmysqlclient-dev libssl-dev pkg-config \
 && conan install . -of build --build=missing \
 && cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
 && cmake --build build -j$(nproc)

FROM ubuntu:22.04
RUN apt update && apt install -y libssl3 libyaml-0-2 libmysqlclient21 && rm -rf /var/lib/apt/lists/*
WORKDIR /app
COPY --from=builder /src/service/build/bin/jobpilot_server ./jobpilot_server
COPY service/config/config.yaml ./config/config.yaml
EXPOSE 8080
ENTRYPOINT ["./jobpilot_server", "--config=config/config.yaml"]
```

> 注意：运行镜像必须安装 `libmysqlclient21`（libmysqlclient-dev 的运行时库），否则启动报找不到 `libmysqlclient.so`。

### 3.2 ai_service（Python 侧）（V1.1 修正：crawler 已从 MVP 范围移除）

```dockerfile
FROM python:3.10-slim
WORKDIR /app
COPY ai_service/requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt
COPY ai_service/ .
CMD ["uvicorn", "app.main:app", "--host", "0.0.0.0", "--port", "8000"]
```

> AI 服务的 MQ 消费/任务轮询由 `app.main` 的 FastAPI lifespan 启动后台线程，单容器单进程即可，无需单独 consumer 容器。

## 4. Nginx 网关（gateway/nginx.conf）（V1.1 修正：limit_req_zone 移到 http 顶层 + map 豁免 GET/HEAD）

> 两条 nginx 语法/设计要点：
> 1. `limit_req_zone` **只能放在 http 上下文**（`server{}` 之外），写在 server 内会导致 nginx 启动失败。
> 2. `map` 让 GET/HEAD 的限流 key 为空字符串（空 key 不限流）——否则前端 AI 页 2s 轮询（30 req/min）恰好触发 `rate=30r/m` 限流，演示就会 503。

```nginx
# ===== http 上下文（顶层，绝不能放进 server{}）=====
limit_req_zone $binary_remote_addr zone=api:10m rate=30r/m;

# GET/HEAD（含 AI 轮询）不限流；POST/PUT/DELETE 按 IP 限 30r/m
map $request_method $rate_limit_key {
    default $binary_remote_addr;
    GET "";
    HEAD "";
}
limit_req_zone $rate_limit_key zone=api_write:10m rate=30r/m;

server {
    listen 80;

    # 前端静态（SPA，产物由 gateway 多阶段构建 COPY 进镜像）
    location / {
        root /usr/share/nginx/html;
        try_files $uri $uri/ /index.html;
    }

    # 后端 API（写操作限流 + 反代合并到一个 location）
    location /api/ {
        limit_req zone=api_write burst=50 nodelay;
        proxy_pass http://backend:8080;
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
    }

    # 健康检查
    location /health {
        proxy_pass http://backend:8080/health;
    }
}
```

## 5. CI（GitHub Actions）（V1.1 修正：单一构建入口 service/，不再分开构建 backend）

`.github/workflows/ci.yml`：push / PR 到 main 触发

```yaml
name: CI
on:
  push:
    branches: [main]
  pull_request:

jobs:
  build-and-test:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - name: Install deps
        run: |
          pip install conan
          sudo apt-get update && sudo apt-get install -y libssl-dev libmysqlclient-dev pkg-config
      - name: Configure (Conan)
        working-directory: service
        run: |
          conan profile detect
          conan install . -of build --build=missing
      - name: Build & Test (C++)
        working-directory: service
        run: |
          cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
          cmake --build build -j2
          ctest --test-dir build --output-on-failure
      - name: Test (AI Service)
        working-directory: ai_service
        run: |
          pip install -r requirements.txt
          python -m pytest -q
```

## 6. 运维要点

| 项 | 说明 |
|---|---|
| 健康检查 | 各服务 compose `healthcheck` + `/health` |
| 数据卷 | mysql/redis/rabbitmq/storage 均持久化，重建容器不丢数据 |
| 日志 | spdlog 落卷（`storage/logs/`）；AI 侧结构化日志 |
| 备份 | `mysqldump -u root -p"$MYSQL_ROOT_PASSWORD" jobpilot > jobpilot_$(date +%F).sql` + 简历卷快照 |
| 密钥 | 只走根目录 `.env` 环境变量，不提交 git；config.yaml 不出现密码 |
| 升级 | `git pull && docker compose up -d --build`，先备份再升级 |

## 7. 生产部署（演示）

```
云服务器(单机) → git clone → cp .env.example .env → docker compose up -d --build → Nginx 80 对外
（compose 在仓库根目录执行）
```

- gateway 镜像（多阶段：node 构建前端 → nginx 托管）见 [08-execution-guide.md](08-execution-guide.md) §10.3 ②
- 可选 HTTPS：`nginx` 挂证书，`listen 443 ssl`
- 资源建议：2C4G 起（embedding 模型内存占用）
- 对外只暴露 80/443，DB/Redis/MQ 仅内网
