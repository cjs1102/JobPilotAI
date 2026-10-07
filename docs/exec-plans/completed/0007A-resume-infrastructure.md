# ExecPlan 0007A: Resume Infrastructure

> Status: Completed.

## Background

ExecPlan 0007 identifies infrastructure that must exist before the Resume
vertical slice can be implemented safely:

- bounded HTTP request bodies;
- multipart/form-data parsing;
- MySQL transaction scope;
- local file storage;
- storage configuration and Docker volume;
- reusable session authentication.

This plan implements only those shared capabilities. It does not implement
Resume DAO, Service, Controller, routes, schema, or API behavior.

Planning baseline:

- `HEAD == origin/main == 470384e`.
- Worktree clean before this plan.
- Existing HTTP body framing and User/Profile/Skill authentication are frozen
  regression boundaries.

## Goal

Provide tested, reusable infrastructure with explicit ownership, limits,
failure behavior, and rollback semantics so ExecPlan 0007 can focus on Resume
business behavior.

## Scope

- Add a maximum HTTP request-body size enforced before body buffering.
- Add an additive RFC 7578 multipart/form-data parser for one file plus simple
  fields.
- Add an RAII MySQL transaction API to `ConnectionPool`.
- Add a local file-storage abstraction with temp write, atomic promotion,
  path confinement, read/existence checks, and compensation removal.
- Add storage root and maximum file-size configuration.
- Add a dedicated Compose storage volume and gitignore rule.
- Add a reusable SessionAuthenticator backed by Redis SessionManager.
- Add focused capability tests for every new boundary.

## Non-Goals

- Resume DAO, Service, Controller, routes, or API registration.
- Resume schema changes.
- PDF text extraction.
- Download and delete APIs.
- Resume cache keys.
- Streaming or chunked request bodies.
- Generic multipart support for nested parts, multiple files, or arbitrary
  media workflows.
- Object storage.
- Changes to EventLoop, Poller, EpollPoller, Channel, Buffer, TcpConnection,
  TcpServer, or existing HTTP body framing semantics.

## Capability Gaps

- `HttpRequestParser` accepts any `Content-Length` and buffers the complete
  body in memory.
- `Content-Type` is not parsed and no multipart parser exists.
- `ConnectionPool::Query` and `Execute` use separate connection leases and
  cannot share a transaction.
- No file-storage abstraction or storage configuration exists.
- Docker Compose has no Resume storage volume.
- `.gitignore` does not exclude local runtime storage.
- Authentication logic currently lives in `UserService`; Resume cannot depend
  on UserService or copy its logic.

## Infrastructure Contracts

### 1. Bounded HTTP Request Body

Location:

- `HttpRequestParser` owns the enforcement point.
- The limit is checked in `ParseHeaders` immediately after parsing
  `Content-Length`, before `kParseBody` or body accumulation.

Contract:

- Default maximum request body: 12 MiB.
- Resume file maximum enforced by upper layers: 10 MiB.
- Requests with `Content-Length > limit` return `ParseResult::kError` without
  waiting for or copying the body.
- Existing requests at or below the limit preserve current behavior.
- `HttpServer` may receive the limit through a constructor parameter or
  documented setter while preserving existing constructor call sites.
- No chunked transfer encoding is added.

Rejected location:

- Checking only in a Controller or Service is insufficient because the body
  has already been buffered by then.

### 2. Multipart/Form-Data Parser

Location:

- Additive HTTP component under `jobpilot/http`.
- No changes to request-line, header, or body framing state transitions.

Contract:

- Parse one `multipart/form-data` payload using `Content-Type` boundary.
- Return lightweight parts containing:
  - field name;
  - optional filename;
  - optional content type;
  - `std::string_view` body referencing the original request body.
- Support the minimum Resume shape: text fields plus one file part.
- Enforce:
  - boundary length at most 70 bytes;
  - at most 8 parts;
  - at most 8 KiB of headers per part;
  - one file part maximum.
- Reject missing/duplicate/invalid boundary, malformed headers, missing
  closing delimiter, duplicate file, and malformed disposition.
- Return stable parser error categories; Controllers map them to
  `10002 malformed request body`.

Copy avoidance:

- The parser receives `std::string_view` for contentType and body.
- Part bodies remain views into the request body.
- Only short field names, filenames, content types, and boundary text are
  copied into metadata strings.

### 3. MySQL RAII Transaction

Location:

