# ExecPlan 0007: P2 Resume Vertical Slice

> Status: Completed.

## Background

The P2 user/auth/profile/skill path is complete and synchronized. The Resume
contract already exists in the repository, but the implementation requires
cross-cutting capabilities that the current service does not yet provide.

This plan is Level 4 because it crosses HTTP input handling, storage,
database transaction boundaries, authentication, DAO/Service/Controller
boundaries, and filesystem/DB consistency.

Current repository state at planning time:

- `HEAD == origin/main == 470384e`.
- The worktree is clean.
- `docs/ai/project-state.md` still describes an older uncommitted 0006
  snapshot; that documentation lag is not changed by this plan.
- No business code, schema, or API contract is changed while creating this
  plan.

## Goal

Deliver an authenticated Resume Vertical Slice that supports:

- uploading one resume file and creating the resume plus first version;
- listing the current user's resumes with pagination;
- creating a new text version for an owned resume;
- listing versions for an owned resume;
- strict user isolation through Bearer token -> Redis session -> `user_id`;
- bounded, validated multipart input;
- consistent database and filesystem behavior;
- focused DAO, Service, HTTP, storage, and transaction tests.

## Scope

- Reuse the locked `resumes` and `resume_versions` schema.
- Reuse the existing GET/POST HTTP method capability.
- Reuse the existing Redis session mechanism without adding cache keys.
- Support `.pdf`, `.txt`, and `.md` uploads.
- Store upload bytes through a file-storage abstraction.
- Store extracted or supplied text in `resume_versions.content` when it is
  available.
- Add the missing `40002 resume not found` service/API mapping.
- Add the minimum infrastructure required by this slice.

## Non-Goals

- Resume download or delete APIs.
- Object storage or MinIO.
- Streaming uploads or chunked transfer encoding.
- PDF text extraction.
- Resume parsing into structured fields.
- Resume optimization or AI match integration.
- Redis caching or cache invalidation.
- Application, job, or notification module work.
- Changes to `docs/03` schema or `docs/04` endpoint contracts.
- A general-purpose job, scheduler, or asynchronous file-processing system.

## Existing Capabilities

- HTTP supports `GET` and `POST`, which are sufficient for all four locked
  Resume endpoints.
- `HttpRequest` preserves request headers and raw `Content-Length`-bounded
  body bytes in `std::string`.
- `HttpRouter` supports static and `{id}` parameter routes.
- `ConnectionPool` supports parameterized `Query` and `Execute`, including
  `lastInsertId`.
- `SessionManager` resolves a Redis session token to `user_id`.
- `resumes` and `resume_versions` already exist in
  `database/schema/02_resume.sql`.
- `resume_versions` has the required unique key
  `uk_resume_version(resume_id, version)`.
- Schema tests already verify the Resume tables and key index.
- GoogleTest, CMake presets, ASan, UBSan, TSan, and integration-test patterns
  are already available.

## Capability Gaps

### HTTP and multipart

- No multipart/form-data parser exists.
- `Content-Type` is stored but not interpreted.
- Request body size is unbounded before the body is copied into memory.
- Chunked uploads are unsupported; Resume upload will require
  `Content-Length`.

### Database

- `ConnectionPool` has no transaction API.
- `Query` and `Execute` independently acquire and return connections, so they
  cannot share a transaction boundary.
- Resume creation needs `resumes` plus `resume_versions` writes in one atomic
  database transaction.
- Version allocation needs a row lock or equivalent serialization to avoid
  duplicate version races.

### Storage

- No file-storage abstraction exists.
- No storage root is defined in `Config`.
- No runtime storage directory or Docker volume is configured.
- `.gitignore` does not yet ignore local resume storage.
- No atomic temp-file promotion or filesystem cleanup path exists.

### Authentication reuse

- `UserService::Authenticate` is public, but Resume must not depend on
  `UserService`.
- A shared session-authenticator boundary is needed so Resume does not copy
  User authentication logic.

### API and errors

- `ErrorCode` and `ErrorMessage` do not yet include `40002 resume not found`.
- No Resume pagination or multipart validation helpers exist.
- No Resume file/content tests exist.

## Infrastructure Decision

Selected option: **B. Complete a separate infrastructure stage before Resume
implementation.**

Reason:

- bounded request bodies and multipart parsing are HTTP infrastructure;
- transaction scope is a ConnectionPool core change;
- file storage, configuration, and Docker volume behavior are independent
  infrastructure concerns;
- shared session authentication must not be copied from the User module;
- mixing these changes into the Resume feature would create a large,
  difficult-to-review transaction across P1, engine, service, storage, and
  API layers.

