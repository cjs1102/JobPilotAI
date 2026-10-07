# ExecPlan 0009: P2 Application Vertical Slice

> Status: Completed and verified on 2026-10-07.

## 1. Objective

Deliver an authenticated Application vertical slice that composes the existing
User, Job, and ResumeVersion capabilities:

```text
User + Job + ResumeVersion
  -> Application
```

The minimum closed loop is:

- create an Application for an authenticated user and active Job;
- reject duplicate `(user_id, job_id)` applications;
- list the current user's applications with status filtering and pagination;
- update application status through a strict state machine;
- return current-user Application statistics.

## 2. Current Capability

### User / Session

- Registration, login, Redis Session, and shared Bearer authentication exist.
- `ISessionAuthenticator` returns a trusted `user_id`.
- Existing authenticated modules never accept client-supplied `user_id` as an
  authority source.
- Session authentication does not currently perform an additional user-status
  query.

### Job

- Public active-job search and detail are implemented.
- `IJobDao` owns Job and JobRequirement queries.
- There is currently no narrow transaction-aware method for checking whether
  an active Job exists.
- Jobs can later become inactive; historical Application responses must not
  require the Job to remain active.

### ResumeVersion

- Resume upload, list, text version creation, and version list are implemented.
- Resume ownership is enforced through `user_id`.
- Current DAO operations validate ownership by `resume_id`, not directly by
  `resume_version_id`.
- There is no transaction-aware `OwnedVersionExists(user_id, version_id)`
  capability yet.

### Database

- The canonical `applications` table already exists.
- It has `UNIQUE(user_id, job_id)` for duplicate prevention.
- It has `INDEX(user_id, status)` for list and statistics.
- It intentionally has no physical foreign keys.
- No migration is required.

### Router

- `GET`, `POST`, and `PUT` are available.
- Query-aware route matching is available.
- Static routes are matched before parameter routes.
- Authentication is handled by the Controller through the shared Bearer
  parser and SessionAuthenticator.

### Error Handling

- Existing resource codes include `40001 job not found` and
  `40002 resume not found`.
- Application-specific codes `40003`, `40004`, and `40005` are documented but
  not yet present in `ErrorCode`.

### Testing

- MySQL DAO integration tests exist.
- Service tests use fake DAO patterns.
- HTTP integration tests use real HttpServer, MySQL, and Redis Session.
- `test_user_support.h` and `test_job_support.h` provide reusable fixtures.
- No dedicated ResumeVersion fixture helper currently exists.

## 3. Scope

### In Scope

- `IApplicationDao` and `ApplicationDao`;
- Application DTOs and status model;
- `ApplicationService`;
- `ApplicationController`;
- Router and `main` wiring;
- reads/writes on the existing `applications` table;
- transaction-aware Job and ResumeVersion validation ports;
- Application status state machine;
- Application list, pagination, status filter, and statistics;
- duplicate application handling;
- DAO, Service, and HTTP tests;
- minimal Application fixture/cleanup helper.

### Out of Scope

- schema migration;
- Redis Application cache;
- asynchronous Application processing;
- file storage;
- new HTTP methods;
- new network infrastructure;
- third-party recruitment-platform integration;
- background job submission;
- notification generation;
- AI integration.

## 4. Architecture

Dependency direction:

```text
HTTP
  -> ApplicationController
  -> ApplicationService
      -> IApplicationDao
      -> Job read port
      -> ResumeVersion read port
      -> ConnectionPool::Transaction
```

Rules:

- Application depends on Job and ResumeVersion capabilities.
- Job and Resume do not depend on Application.
- User identity always comes from Session.
- ApplicationService owns business rules and transaction boundaries.
- ApplicationDao owns `applications` table access.
- Cross-module validation uses minimal transaction-aware ports.
- The Controller owns HTTP/JSON parsing and response mapping only.

Recommended cross-module ports:

```cpp
bool ActiveJobExists(
    jobpilot::core::ConnectionPool::Transaction &transaction,
    std::uint64_t jobId);

bool OwnedVersionExists(
    jobpilot::core::ConnectionPool::Transaction &transaction,
    std::uint64_t userId,
    std::uint64_t resumeVersionId);
```