- `ConnectionPool` in the database engine.

Contract:

- Add `ConnectionPool::BeginTransaction()`.
- The returned `Transaction` exclusively leases one `MYSQL*` from the pool.
- Transaction methods:
  - parameterized `Query`;
  - parameterized `Execute`;
  - explicit `Commit`;
  - explicit `Rollback`.
- Destructor rolls back if the transaction is still active, then returns the
  connection to the pool.
- Move-only ownership prevents two transaction objects from releasing the
  same connection.
- Existing `ConnectionPool::Query` and `Execute` signatures remain unchanged.
- Shared prepared-statement execution helpers may be extracted internally,
  but existing behavior and error messages remain stable.

### 4. Local File Storage

Location:

- Add a storage interface and local filesystem implementation under the
  engine layer.

Contract:

- Configurable storage root.
- Write bytes to a unique temp file under `<root>/.tmp`.
- Flush/fsync and close before promotion.
- Promote temp to a relative final path with atomic same-volume rename.
- Reject absolute paths, empty paths, `..`, root escapes, and symlink-based
  traversal.
- Provide read and existence checks for capability tests.
- Provide removal for compensation; missing files are idempotent.
- Runtime files use restrictive permissions where supported.
- Temp and final files stay under the same storage root.

Orphan policy:

- A pre-commit failure removes temp and promoted files.
- If commit outcome is unknown, keep the file because the database row may
  have committed.
- Orphan files are tolerated and may be reconciled by a future maintenance
  job; missing files for committed rows are not tolerated.

### 5. Configuration and Docker

Config additions:

```yaml
storage:
  root: storage
  max_file_bytes: 10485760
```

Environment overrides:

- `STORAGE_ROOT`;
- `STORAGE_MAX_FILE_BYTES`.

Docker:

- Declare a dedicated `resume_storage` volume in Compose for the future
  service mount.
- Do not add a fake service container merely to consume the volume.

Git:

- Ignore `/storage/` so runtime uploads are never committed.

### 6. SessionAuthenticator

Location:

- Shared service infrastructure under `jobpilot/service/auth`.

Contract:

- Accept a Bearer token string.
- Resolve `user_id` through existing `SessionManager`.
- Return `ServiceResult<std::uint64_t>`.
- Map empty/missing/expired tokens to the existing unauthorized behavior.
- Map Redis failures to `kInternalError` and log without token contents.
- No HTTP or UserController dependency.
- UserService may delegate to the shared authenticator without changing
  observable User/Profile/Skill behavior.
- Resume can depend on the shared interface without depending on UserService.

## Implementation Order

### Phase 0: Capability tests

- Add failing-focused tests for body limit, multipart parser, transaction,
  file storage, config, and session authentication.

### Phase 1: HTTP body limit

- Add parser limit and HttpServer wiring.
- Run parser/server regression tests.

### Phase 2: Multipart parser

- Add additive parser and focused tests.
- Run HTTP parser/router/server regression tests.

### Phase 3: Transaction

- Add RAII transaction ownership and share prepared-statement execution
  helpers internally.
- Run MySQL transaction tests and existing database tests.

### Phase 4: FileStorage

- Add interface, local implementation, and filesystem tests.
- Run focused storage tests.

### Phase 5: Config and Docker

- Add storage configuration, environment overrides, Compose volume, and
  gitignore rule.
- Run config tests and `docker compose config`.

### Phase 6: SessionAuthenticator

- Add shared authenticator.
- Delegate UserService authentication to it while preserving the public
  UserService constructor and behavior.
- Run User service/HTTP tests.

### Phase 7: Integration capability tests

- Verify bounded multipart body flows through parser and multipart parsing.
- Verify transaction plus file promotion rollback/compensation.
- Verify config-derived storage root.
- Run the affected full subset and then the L4 stage-completion matrix.

## Failure and Rollback Semantics

- Body over limit: no body buffering, no business call.
- Malformed multipart: no storage write, no database call.
- Transaction exception: destructor attempts rollback; connection is returned
  or discarded by pool health checks.
- Explicit rollback failure: connection is not reused.
- File temp write failure: no database transaction starts.
- Promote failure: transaction rolls back and temp/final cleanup is attempted.
- Commit failure:
  - explicit rollback is attempted when the outcome is known to be failed;
  - if outcome is unknown, keep the promoted file and report an internal
    error.
- Removal failure during compensation: preserve DB safety, log the residual
  path without content, and leave the orphan for reconciliation.