Before implementation starts, create and complete the separate prerequisite
[0007A infrastructure plan](../completed/0007A-resume-infrastructure.md)
covering:

1. a configured maximum request-body size enforced before buffering the body;
2. an additive RFC 7578 multipart parser with strict part/count/size limits;
3. `ConnectionPool::BeginTransaction()` and an RAII `Transaction` using one
   dedicated MySQL connection;
4. a local file-storage abstraction with temp writes, atomic promotion,
   removal, path confinement, and configurable root;
5. a shared session-authenticator service used by Resume without coupling to
   `UserService`;
6. storage/transaction/multipart capability tests.

This plan stops before implementation if the prerequisite stage is not
approved and completed. It must not bypass that gate with base64 upload,
direct filesystem calls from ResumeService, or two independent database
writes without a transaction.

## API Contract

All endpoints require `Authorization: Bearer <token>`. The session resolves
the owner user id; clients never submit `user_id`.

### Upload resume and first version

`POST /api/v1/resumes/upload`

- Content type: `multipart/form-data`
- Fields:
  - `title`: required, non-empty, maximum 128 UTF-8 bytes;
  - `file`: required, one file, `.pdf`, `.txt`, or `.md`;
- Body limit: 12 MiB including multipart framing.
- File limit: 10 MiB.
- Response:

```json
{"code":0,"message":"success","data":{"resume_id":30001,"version":1}}
```

### List resumes

`GET /api/v1/resumes?page=1&page_size=20`

- Defaults: `page=1`, `page_size=20`.
- Bounds: `page >= 1`, `1 <= page_size <= 100`.
- Response data:

```json
{
  "list": [
    {"id":30001,"title":"C++后端版","version":3,"created_at":"...","updated_at":"..."}
  ],
  "total": 1,
  "page": 1,
  "page_size": 20
}
```

### Create a text version

`POST /api/v1/resumes/{id}/versions`

- JSON body: `{"content":"..."}`.
- Content must be non-empty and at most 1 MiB.
- Response:

```json
{"code":0,"message":"success","data":{"version_id":30012,"version":4}}
```

### List versions

`GET /api/v1/resumes/{id}/versions`

- Response data:

```json
{"list":[{"id":30012,"version":4,"created_at":"..."}]}
```

No `file_path` is exposed. A missing resume or a resume owned by another user
returns the same `40002 resume not found` result.

## Data Model

No schema migration is planned. The implementation uses the existing columns:

### Resume summary

- `id`
- `user_id`
- `title`
- `create_time`
- `update_time`

### Resume version

- `id`
- `resume_id`
- `version`
- `file_path`
- `content`
- `create_time`

Service DTOs:

- `ResumeSummary`: id, title, latest version, create time, update time.
- `ResumeVersion`: id, resume id, version, optional relative file path,
  optional content, create time.
- `ResumeUpload`: validated title, original filename, extension, bytes.

`MAX(version)` remains derived from `resume_versions`; no `current_version`
column is added.

## Authentication and Ownership

- ResumeController parses the Bearer token and passes only the token into
  ResumeService.
- ResumeService uses the shared session authenticator from the prerequisite
  stage to resolve `user_id`.
- Every read or write is scoped by the authenticated `user_id`.
- Resume existence is checked with a `WHERE id = ? AND user_id = ?` query
  inside the transaction where mutation order matters.
- Cross-user access returns `40002`, not `403`, to avoid leaking resource
  existence.
- No token, file content, or sensitive storage path is logged.

## File Storage Design

### Configuration

Add a future `storage` configuration section in the prerequisite stage:

```yaml
storage:
  root: storage
  max_file_bytes: 10485760
```

Environment override:

- `STORAGE_ROOT`
- `STORAGE_MAX_FILE_BYTES`

### Path layout

Recommended collision-free path:

```text
<storage.root>/{user_id}/resumes/{resume_id}/v{version}.{ext}
```

The database stores a relative path. Absolute paths and client filenames are
never used as storage paths.

The current execution guide shows
`storage/{user_id}/resume/v{version}.{ext}`, which collides when one user has
multiple resumes. Before implementation, update the execution-guide path or
record an approved exception; do not silently use the collision-prone path.

### Storage abstraction

The prerequisite stage provides an interface with operations equivalent to:

- write bytes to a uniquely named temp file under the storage root;
- fsync and close the temp file;
- atomically promote it to the final relative path;
- remove a temp or final file during compensation;
- validate that every relative path remains inside the configured root.

ResumeService depends on the abstraction, not on `std::filesystem` directly.
Tests use a real temporary root and may use a deterministic fake only for
unit-level Service tests.

### Formats and content

- `.txt` and `.md`: validate as text and store the bytes in `content`.
- `.pdf`: validate the `%PDF-` signature and store the file bytes; set
  `content` to `NULL` in 0007.
