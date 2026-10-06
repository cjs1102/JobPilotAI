# ExecPlan 0005: P2 User Vertical Slice

## Background

P2 engine primitives are committed, but the service does not construct MySQL,
Redis, SessionManager, or any business DAO.

The untracked WIP files in `service/include/jobpilot/service/user/` define DAO
interfaces but have no implementation or tests.

## Goal

Deliver one complete P2 vertical slice:

- `POST /api/v1/users/register`
- `POST /api/v1/users/login`
- `GET /api/v1/users/me`

The slice must demonstrate:

- config-driven MySQL and Redis construction;
- password hashing and verification;
- session-token creation and Redis storage;
- DAO -> Service -> Controller layering;
- consistent JSON responses and error codes;
- unit and integration tests.

## Non-Goals

- Resume, job, application, and AI modules.
- Profile and skill endpoints beyond what one coherent user slice requires.
- RabbitMQ, RPC, crawler, and frontend work.
- P1 network redesign.

## Constraints and Invariants

- `docs/03-database-design.md` and `docs/04-api-design.md` are contracts.
- Controllers contain no SQL.
- Services do not depend on HTTP types.
- DAO uses parameterized SQL only.
- Password hashes and session values are never logged.
- DAO queries enforce user identity.
- P1 remains frozen except for the approved correctness gate.

## Research Evidence

- `service/src/main.cpp` contains no infrastructure construction.
- `ConnectionPool::Query` returns rows but no affected-row count or last insert
  id.
- `RedisClient` and `SessionManager` already expose the required basic
  operations.
- `IUserDao` is declared but not implemented.
- `service/src/router.cpp` manually serializes JSON and has no controller or
  service layer.
- `nlohmann_json` is already a declared dependency but unused.

## Selected Design

1. Add connection execution metadata and transaction ownership to the engine.
2. Implement `UserDao` using the completed engine API.
3. Add `UserService` for registration, login, token creation, and current-user
   lookup.
4. Add `UserController` for request validation and response serialization.
5. Register the three user routes through the existing HTTP router.
6. Generate tokens with a cryptographically secure random source.
7. Keep the untracked DAO declarations as input, then evolve them into the
   implemented design rather than deleting them prematurely.

## Rejected Alternatives

- Querying MySQL directly from route lambdas.
- Reusing `Query` for inserts and guessing the insert id.
- Storing plaintext or unsalted passwords.
- Storing sessions only in memory.
- Implementing all P2 modules before proving one vertical slice.

## Implementation Phases

### Phase 1: Engine Execution Contract

- add execute result with affected rows and last insert id;
- add transaction/connection lease design;
- add engine tests against MySQL.

### Phase 2: User DAO

- implement user, profile, and skill queries required by the slice;
- add DAO integration tests.

### Phase 3: User Service and Token

- implement registration and login rules;
- add bcrypt hashing and verification;
- add secure token generation and Redis session storage.

### Phase 4: Controller and Routes

- parse and validate JSON;
- return locked error codes and response shape;
- add integration tests for register, login, and current user.

### Phase 5: Shutdown and Verification

- define destruction order for HTTP server, MySQL pool, Redis, and logging;
- run full CTest and sanitizer presets.

## Verification and Rollback

Verification:

- schema applied;
- DAO/service/controller tests;
- HTTP integration flow;
- missing/invalid token tests;
- full CTest under normal and ASan/UBSan builds.

Rollback:

- new routes and module code can be disabled independently;
- engine execution API is additive where possible;
- no P1 source change is required for rollback.

## Current Progress

Not started.

## Known Issues and Follow-Up

- Secure token generation may add OpenSSL linkage or another reviewed random
  source.
- The WIP DAO files remain untracked until this plan enters implementation.
- Profile and skill endpoints follow after the three core routes are stable.