## Known Infrastructure Clarifications

- The 12 MiB value in `HttpRequestParser` is a global HTTP request-body
  capability limit. It protects all routes from unbounded body buffering.
- `storage.max_file_bytes` defaults to 10 MiB and is the Resume/business file
  limit. ResumeService must enforce it after multipart parsing; it does not
  replace the HTTP body limit.
- `resume_storage` is intentionally declared in Compose before the backend
  service exists. It will be mounted only when Resume deployment wiring is
  implemented. Adding a placeholder service or expanding deployment scope in
  0007A is prohibited.

## Tests

HTTP:

- `Content-Length` above limit returns parse error before body consumption.
- limit boundary accepted.
- existing GET/POST/PUT/DELETE parser and server tests remain green.

Multipart:

- valid text fields plus one binary file;
- quoted boundary and quoted disposition parameters;
- CRLF and body bytes containing CR/LF;
- malformed boundary, truncated body, missing final boundary, duplicate file,
  header limit, part limit, and unsupported content type.

Transaction:

- commit persists;
- explicit rollback discards;
- destructor rollback on exception;
- transaction query/execute uses one connection;
- move-only ownership and no double return.

FileStorage:

- binary write, promote, read, exists, remove;
- traversal and absolute-path rejection;
- destination collision behavior;
- temp cleanup and idempotent removal.

Config:

- YAML defaults;
- environment overrides;
- invalid negative/zero/type values;
- storage root non-empty validation.

SessionAuthenticator:

- token maps to user id;
- missing/invalid token returns unauthorized;
- Redis failure maps to internal error;
- existing UserService/User HTTP behavior remains unchanged.

## Verification Level

- During implementation: L1/L2 focused tests for each capability.
- HTTP/network changes: L3 focused regression.
- Transaction/concurrency changes: TSan only for concurrent transaction or
  connection tests.
- Stage completion: L4 full Debug CTest, ASan, UBSan, conditional TSan.
- Commit preparation: L5 reuse valid L4 evidence unless relevant files change.

## Risks

| Priority | Risk | Mitigation |
|---|---|---|
| High | HTTP body limit changes P1 behavior | Check before buffering, default limit high enough for current tests |
| High | Multipart parser accepts malformed binary input | Strict delimiter/header/part limits and focused malformed tests |
| High | Transaction double-return or lifetime bug | Move-only RAII ownership and MySQL integration tests |
| High | Path traversal or cross-volume rename | Root-confined relative paths and same-root temp directory |
| Medium | Storage/DB failure ambiguity | Promote before commit; keep file on unknown commit |
| Medium | UserService auth refactor regression | Delegate without changing observable behavior; run User tests |
| Medium | Unused Compose volume | Declare for future service mount; validate compose config |
| Medium | Additional memory copy in multipart parser | Return string_view parts into existing request body |

## Stop Conditions

Stop and return to design if:

- body-limit enforcement cannot occur before buffering;
- multipart parsing requires changing the HTTP body framing state machine;
- transaction support requires changing existing Query/Execute signatures or
  connection ownership semantics;
- atomic rename cannot guarantee same filesystem;
- path confinement cannot be made deterministic;
- UserService authentication behavior changes;
- P1 regression tests fail for GET/POST/PUT/DELETE or body framing;
- implementation requires Resume DAO, Service, Controller, routes, schema,
  PDF parsing, download, delete, or cache work.

## Current Progress

- [x] Capability Research
- [x] Infrastructure Design
- [x] ExecPlan
- [x] Capability tests
- [x] HTTP body limit
- [x] Multipart parser
- [x] Transaction
- [x] FileStorage
- [x] Config and Docker
- [x] SessionAuthenticator
- [x] Integration capability tests

## Verification Results

- Focused capability tests passed for HTTP body limits, multipart parsing,
  config, file storage, MySQL transaction commit/rollback, and session
  authentication.
- UserService and User HTTP regression tests passed.
- Service incremental build passed.
- Root incremental build passed.
- Debug full CTest: 65/65 passed.
- ASan full CTest: 65/65 passed.
- UBSan full CTest: 65/65 passed.
- TSan concurrency/User preset: 21/21 passed.
- TSan external-service preset: 6/6 passed.
- `docker compose --env-file .env.example config -q` passed.
- `storage/` is ignored by Git.

No Resume business code, schema, or API route is part of this plan.