These should be implemented by the owning Job and Resume data-access layer,
not by copying Job/Resume SQL into ApplicationService.

For list response projection, `ApplicationDao` may JOIN `jobs` to obtain
`job_title` and `company`. This is a read-only projection exception and does
not create a reverse module dependency. Job status is not re-filtered in list
queries because historical applications must remain visible after a Job is
inactivated.

## 5. Data Model

Existing `applications` fields:

| Field | Purpose |
|---|---|
| `id` | Application primary key |
| `user_id` | Owner resolved from Session |
| `job_id` | Related Job |
| `resume_version_id` | Resume version used, nullable in schema |
| `status` | 0-7 state machine |
| `apply_time` | First transition into applied status |
| `remark` | Optional note |
| `create_time` | Creation timestamp |
| `update_time` | Last update timestamp |

Constraints:

- `UNIQUE(user_id, job_id)`;
- `INDEX(user_id, status)`.

No migration is added.

No physical foreign keys are added.

Consistency is guaranteed by ApplicationService validation and the unique
constraint.

## 6. State Machine

Existing status meanings:

| Value | Meaning |
|---:|---|
| 0 | 收藏 |
| 1 | 待投递 |
| 2 | 已投递 |
| 3 | 笔试 |
| 4 | 一面 |
| 5 | 二面 |
| 6 | Offer |
| 7 | 拒绝 |

Allowed transitions:

```text
0 -> 1
1 -> 2
2 -> 3
3 -> 4
4 -> 5
5 -> 6
0/1/2/3/4/5 -> 7
```

Rules:

- `6` and `7` are terminal.
- No skipping states.
- No backward transitions.
- Updating to the same status is not a valid transition in this slice and
  returns `40005`.
- A status outside `0..7` is invalid input and returns `10001`.
- A valid status combined with a disallowed source transition returns `40005`.
- The first transition into status `2` sets `apply_time`; later transitions
  preserve it.

## 7. API Contract

### Create Application

```text
POST /api/v1/applications
Authorization: Bearer <token>
```

```json
{
  "job_id": 20001,
  "resume_version_id": 30012
}
```

Response:

```json
{"code":0,"message":"success","data":{"application_id":40001,"status":0}}
```

`resume_version_id` decision:

- the database column remains nullable;
- recommendation for 0009 is to require a non-null `resume_version_id` on
  create because the locked API has no endpoint for attaching a ResumeVersion
  later;
- allowing null while transitions above status `0` are possible creates an
  unrecoverable state with the current API;
- if the product explicitly needs favorite-before-resume, the API/state rules
  must be confirmed before implementation; do not silently add a new API.

### List Applications

```text
GET /api/v1/applications?status=2&page=1&page_size=20
Authorization: Bearer <token>
```

Rules:

- `status` is optional;
- `page >= 1`;
- `1 <= page_size <= 100`;
- list only the authenticated user's applications;
- default order is `update_time DESC, id DESC`;
- response contains `job_id`, `job_title`, `company`, `status`, `apply_time`,
  and `remark`.

### Update Status

```text
PUT /api/v1/applications/{id}/status
Authorization: Bearer <token>
```

```json
{"status":3}
```

Rules:

- Application must belong to Session user;
- transition must follow Section 6;
- missing/non-owned Application returns `40003`;
- invalid transition returns `40005`.

### Statistics

```text
GET /api/v1/applications/statistics
Authorization: Bearer <token>
```

```json
{"total":50,"interview":5,"offer":1,"rejected":12}
```

Statistics scope:

- `total`: all current-user Applications;
- `interview`: statuses `3/4/5`;
- `offer`: status `6`;
- `rejected`: status `7`.

`statistics` must be registered as a static route before/independently of the
`{id}` parameter route.

## 8. Error Model

Add:

```text
40003 application not found
40004 duplicate application
40005 invalid status transition
```

Required mappings:

| Condition | Error |
|---|---|
| Job missing/inactive | `40001` |
| ResumeVersion missing/not owned | `40002` |
| Application missing/not owned | `40003` |
| Duplicate `(user_id, job_id)` | `40004` |
| Invalid state transition | `40005` |
| Malformed JSON/query/status type | `10001` or `10002` according to existing envelope |
| Authentication failure | existing `20001` |
| Database failure | `60002` |

