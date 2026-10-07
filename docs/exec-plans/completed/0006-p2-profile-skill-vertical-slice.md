# ExecPlan 0006: P2 Profile and Skill Vertical Slice

## Background

The completed 0005 user slice provides registration, login, Redis sessions,
`GET /api/v1/users/me`, MySQL access through `ConnectionPool`, and the
Controller -> Service -> DAO pattern.

`profiles` and `skills` are already locked in `docs/03-database-design.md`.
The User API contract defines profile and skill endpoints in
`docs/04-api-design.md`, but their response details and duplicate-skill
behavior are only partially specified.

## Goal

Complete the user-domain P2 slice with:

- authenticated profile read/update;
- authenticated skill list/create/update/delete;
- user identity resolved exclusively from the Redis session token;
- parameterized MySQL access through the existing `ConnectionPool`;
- focused DAO, Service, and HTTP tests;
- no new Redis keys, file storage, or P1 changes beyond the approved
  `PUT`/`DELETE` method capability.

## Non-Goals

- Resume, job, application, AI task, or notification modules.
- Profile/skill caching in Redis.
- File upload or local object storage.
- A transaction framework for single-statement writes.
- New API error codes or schema changes.
- P1 network/HTTP implementation changes.

## Constraints and Invariants

- `docs/03-database-design.md` and `docs/04-api-design.md` remain contracts.
- Requests never submit `user_id`; the Controller derives it from the Bearer
  token and Redis session.
- Every profile/skill query includes `user_id` or is keyed by the session user.
- SQL is parameterized; no string interpolation of user input.
- Profile update is one UPSERT; skill create/update and delete are one
  statement each. No transaction abstraction is required for this slice.
- Passwords and session tokens are never logged.
- P1 network/HTTP behavior remains frozen except for the approved method
  capability extension.

## Approved P1 Freeze Exception

Because the current P1 HTTP method capability is insufficient, one minimal
freeze exception is approved to satisfy the locked API contract:

- add only `kPut` and `kDelete` to `HttpMethod`;
- parse only the `PUT` and `DELETE` request-line methods;
- allow those methods to participate in existing route matching;
- add only the corresponding parser/router tests.

No other P1 network/HTTP refactor is authorized. EventLoop, Poller,
EpollPoller, Channel, Buffer, TcpConnection, TcpServer, existing HTTP body
framing, and existing GET/POST behavior must remain unchanged.

## Research Evidence

- `service/src/user/user_dao.cpp` currently implements only user create and
  lookup by email/id.
- `service/src/user/user_service.cpp` owns bcrypt, token/session handling, and
  current-user lookup.
- `service/src/user/user_controller.cpp` already centralizes JSON parsing,
  Bearer parsing, and `{code,message,data}` responses.
- `service/src/router.cpp` registers the existing user routes and can add the
  profile/skill routes without changing P1.
- `ConnectionPool::Query` and additive `Execute` are sufficient for profile
  and skill reads/writes; no multi-write transaction is needed.
- `docs/03-database-design.md` defines:
  - `profiles`: unique `user_id`, nullable profile fields, salary min/max.
  - `skills`: unique `(user_id, name)`, `level` defaults to 1.
- `docs/04-api-design.md` defines:
  - `GET|PUT /api/v1/users/profile`
  - `GET|POST /api/v1/users/skills`
  - `DELETE /api/v1/users/skills/{id}`
- Resume requires file storage and a resume + first-version two-write flow.
- Job read APIs require query/path parsing and test data; job write/admin
  authorization is not defined.
- Application depends on Job and optionally ResumeVersion, so it is blocked on
  those modules.

## P2 Dependency Graph

```text
User auth/session (0005)
  -> Profile + Skill (0006)
  -> Resume
       -> optional file storage
       -> transaction for resume + first version

Job (independent read path)
  -> Application
Resume
  -> Application

Profile + Skill
  -> future AI match/ranking inputs
```

## Selected Design

1. Extend the existing user module rather than create a second bounded context.
2. Add profile/skill records and DAO operations to `IUserDao`/`UserDao`.
3. Add an authentication helper to `UserService` that validates a token and
   returns the session user id without exposing token/session internals to the
   controller.
4. Extend `UserService` with profile/skill validation and operations.
5. Extend `UserController` with profile/skill request parsing and response
   serialization.
6. Register the new routes in the existing `service/src/router.cpp`.
7. Reuse the existing MySQL pool and Redis session; add no new infrastructure.

## API Decisions

- All responses keep the `{code,message,data}` envelope.
- `GET /users/profile` returns a default object with null/empty optional fields
  when no row exists; absence is not a 404.
- `PUT /users/profile` uses `INSERT ... ON DUPLICATE KEY UPDATE`.
- `GET /users/skills` returns `data: {"list": [...]}`.
- `POST /users/skills` upserts by `(user_id, name)`, updating `level` when the
  skill already exists. This avoids inventing a new duplicate-skill error code.
