# JobPilotAI

[English](README.md) | 简体中文

JobPilotAI 是一个自托管的求职工作台：基于 C++17 后端采集和检索公开招聘信息，
以确定性规则解释岗位匹配，并跟踪求职进度。可选的 AI Worker 提供任务型辅助；
核心流程不需要配置 AI 服务商或 API Key。

| 带可解释匹配的岗位详情 | 申请状态板 |
|---|---|
| ![岗位详情，展示基于规则的匹配分数与理由](docs/images/web-detail.png) | ![申请状态板，包含简历上传与投递跟踪](docs/images/web-status.png) |

截图展示的是使用合成演示数据的本地 Web UI，当前界面文案为中文。

## 快速开始

前置条件：Docker Engine 与 Compose v2、`curl`、Python 3。首次运行会构建后端
镜像，Docker 需要能够拉取基础镜像并完成构建。

```bash
git clone https://github.com/cjs1102/JobPilotAI.git
cd JobPilotAI
./tools/dev/demo-up.sh
```

启动脚本会：

- 检查 Docker、Compose、`curl` 和 Python；
- 仅在 `.env` 不存在时生成包含随机 MySQL 密码和内部令牌的 `.env`，权限设为
  `0600`，且不打印任何密钥值；已存在的 `.env` 不会被修改；
- 启动 MySQL 8 和 Redis 7，构建并启动后端，在 `8081` 端口提供 Web UI：可用
  `nginx:1.27-alpine` 时使用 nginx，否则使用仓库内置的 Python 静态文件/反向代理
  服务；
- 默认导入 12 条合成岗位，并打印类似
  `DEMO_READY url=http://127.0.0.1:8081` 的地址。

打开打印出的地址即可。后端默认位于 `http://127.0.0.1:8080`。加上 `--no-seed`
可以不导入示例岗位。可通过 `DEMO_API_PORT`、`DEMO_WEB_PORT`、`DEMO_MYSQL_PORT`、
`DEMO_REDIS_PORT` 和 `DEMO_PROJECT` 覆盖端口与 Compose 项目名。

停止演示环境：

```bash
docker compose --env-file .env -p jobpilot-demo --profile app --profile web down
```

只有在需要同时删除演示数据库和上传文件时，才加 `-v`。

### 从源码构建（可选）

一条命令演示使用的是容器化后端。如果需要直接构建并运行 C++ 服务，请先安装
C++17 工具链、CMake、`pkg-config` 和 MySQL 客户端开发文件，然后执行：

```bash
tools/dev/bootstrap.sh          # 解析固定的 Conan 依赖图
cmake --preset release
cmake --build --preset release -j"$(nproc)"
```

Preset、测试等级和 Sanitizer 运行方式见
[`docs/quality/README.md`](docs/quality/README.md)。二进制位于
`build/presets/release/service/jobpilot_service`。

### 手动启动与 nginx 回退

如果希望自行管理各服务：

```bash
cp .env.example .env          # 填写 MYSQL_ROOT_PASSWORD 和 MYSQL_PASSWORD
docker compose up -d          # MySQL + Redis
docker compose --profile app build backend
docker compose --profile app --profile web up -d
```

如果无法拉取 `nginx:1.27-alpine`，可以用 Python 标准库运行器提供同一套 `web/`
文件：

```bash
python3 tools/dev/web_static_proxy.py \
  --root web --port 8081 --backend http://127.0.0.1:8080
```

`tools/dev/demo-up.sh` 会自动执行这一回退。故障排查见
[本地演示指南](docs/guides/local-demo.md)。

## 体验演示

1. 注册一个本地账号。
2. 打开 **申请状态板 → 简历设置**，上传 PDF、TXT 或 Markdown 简历。收藏岗位前
   必须先有一个简历版本。
3. 打开 **岗位**，搜索 `C++`，并尝试地点、薪资、学历、岗位性质和排序筛选。
4. 打开一个岗位，查看匹配分数、命中与缺失技能、`reasons` 和
   `deal_breakers`。匹配是确定性的，不需要 AI Key。
5. 收藏该岗位。在 **申请状态板** 中按后端状态机推进：收藏、待投递、已投递、
   笔试、面试、Offer，或标记为拒绝。
6. 有申请链接时点击 **打开申请链接**，否则点击 **打开原始公告**。链接只会在新
   标签页打开原始页面，JobPilotAI 不会代填或提交申请。

演示用户、简历和岗位均为本地合成数据。不要在共享环境中使用真实凭据或个人简历。

## 使用方式

### 采集或导入岗位

JobPilotAI 通过 Python 来源适配器读取公开、免登录的来源：Greenhouse、Lever、
Ashby，以及面向国内校招的 nowcoder。适配器通过受内部令牌保护的接口发布观测，
不直接写数据库。对于登录墙后的页面，可以使用人工采集书签小工具；它只读取用户
已经打开的页面，不发送凭据，也不发起网络请求。此外还提供 JSON 批量导入接口。

来源适配器、来源登记表和每个来源的限制见
[`source_adapters/README.md`](source_adapters/README.md)。`complete=false` 的
观测应视为有界样本，而不是全量抓取。

### 搜索、筛选与排序

公开岗位列表支持分页、关键词搜索、薪资与技能筛选、地点与学历筛选、截止时间
筛选和排序。登录用户可以查看推荐视图。只有通过入库质量门禁的岗位才会公开可见；
需要复核的记录保留在内部复核界面。