- PDF text extraction is a follow-up capability and must not be simulated
  with a fake parser.

## Transaction and Consistency Strategy

Upload sequence:

1. Parse and validate multipart metadata and bytes without writing business
   state.
2. Write bytes to a temp file, fsync, and close it.
3. Begin a database transaction.
4. Insert `resumes` and obtain `resume_id`.
5. Lock the owned resume row with `SELECT ... FOR UPDATE`.
6. Calculate `MAX(version) + 1`.
7. Insert `resume_versions` with the final relative path.
8. Atomically promote the temp file to the final path.
9. Commit the transaction.
10. On any pre-commit failure: rollback, remove the promoted file if needed,
    and remove the temp file.

Text-version sequence:

1. Begin a transaction.
2. Select/lock the owned resume row with `FOR UPDATE`.
3. Return `40002` if it does not exist.
4. Calculate the next version.
5. Insert `resume_versions` with `file_path = NULL`.
6. Commit.

Commit-uncertainty rule:

- after a commit failure with unknown outcome, keep the promoted file;
- return an internal failure;
- do not delete a file that may be referenced by a committed row;
- allow a future reconciliation job to find orphan files.

This favors a harmless orphan file over a database row pointing to a missing
file.

## Error Handling

Add or reuse:

- `10001 invalid parameter`: malformed title, type, extension, content,
  pagination value, or file size;
- `10002 malformed request body`: invalid multipart syntax or malformed JSON;
- `20001 unauthorized`: missing or invalid session token;
- `40002 resume not found`: missing or non-owned resume;
- `60001 internal error`: storage/auth infrastructure failure;
- `60002 database error`: database failure.

The existing `{code,message,data}` envelope remains unchanged.

## Implementation Order After the Infrastructure Gate

### Phase 1: Resume DAO

- add Resume DTOs and `IResumeDao`;
- implement parameterized create/list/version queries;
- use transaction-scoped methods for writes;
- add DAO tests for ownership, pagination, ordering, and version allocation.

### Phase 2: Resume Service

- add upload, list, create-version, and list-version operations;
- integrate multipart parsing, file storage, transaction scope, and session
  authentication;
- validate all metadata and content;
- test rollback and compensation behavior.

### Phase 3: Controller and Routes

- add `ResumeController`;
- add path-id and pagination parsing;
- add multipart validation/error mapping;
- register the four routes without changing existing User routes;
- update `RegisterRoutes` and service startup wiring.

### Phase 4: HTTP and Storage Integration Tests

- upload `.txt`, `.md`, and minimal valid `.pdf`;
- verify listing and version listing;
- verify exact stored text for text formats;
- verify PDF file bytes are stored and content is null;
- verify cross-user isolation, invalid token, malformed multipart, oversized
  file, and malformed JSON.

### Phase 5: Verification and Documentation

- run focused Resume tests;
- run transaction/multipart/storage tests;
- update `docs/ai/project-state.md` only when implementation completes;
- archive this plan to `completed/`.

## Tests

Prerequisite capability tests:

- request-body limit rejects before unbounded body accumulation;
- multipart parser accepts one file plus fields, arbitrary binary bytes, LF
  and CRLF boundaries, and rejects malformed/duplicate/oversized parts;
- Transaction commits multiple writes and rolls back on thrown errors;
- file storage confines paths, promotes atomically, and removes temp files.

Resume DAO tests:

- create resume and first version;
- list only owned resumes;
- paginate deterministically;
- create consecutive versions;
- reject cross-user version reads/writes through the user-scoped query.

Resume Service tests:

- title/type/size validation;
- text content behavior;
- PDF metadata behavior;
- storage failure compensates the transaction;
- commit failure keeps a potentially referenced file;
- concurrent version creation produces unique increasing versions.

HTTP tests:

- upload -> list -> create version -> list versions;
- missing/invalid token;
- malformed multipart and unsupported extension;
- oversized request/file;
- user A cannot see or mutate user B's resume;
- client-supplied `user_id` is ignored and never grants authority.

## Verification Level

- Research and planning: no full test matrix.
- Infrastructure and transaction work: L3 focused tests, with TSan only for
  concurrent transaction/version paths.
- Resume DAO/Service/Controller work: L2/L3 focused module and HTTP tests.
- Stage completion: L4 full Debug CTest, full ASan, full UBSan, TSan for
  concurrent version tests, and Release build if build-system behavior
  changes.
- Commit preparation: L5 reuse the completed L4 evidence unless relevant
  files changed afterward.

## Risks

