# ExecPlan 0005: P2 User Vertical Slice

> Status: Completed on 2026-10-06.
>
> Archived after normal and sanitizer verification.

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
- `docs/04-api-design.md` is the API authority and defines
  `/api/v1/users/register`, `/api/v1/users/login`, `/api/v1/users/me`,
  `Authorization: Bearer <token>`, `{code,message,data}`, and TTL 86400.
- The WIP DAO interface includes profile and skill methods outside the scope of
  this first vertical slice.
- Redis deletes a session key when its TTL expires, so a missing key cannot
  distinguish a never-issued token from an expired token without an additional
  marker key. Both will map to `20001`; `20002` remains reserved.

## Selected Design

1. Add an additive `ExecutionResult` and `ConnectionPool::Execute` for
   `affected_rows` and `last_insert_id`. No transaction abstraction is added
   because registration is a single INSERT.
2. Reduce the WIP DAO interface to the three methods required by this slice:
   create user, find by email, and find by id.
3. Implement `UserDao` using prepared statements and `ExecutionResult`.
4. Add `UserService` for validation, bcrypt hashing/verification, token
   creation, Redis sessions, and current-user lookup.
5. Add `UserController` for JSON parsing, Authorization parsing, HTTP status
   selection, and `{code,message,data}` serialization.
6. Register the three routes through the existing `HttpRouter`.
7. Generate 32 random bytes with `getrandom(2)` and encode them as a 64-byte
   lowercase hex token.
8. Construct MySQL, Redis, SessionManager, DAO, Service, and Controller in
   `main` after configuration and before the HTTP server; destruction order is
   the reverse.
9. Map business errors to explicit HTTP statuses: 400 validation/malformed,
   401 authentication, 403 disabled account, 409 duplicate email, 500 engine
   failure.

## Rejected Alternatives

- Querying MySQL directly from route lambdas.
- Reusing `Query` for inserts and guessing the insert id.
- Storing plaintext or unsalted passwords.
- Storing sessions only in memory.
- Implementing all P2 modules before proving one vertical slice.
- Adding transaction infrastructure for one INSERT that has no cross-write
  consistency requirement.
- Implementing profile and skill DAO methods before their endpoints exist.

## Implementation Phases

### Phase 1: Engine Execution Contract

- add execute result with affected rows and last insert id;
- keep the existing connection lease and return path;
- add engine tests against MySQL.

### Phase 2: User DAO

- trim the WIP interface to create-user/find-by-email/find-by-id;
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

- [x] Research
- [x] Phase 1: Engine Execution Contract
- [x] Phase 2: User DAO
- [x] Phase 3: User Service and Token
- [x] Phase 4: Controller and Routes
- [x] Phase 5: Shutdown and Verification

## Verification Results

- Engine `Execute` metadata integration test passed.
- User DAO tests: 3/3 passed against clean test data.
- User Service/Session tests: 6/6 passed.
- User HTTP integration tests: 2/2 passed.
- Full normal CTest: 54/54 passed in `service/` and root builds.
- ASan with leak detection: 54/54 passed.
- UBSan with halt-on-error: 54/54 passed.
- TSan concurrency/User subset: 17/17 passed.
- TSan external-service subset: 6/6 passed.
- Real service curl flow:
  `register -> login -> Authorization -> me` returned user id 99 and the
  current-user payload; test user and Redis session were cleaned afterward.

## Recorded Design Decisions

- No transaction abstraction was added. Registration is one INSERT.
- The DAO WIP was reduced to create-user, find-by-email, and find-by-id.
- `20002 token expired` is reserved; Redis TTL deletion cannot distinguish an
  expired token from a never-issued token, so both currently return `20001`.
- `getrandom(2)` produces 32 random bytes encoded as a 64-character lowercase
  hex token.
- HTTP status mapping is 400 validation, 401 authentication, 403 disabled
  account, 409 duplicate email, and 500 engine/internal failure.

## Known Issues and Follow-Up

- Secure token generation may add OpenSSL linkage or another reviewed random
  source.
- The WIP DAO files remain untracked until this plan enters implementation.
- Profile and skill endpoints follow after the three core routes are stable.
