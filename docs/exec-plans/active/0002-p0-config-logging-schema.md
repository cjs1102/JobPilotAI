# ExecPlan 0002: Complete P0 Configuration, Logging, and Schema

## Background

The executable currently hardcodes `0.0.0.0:8080`, does not load
`service/config/config.yaml`, and does not initialize logging.

`yaml-cpp` and `spdlog` are declared in `service/conanfile.txt` but are not
used. `docs/03-database-design.md` contains the locked schema, while
`docker/mysql/init/` is empty and the live database has no tables.

## Goal

Create a reproducible P0 runtime and persistence foundation:

- typed application configuration;
- environment-variable overrides;
- fail-fast handling for required secrets;
- initialized, thread-safe logging;
- canonical MySQL schema and initialization scripts;
- clean and sanitizer-compatible CMake integration.

## Non-Goals

- No P1 network or HTTP behavior changes.
- No MySQL or Redis object construction in the business flow.
- No user DAO implementation.
- No business endpoints.

## Constraints and Invariants

- `docs/03-database-design.md` remains the schema contract.
- Passwords never appear in YAML or logs.
- Required `MYSQL_PASSWORD` fails fast with a clear error.
- Environment variables override YAML values.
- Configuration errors return or map to `CONFIG_ERROR`.
- Existing P1 health behavior remains unchanged.

## Research Evidence

- `service/src/main.cpp` hardcodes address and port.
- `service/config/config.yaml` already defines server, MySQL, Redis, and log
  sections.
- `service/conanfile.txt` already includes `spdlog` and `yaml-cpp`.
- `backend/include/jobpilot/log/` and `backend/src/log/` are empty.
- `backend/include/jobpilot/database/mysql_pool.h` requires host, port, user,
  password, database, and max connections.
- `database/` does not exist and `docker/mysql/init/` is empty.

## Selected Design

1. Add a typed `jobpilot::core::Config` under `backend`.
2. Load YAML first, then overlay environment variables.
3. Keep secret values in environment variables only.
4. Add a small spdlog initialization wrapper and structured startup/shutdown
   logging.
5. Make `database/schema/` the canonical SQL source.
6. Mount the canonical schema directory into the MySQL container rather than
   duplicating SQL under `docker/mysql/init`.
7. Keep schema scripts idempotent where it does not change the locked schema.

## Rejected Alternatives

- Parsing YAML directly in `main`: no testable configuration boundary.
- Putting configuration only in `service`: engine tests and future binaries
  would depend on service-specific code.
- Duplicating SQL in both `database/schema/` and `docker/mysql/init/`: creates
  drift risk.
- Logging only with `std::cout`: violates the project rule and lacks levels and
  structured context.

## Implementation Phases

### Phase 1: Configuration

- define config types and defaults;
- implement YAML parsing and environment overlay;
- implement validation and secret fail-fast;
- add unit tests for defaults, overrides, malformed YAML, and missing password.

### Phase 2: Logging

- add logger wrapper and initialization;
- configure level and sink from `Config`;
- route startup/config/database/Redis errors through the logger;
- remove production `std::cout` usage.

### Phase 3: Schema and Initialization

- create canonical schema files under `database/schema/`;
- implement idempotent table creation and optional seed data;
- update Compose to mount the canonical schema directory;
- verify schema application against a temporary database or existing empty
  schema without deleting the current volume.

### Phase 4: Service Wiring

- load config and initialize logging before server construction;
- preserve P1 server behavior and routes;
- validate missing configuration and bad port handling.

## Verification and Rollback

Verification:

- clean `service/` and root build;
- all current CTest tests;
- config unit tests;
- `docker compose --env-file .env.example config`;
- schema apply and table inspection on a non-production database;
- startup with missing `MYSQL_PASSWORD` fails clearly.

Rollback:

- config/log wiring can be reverted independently;
- schema scripts are not destructive and do not replace the running volume.

## Current Progress

Not started.

## Known Issues and Follow-Up

- `project-state.md` currently points to `f28a98c`; actual HEAD is `2be5212`.
- Existing MySQL volumes should not be deleted during verification.
- Schema path changes must be reflected in Compose and project-structure docs.