No existing error meaning is changed.

## 9. Transaction / Concurrency

### Create

```text
BEGIN
  -> validate active Job
  -> validate ResumeVersion ownership
  -> atomically insert Application
COMMIT
```

Duplicate safety:

- unique key `(user_id, job_id)` is the final concurrency guarantee;
- do not rely on `SELECT -> INSERT` as the only duplicate check;
- use an atomic insert/upsert strategy.

### Status Update

```text
BEGIN
  -> SELECT owned Application FOR UPDATE
  -> validate current status and target status
  -> set apply_time when entering status 2
  -> UPDATE status
COMMIT
```

The row lock serializes concurrent updates for the same Application.

## 10. Cross-module Ports

### Job Port

```cpp
ActiveJobExists(Transaction&, jobId)
```

Semantics:

- true only when `jobs.status = 1`;
- transaction-aware;
- no Job write.

### ResumeVersion Port

```cpp
OwnedVersionExists(Transaction&, userId, resumeVersionId)
```

Semantics:

- version belongs to a Resume owned by the Session user;
- transaction-aware;
- no Resume write.

Do not inject full JobService or ResumeService into ApplicationService when
only these narrow capabilities are needed.

## 11. DAO Design

`IApplicationDao` minimum operations:

- `Create`;
- `FindOwnedForUpdate`;
- `List`;
- `Count`;
- `UpdateStatus`;
- `Statistics`.

Suggested result model:

```cpp
enum class ApplicationCreateResult
{
    kCreated,
    kDuplicate
};
```

Atomic duplicate strategy:

```sql
INSERT INTO applications (...)
VALUES (...)
ON DUPLICATE KEY UPDATE id = LAST_INSERT_ID(id)
```

Result semantics:

- `affectedRows == 1`: created;
- `affectedRows == 0` or `2`: duplicate;
- expected duplicate `lastInsertId` is the existing Application ID;
- any other condition is a database error;
- integration tests must verify affected-row behavior with the current
  ConnectionPool flags.

This uses the existing unique key and avoids a fragile generic database-error
message match.

List query may JOIN `jobs` for response projection:

- application fields come from `applications`;
- `job_title` and `company` come from `jobs`;
- Job status is not used to hide historical applications.

Application DAO must not write jobs, resumes, or resume versions.

## 12. Testing Plan

### DAO

- create Application;
- duplicate result;
- list for one user;
- list status filter;
- pagination and total;
- ordering;
- status update;
- statistics counts;
- user isolation.

### Service

- Job missing;
- Job inactive;
- ResumeVersion missing;
- ResumeVersion owned by another user;
- duplicate Application;
- valid transitions `0 -> 1 -> 2 -> 3 -> 4 -> 5 -> 6`;
- transition to `7` from each non-terminal state;
- skipped state;
- backward state;
- terminal-state update;
- status value outside `0..7`;
- `apply_time` set only when entering `2`;
- DAO error mapping.

### HTTP

- unauthenticated request;
- invalid/expired token;
- create success;
- duplicate `40004`;
- list and pagination;
- status filter;
- update status;
- `40003/40004/40005`;
- cross-user isolation;
- statistics response.

### Fixture

- reuse `test_user_support.h`;
- reuse `test_job_support.h`;
- add the minimum helper needed to create a ResumeVersion owned by a user;
- no fixed IDs;
- clean up Applications, ResumeVersions, Resumes, Jobs, users, and sessions
  created by each test.

## 13. Implementation Steps

### Step 1: Cross-module Read Ports

- Goal: provide transaction-aware active Job and owned ResumeVersion checks.
- Change: add narrow methods to Job and Resume data-access interfaces.
- Dependency: none.
- Risk: broad interface leakage; keep methods minimal.
- Verification: focused Job/Resume port tests.

### Step 2: Application DAO

- Goal: implement Application persistence and queries.
- Change: DTOs, status conversion, `IApplicationDao`, `ApplicationDao`.
- Dependency: existing applications schema.
- Risk: duplicate affected-row semantics; verify with MySQL integration test.
- Verification: DAO tests.