### 阅读匹配解释

`rule_v1` 是确定性的排序与解释模型，返回分数、命中与缺失技能、可加性的
`reasons`、`deal_breakers` 和截止时间紧迫度。该路径没有隐藏的大模型步骤。
AI 任务管线是可选的，且不能覆盖确定性分数。

### 生成简历草稿

简历定制功能会生成确定性的纯文本 ATS 草稿，每一条事实都带来源标注；无法推导
的事实保留为显式占位符。草稿必须通过机械事实门禁才能确认。这是草稿工作流，
不是自动投递生成器。

### 跟踪投递并手动申请

收藏和投递状态变更会记录在本地数据库，后端会校验状态机。需要申请时，打开原始
公告或显式提供的申请链接，在浏览器中手动完成申请。系统没有自动提交、代填表单
或通知路径。

所有演示数据都保存在本地 Docker 卷中。只有运行来源适配器或配置可选 AI 服务商时
才会发生网络访问。

## 已包含的能力

- **来源采集**：公开 ATS 适配器、nowcoder 校招来源、人工采集、JSON 导入、来源
  观测、时效性与入库质量门禁。
- **搜索与排序**：关键词与结构化筛选、稳定分页、推荐视图，以及确定性
  `rule_v1` 评分。
- **可解释匹配**：列表与详情中的分数、命中与缺失技能、理由、否决项和截止时间
  紧迫度。
- **简历定制**：带来源标注和事实校验的确定性 ATS 草稿。
- **投递跟踪**：收藏、经后端校验的状态机、统计信息和本地状态板。
- **本地 Web UI**：原生 HTML/CSS/JavaScript，无构建步骤；同源代理 `/api` 和
  `/health`，nginx 不可用时自动回退到标准库服务。
- **可选 AI Worker**：Python 任务型辅助；没有服务商密钥时核心产品仍可正常使用。
- **工程与运维工具**：Docker Compose、已提交的容器镜像定义、CI、Sanitizer
  Preset、基准测试、发布预检，以及禁止自动投递路径的检查。

## 验证情况

2026-10-21 记录的发布准备基线：

- **312 个 C++ 测试**（通过 CTest）。Debug、ASan、UBSan 各完成 `312/312`，
  无失败且没有 Sanitizer 诊断。一个复核回填测试在开发数据库上跳过，在隔离的
  临时数据库上通过。
- **TSan**：测试框架 52/52，外部运行时 17/17，均无诊断。TSan 不覆盖完整测试集。
- **Python**：来源适配器 481 个测试、AI Worker 190 个测试，在固定依赖环境中
  全部通过。
- **CI**：发布准备合并后，`main` 上的 `ci` 和 `image` 工作流均为绿色。镜像
  工作流会构建已提交的 Dockerfile，并运行端到端演示门禁。
- **基准测试**：在 4 vCPU 主机上，`/health` 为 13.4–16.1k 请求/秒，数据库支撑的
  岗位接口为 180–194 请求/秒。这些是特定主机上的测量值，不是服务等级目标，也
  不是通过/失败阈值。环境说明与原始结果见
  [`docs/benchmarks/README.md`](docs/benchmarks/README.md)。

## 限制与非目标

- **国内来源覆盖范围有限。** nowcoder 是目前唯一注册的国内校招来源，只读取
  免登录的有界列表：单次查询最多 20 条，不翻页，且始终 `complete=false`。
  其他国内来源尚未注册。
- **部分岗位没有申请链接。** Greenhouse 与 nowcoder 聚合岗不提供来源侧的
  `apply_url`，界面会回退到原始公告。系统不会推导或猜测申请链接。Moka 尚未
  验证，也未注册。
- **不绕过登录墙。** 系统不做自动登录、验证码绕过、Cookie 或 User-Agent 伪造，
  也不使用代理池。登录墙后的岗位只能通过人工采集处理。
- **不自动投递。** 没有自动申请、自动提交、表单代填或通知路径，也没有通用爬虫、
  消息队列、API 网关或 SaaS RBAC。
- **截止时间仅用于展示。** 截止时间过期不会自动关闭岗位。
- **界面是本地演示。** 认证基于 HTTP 和浏览器 `localStorage`，不是生产级认证，
  也没有 TLS。
- **不构成法律结论。** 来源条款、robots 指令和数据使用规则仍由运营者负责。

## 文档

- [文档地图](docs/README.md)与
  [项目文档索引](docs/PROJECT_DOCUMENTATION_INDEX.md)
- [本地 Web UI 指南](docs/guides/local-demo.md)
- [架构说明](docs/architecture/open-source-mvp.md)与
  [架构决策](docs/adr/README.md)
- [API 契约](docs/04-api-design.md)与
  [数据库契约](docs/03-database-design.md)
- [来源适配器](source_adapters/README.md)与
  [基准测试](docs/benchmarks/README.md)

## 贡献与安全

提交 Pull Request 前请阅读 [`CONTRIBUTING.md`](CONTRIBUTING.md)。安全漏洞请按
[`SECURITY.md`](SECURITY.md) 中的方式私下报告，不要创建公开 Issue。行为准则
暂缓，待有可用的报告渠道后再补充。

## 许可证

MIT。见 [`LICENSE`](LICENSE) 与 [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md)。
