# JobPilot AI 开发规范 V2.0

> 依据 ADR-0001 与架构 V3.0 编写。规范服务于「代码跟随文档、避免中途大改」。所有 C++ / Git / 分层代码遵守本规范。

## 1. C++ 版本与标准

- **C++17**，`-std=c++17`，统一编译选项：`-Wall -Wextra -Werror`（测试代码除外）
- 目标平台：Linux（开发用 WSL2 或 Linux 服务器）
- 构建：CMake + Conan（Conan 卡住时降级 CMake FetchContent）

## 2. 命名规范

| 类别 | 风格 | 示例 |
|---|---|---|
| 文件 | 小写+下划线，`.h`/`.cc` | `event_loop.h` `user_service.cc` |
| 类/结构体 | PascalCase | `class HttpServer` `struct MatchResult` |
| 函数/方法 | camelCase | `void loginUser()` |
| 变量 | camelCase | `int userCount;` |
| 成员变量 | 下划线结尾 | `int fd_;` `std::string name_;` |
| 常量 | 全大写+下划线 | `const int kMaxConn = 10000;` |
| 命名空间 | 小写 | `namespace jobpilot::core` |
| 枚举值 | PascalCase | `enum class TaskStatus { Pending, Processing };` |
| 宏 | 全大写（尽量不用宏） | `JOBPILOT_UNUSED(x)` |

## 3. 命名空间与目录

```
jobpilot
├── core      # 引擎：network / http / database / cache / core
└── service   # 业务：user / resume / job / application / ai
```

- 引擎代码在 `jobpilot::core` 命名空间，业务在 `jobpilot::service`
- 头文件放 `include/jobpilot/<ns>/<name>.h`，源文件放 `src/<ns>/<name>.cc`
- 每个目录自持 `CMakeLists.txt`，顶层聚合

## 4. 分层规范（强制）

```
Controller   解析 HTTP → 调 Service → 组装统一响应（只做这三件事）
    │
Service      业务规则：状态机校验、事务编排、跨模块调用
    │
DAO          数据库访问；依赖 IxxxDao 接口（缝，测试可注入 mock）
    │
backend 引擎（连接池 / Redis / MQ）
```

**规则**
- Controller 不得出现 SQL、不得调多个 Service 拼业务
- Service 不持有 HTTP 细节（不 #include http 头）
- DAO 只写 SQL，不写业务判断
- 模块间只通过 `IxxxService` 接口调用，**禁止 include 其它模块实现**

### 深模块要求（codebase-design）

- 对外接口尽量小：`EventLoop::Run()/Wakeup()/RunInLoop(cb)`，内部承担全部复杂度
- 调用方不得感知 epoll/socket 细节
- 网络/数据库/缓存都封装为可注入的依赖，测试可替换 in-memory 实现

## 5. 网络层规范

- 网络层只做 IO 与连接管理，**禁止写业务**
- 错误写法：socket 收到请求直接查数据库
- 正确：`network → service(router) → business`

## 6. HTTP 与接口规范

- 路由注册：`HttpServer::RegisterRoute(HttpMethod::POST, "/api/v1/users/login", handler)`
- 统一响应由 Controller 组装：`{code, message, data}`（见 04 文档）
- 请求体用 JSON（`nlohmann::json`），禁止手拼字符串
- 错误统一用错误码（`ErrorCode` 枚举），不抛异常表达业务失败

## 7. 错误处理规范

```cpp
enum class ErrorCode : int {
  kSuccess = 0,
  kInvalidParam = 10001,
  kUnauthorized = 20001,
  // ... 与 04 文档错误码表一一对应
};
```

- 业务失败：返回 `ErrorCode`，不 `throw`（异常仅用于不可恢复错误）
- 禁止裸 `assert` 处理用户输入；参数校验在 Controller/Service 边界完成

## 8. 配置规范

- 所有可配置项走 `config.yaml` + 环境变量覆盖，**禁止硬编码**
- 错误写法：`mysql.connect("127.0.0.1")` 写死在代码里
- 正确：读 `config` 模块；密钥只从环境变量取，不入库不入 git

## 9. 数据库规范

- 表/字段小写+下划线（严格对齐 03 文档 DDL，禁止改表）
- 写操作使用事务（`jobpilot::core::Transaction`），读操作走 DAO
- SQL 全部参数化（预处理语句），禁止字符串拼接防注入
- 状态值用代码层枚举映射 TINYINT，禁止魔法数字

## 10. 日志规范

- 统一 `spdlog`，等级：`DEBUG/INFO/WARN/ERROR`
- 业务关键路径打 INFO，异常/失败打 ERROR 并带上下文
- 禁止 `std::cout/printf` 调试输出留仓库

## 11. 测试规范

- 单元测试：GoogleTest，`backend/tests/`，核心模块必须覆盖（Buffer/HTTP解析/EventLoop/Service/DAO）
- DAO/Service 测试用 mock 或内存实现，不依赖真实数据库（可用 Testcontainers 做集成）
- 命名：`test_<module>.cc`
- 按 `docs/quality/README.md` 的 L1-L5 选择最小充分验证；阶段完成执行 L4，
  commit 前按 L5 复用刚通过的验证结果或在相关文件变化后补跑失效门禁。

## 12. Git 规范

### 分支模型

```
main        稳定可发布
develop     开发主干
feature/*   功能分支（从 develop 拉）
bugfix/*    缺陷修复
```

### Commit 规范（Conventional Commits）

```
type(scope): subject

feat(user): implement login api
fix(network): fix epoll edge-trigger missing read
docs(api): update match response schema
test(core): add buffer unit tests
refactor(core): extract connection pool
```

| type | 含义 |
|---|---|
| feat | 新功能 |
| fix | 缺陷修复 |
| docs | 文档 |
| test | 测试 |
| refactor | 重构（不改行为） |
| style | 格式（无逻辑变化） |

### 提交纪律

- 一次提交一个逻辑变更，禁止把无关修改混在一起
- 提交前必须本地编译 + 单测通过
- 关键功能带测试与文档更新（文档先行原则）

## 13. Code Review 清单

提交合并前自查：

- [ ] 是否符合本文档规范与 02/03/04 设计契约
- [ ] 是否影响既有接口/表结构（若是，必须先走文档变更）
- [ ] 是否新增/修改了测试
- [ ] 是否同步更新了文档
- [ ] 是否存在硬编码/魔法数/SQL 拼接
- [ ] 是否引入新的模块间耦合（应通过接口）

## 14. 防"中途大改"的纪律

1. **契约冻结**：`03` 表结构、`04` API 契约改动必须走文档评审，禁止边写边改
2. **接口先行**：写业务前先定 `IxxxService/IxxxDao` 接口，实现随后
3. **小步提交**：每阶段按 05 计划验收，超止损线先冻结
4. **文档同步**：任何设计变化，先改 docs 再改代码