### Step 3: Application Service

- Goal: business rules and transaction boundaries.
- Change: validation, duplicate mapping, state machine, list/status/statistics.
- Dependency: Steps 1-2.
- Risk: cross-module transaction lifetime and `apply_time`.
- Verification: Service tests with fake DAOs/ports.

### Step 4: Application Controller

- Goal: HTTP/JSON adaptation and authentication.
- Change: auth through Bearer Session, request parsing, response mapping.
- Dependency: Step 3.
- Risk: query/path/status error mapping.
- Verification: Controller/HTTP tests.

### Step 5: Router / main Wiring

- Goal: register the four authenticated routes.
- Change: optional ApplicationController in route registration and service
  construction.
- Dependency: Step 4.
- Risk: static `statistics` route must not be shadowed by `{id}`.
- Verification: route tests and real HTTP.

### Step 6: DAO Tests

- Goal: verify persistence and ownership at database boundary.
- Change: Application DAO tests and fixture helper.
- Dependency: Step 2.
- Risk: test data leakage; cleanup by tracked IDs.
- Verification: DAO test target.

### Step 7: Service Tests

- Goal: verify validation, concurrency semantics, and state transitions.
- Change: fake DAO/port tests.
- Dependency: Step 3.
- Risk: duplicating DAO behavior instead of testing Service rules.
- Verification: Service test target.

### Step 8: HTTP Tests

- Goal: verify authenticated end-to-end behavior.
- Change: HTTP create/list/status tests.
- Dependency: Steps 4-5.
- Risk: session/job/resume fixture coupling.
- Verification: real HttpServer tests.

### Step 9: Statistics

- Goal: verify status aggregation and route behavior.
- Change: DAO aggregate query, Service mapping, Controller response.
- Dependency: Steps 2-5.
- Risk: ambiguous `interview` definition.
- Verification: DAO and HTTP tests.

### Step 10: Verification

- Goal: prove the complete slice.
- Change: no production change.
- Dependency: Steps 1-9.
- Risk: test matrix repetition.
- Verification: focused development tests, then one L4 stage gate.

### Step 11: Documentation / Archive

- Goal: record actual outcomes and archive the plan.
- Change: project state, quality baseline, only necessary API clarifications.
- Dependency: Step 10.
- Risk: documenting behavior not observed.
- Verification: `git diff --check`, completed plan links.

## 14. Verification Strategy

Development validation:

- Application DAO tests;
- Application Service tests;
- Application HTTP tests;
- only relevant User/Job/Resume regressions.

Final L4 after implementation completes:

- Debug full CTest;
- ASan full CTest;
- UBSan full CTest;
- TSan only if concurrency-sensitive paths require it;
- service and root Release builds.

This planning task does not run any of these commands.

## 15. Exit Criteria

0009 is complete only when:

- all four Application APIs are available;
- user identity comes only from Session;
- active Job validation is correct;
- ResumeVersion ownership validation is correct;
- duplicate creation is concurrency-safe and returns `40004`;
- state machine and `apply_time` behavior are verified;
- user isolation is verified;
- statistics are correct;
- DAO, Service, and HTTP tests pass;
- final L4 verification passes;
- project state and quality baseline are updated;
- this ExecPlan is archived to `completed/`.

## Current Progress

- [x] Research
- [x] Capability Check
- [x] Design
- [x] ExecPlan
- [x] Cross-module read ports
- [x] Application DAO
- [x] Application Service
- [x] Application Controller
- [x] Router / main
- [x] Tests
- [x] Statistics
- [x] L4 verification
- [x] Documentation / archive

## Verification Results

- Application DAO tests: 2/2 passed.
- Application Service tests: 4/4 passed.
- Application HTTP tests: 2/2 passed.
- Debug full CTest: 97/97 passed.
- ASan full CTest: 97/97 passed.
- UBSan full CTest: 97/97 passed.
- TSan default gate: 36/36 passed.
- TSan external-service tests: 6/6 passed.
- Service and root Release builds passed.

The implementation was complete and verified in the working tree when this
plan was archived. Commit and GitHub synchronization remain separate
delivery actions.