| Priority | Risk | Mitigation |
|---|---|---|
| High | P1 body-limit exception | Keep it narrow, bounded, and covered by parser tests |
| High | ConnectionPool transaction refactor | RAII scope, same-connection guarantees, rollback tests |
| High | DB/file partial failure | Promote before commit and compensate; keep file on unknown commit |
| High | Path traversal or symlink escape | Generated paths only, root confinement, no client path components |
| Medium | Version race | Lock owned resume row with `FOR UPDATE` |
| Medium | PDF content unavailable | Explicitly leave content null; defer extraction |
| Medium | Multipart parser complexity | One-file MVP, strict limits, RFC 7578 tests |
| Medium | Storage path contract mismatch | Resolve execution-guide path before implementation |
| Medium | No idempotency key | Upload retries may create duplicate resumes; defer |
| Low | Orphan files after crash | Keep DB/file integrity; add reconciliation later |

## Stop Conditions

Stop and return to design if any of these occurs:

- transaction support requires changing the public ConnectionPool ownership
  model beyond an additive RAII transaction scope;
- request-body limiting cannot be enforced before the body is buffered;
- multipart parsing cannot be bounded without a P1 network/body redesign;
- the execution guide's storage path conflicts with the collision-free path
  and no contract decision is made;
- PDF upload cannot be made safe without PDF parsing/scanning scope that is
  not approved;
- the locked API requires download/delete behavior that the plan does not
  include;
- filesystem and database consistency tests cannot distinguish normal
  failure from commit uncertainty.

## Rejected Alternatives

- Base64 JSON upload: avoids multipart but violates the locked API contract.
- Direct `std::filesystem` calls inside ResumeService: bypasses the storage
  abstraction and makes consistency testing difficult.
- Independent `resumes` and `resume_versions` inserts without a transaction:
  can leave partial records.
- Insert first and write the file after commit: can leave DB rows pointing to
  missing files.
- Copy authentication from `UserService`: creates cross-module coupling or
  duplicate logic.
- Add Redis caching now: Resume is not read-heavy yet and no cache key is
  defined for this slice.
- Add PDF text extraction now: introduces a parser/security surface outside
  the vertical-slice goal.

## Decisions and Open Questions

Selected decisions:

- Task level: Level 4 overall.
- Infrastructure split: Option B, separate prerequisite stage.
- Storage: local filesystem adapter behind an interface.
- Consistency: DB transaction with temp-file promotion before commit.
- PDF text extraction: deferred; metadata/file storage only in 0007.
- Redis: session lookup only, no new keys.
- Schema and locked endpoint contracts: unchanged.

Items requiring user approval before implementation:

1. Approve Option B and create the separate `0007A` infrastructure plan.
2. Approve the collision-free storage path and the corresponding
   execution-guide update.
3. Approve accepting PDF files without text extraction for this slice.

## Current Progress

- [x] Capability Check
- [x] Research
- [x] Design
- [x] ExecPlan
- [x] Prerequisite infrastructure stage
  ([0007A](../completed/0007A-resume-infrastructure.md))
- [x] Phase 1: Resume DAO
- [x] Phase 2: Resume Service
- [x] Phase 3: Controller and Routes
- [x] Phase 4: HTTP and Storage Integration Tests
- [x] Phase 5: Verification and Documentation

Implementation notes:

- Added minimal query-aware route matching so `GET /api/v1/resumes?page=...`
  reaches the Resume route without changing HTTP body framing.
- Moved Bearer header parsing into the shared auth module so User and Resume
  controllers use the same authentication parsing.
- Implemented the approved collision-free storage path
  `<root>/{user_id}/resumes/{resume_id}/v{version}.{ext}`, and updated
  `docs/08-execution-guide.md` to match.
- These changes were required by the approved API and authentication-reuse
  constraints. No schema or API contract change was introduced.

## Verification Results

- Focused Resume DAO/Service tests: 6/6 passed.
- Resume HTTP integration tests: 3/3 passed.
- User/Auth/Router regression subset: 16/16 passed.
- Debug full CTest: 76/76 passed.
- ASan full CTest: 76/76 passed.
- UBSan full CTest: 76/76 passed.
- TSan default gate: 32/32 passed.
- TSan external-service subset: 6/6 passed.
- Service and root builds passed.

The implementation is complete in the working tree and awaits review/commit.

## Verification and Rollback

Verification evidence must record the exact commands, scopes, result counts,
and any environment dependency. Final verification uses the current L1-L5
policy in `docs/quality/README.md`.

Rollback:

- Resume routes and module code can be removed independently;
- the filesystem adapter can be disabled without changing User routes;
- no Resume schema migration is needed;
- transaction changes require their own prerequisite rollback plan;
- stored test files must be removed by tests using a temporary root.
