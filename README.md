# JobPilotAI

JobPilotAI 是一个基于 C++ 高性能后端架构的 AI 求职自动化平台。

## 技术栈

### Backend

- C++17
- CMake
- Conan

### Infrastructure

- MySQL 8.0
- Redis 7
- Docker Compose

### Libraries

- spdlog
- yaml-cpp
- nlohmann/json
- hiredis
- GoogleTest


## 项目结构

```text
JobPilotAI
├── backend
├── service
│   ├── config
│   ├── include
│   ├── src
│   └── tests
├── docker
│   └── mysql
├── docker-compose.yml
├── CMakeLists.txt
└── .env.example