- `DELETE /users/skills/{id}` is scoped by `user_id` and is idempotent for an
  absent or non-owned skill. No ownership information is leaked.
- Validation errors return `10001`; unauthenticated requests return `20001`;
  engine failures map to `60002` through the existing error model.

## DAO Design

Add records:

- `UserProfile`: user id, optional target position/experience/location, optional
  salary min/max, optional summary, timestamps.
- `UserSkill`: id, user id, name, level, create time.

Add operations:

- `FindProfile(userId)`;
- `UpsertProfile(profile)`;
- `ListSkills(userId)`;
- `UpsertSkill(userId, name, level)`;
- `DeleteSkill(userId, skillId)`.

All SQL uses prepared statements and includes `user_id` in the predicate where
applicable.

## Service Design

Add authenticated identity resolution:

- `Authenticate(token) -> ServiceResult<uint64_t>`.

Add profile/skill operations:

- read profile;
- update profile;
- list skills;
- upsert skill;
- delete skill.

Validation:

- profile text fields respect schema lengths;
- salary values are non-negative and `salary_min <= salary_max` when both
  exist;
- skill name is non-empty and at most 64 characters;
- skill level is in `[1, 3]`.

Business failures are returned as `ServiceResult`; exceptions are reserved for
unexpected engine failures.

## Controller and Routes

Add:

- `GET /api/v1/users/profile`
- `PUT /api/v1/users/profile`
- `GET /api/v1/users/skills`
- `POST /api/v1/users/skills`
- `DELETE /api/v1/users/skills/{id}`

The Controller parses Bearer tokens and JSON, never accepts `user_id` from the
request body, and reuses the existing response/error mapping.

## Redis, Transactions, and Storage

- Redis: existing session lookup only; no new keys or cache invalidation.
- Transactions: none; each mutation is a single SQL statement.
- File storage: none.
- P1: only the approved request-method parsing and routing capability changes.

## Implementation Phases

### Phase 1: DAO and Database Contract

- add profile/skill DTOs and DAO methods;
- implement parameterized queries and UPSERT behavior;
- add DAO integration tests with user ownership isolation.

### Phase 2: Service Rules

- add authenticated identity resolution;
- implement profile and skill validation/operations;
- add Service tests for success, invalid input, and ownership behavior.

### Phase 3: HTTP Controller and Routes

- add JSON parsing for profile/skill requests;
- register the five routes;
- add HTTP tests for auth, profile, skills, malformed input, and isolation.

### Phase 4: Verification and Documentation

- run normal, ASan, UBSan, and TSan where concurrency is exercised;
- update `docs/ai/project-state.md`;
- record remaining P2 dependencies and move this plan to `completed/`.

## Verification and Rollback

Verification:

- clean `service/` and root configure/build;
- full CTest without MySQL/Redis-independent gaps hidden;
- profile absent/present/update paths;
- skill create, duplicate UPSERT, list, delete, and cross-user isolation;
- malformed JSON, invalid salary/level, and unauthorized requests;
- ASan/UBSan full suite; TSan for concurrent user operations.

Rollback:

- existing user auth routes remain unchanged;
- profile/skill DAO, Service, Controller, and route additions can be removed
  independently;
- no schema migration is required;
- the minimal P1 method capability can remain because it is additive and does
  not alter existing `GET`/`POST` behavior.

## Current Progress

- [x] Research
- [x] Phase 1: DAO and Database Contract
- [x] Phase 2: Service Rules
- [x] Phase 3: HTTP Controller and Routes
- [x] Phase 4: Verification and Documentation

## Verification Results

- Approved P1 freeze exception implemented and verified:
  `PUT` and `DELETE` parser/router coverage passes.
- User DAO profile/skill tests: 2/2 passed.
- User Service profile/skill tests: 2/2 passed.
- User HTTP profile/skill integration tests: 2/2 passed.
- Full Debug CTest: 60/60 passed.
- ASan: 60/60 passed.
- UBSan: 60/60 passed.
- TSan concurrency/User preset: 21/21 passed.
- Focused Release regression after the method extension: 9/9 passed.
- Both `service/` and root Release builds completed successfully.

## Known Issues and Follow-Up

- The API contract does not define duplicate-skill or absent-delete responses;
  this plan selects UPSERT and idempotent DELETE.
- Profile GET absent-row behavior is selected as an empty default object.
- `ConnectionPool` still has no transaction abstraction; resume creation will
  need a separate design because it writes `resumes` and `resume_versions`.
- Job search query parsing and application dependencies remain open for later
  modules.
- The P1 HTTP method extension intentionally supports only `PUT` and `DELETE`
  in addition to the existing `GET` and `POST`; further method or parser
  expansion requires a new reviewed change.
