# ExecPlan 0010: P2 Job Ingestion Vertical Slice

> Status: Completed and verified on 2026-10-07.
>
> Task level: L3.

## 1. Objective

Deliver a trusted, idempotent, transactional Job ingestion path:

```text
Trusted importer / future crawler
  -> InternalJobImportController
  -> JobIngestionService
  -> IJobIngestionDao
  -> MySQL Transaction
  -> jobs + job_requirements
```

This closes the current upstream data gap:

```text
Job Ingestion
  -> Job Read
  -> Application
```

The result is that imported jobs are visible through the existing public Job
Read APIs and can be referenced by the existing Application APIs without
direct database manipulation.

## 2. Current Capability

### Existing

- Job Read DAO, Service, and Controller.
- Public `GET /api/v1/jobs` and `GET /api/v1/jobs/{id}`.
- `jobs` and `job_requirements` tables already exist.
- MySQL `ConnectionPool` and move-only RAII `Transaction`.
- Parameterized SQL for all current DAO operations.
- HTTP JSON body parsing with a global 12 MiB body limit.
- Query-aware Router with static routes matched before parameter routes.
- Existing Job fixtures and DAO/Service/HTTP tests.
- Existing Application validation can reference an active Job.

### Missing

- There is no formal Job write path.
- Job data currently depends on test fixtures or direct database manipulation.
- There is no internal caller authentication boundary for trusted ingestion.
- There is no atomic batch import operation for both `jobs` and
  `job_requirements`.
- There is no reusable ingestion interface for a future crawler or dataset
  importer.

## 3. Scope

### In Scope

- Job ingestion write DAO.
- Job ingestion Service.
- Internal Job import Controller.
- `POST /internal/jobs/import`.
- Environment-provided internal token.
- `source_url` idempotent upsert.
- Requirements replacement in the same transaction.
- Atomic batch import with a maximum of 100 Jobs.
- Input validation and stable created/updated/total result counters.
- DAO, Service, HTTP, rollback, and idempotency tests.
- Minimum dataset sample during implementation, not during this planning task.
- Necessary CMake/wiring changes.
- Reuse of existing MySQL, Transaction, HTTP, Router, and Job test
  infrastructure.

### Out of Scope

- Crawler implementation.
- RabbitMQ.
- AI Task, AI Service, LLM, or Embedding.
- Gateway or Nginx changes.
- Admin UI.
- `users.role`, RBAC, or role migration.
- Complete Job CRUD.
- Redis Job cache.
- Job skill filter or full-text search.
- Notification.
- Interview records.
- Frontend.
- Schema migration.

## 4. Internal Contract

### Endpoint

```text
POST /internal/jobs/import
X-Internal-Token: <environment secret>
Content-Type: application/json
```

### Request

```json
{
  "jobs": [
    {
      "company": "某公司",
      "title": "C++后端开发",
      "location": "深圳",
      "salary_min": 20,
      "salary_max": 35,
      "salary_currency": "CNY",
      "description": "...",
      "source": "import",
      "source_url": "https://...",
      "requirements": [
        {
          "skill": "C++",
          "level": 3
        }
      ]
    }
  ]
}
```

### Success Response

The internal success response is the compact contract required by 0010:

```json
{
  "created": 1,
  "updated": 0,
  "total": 1
}
```

Definitions:

- `total` is the number of jobs accepted in the request batch.
- `created` is the number of new Jobs inserted.
- `updated` is the number of existing Jobs matched by `source_url` and
  successfully processed, including a no-op re-import.

Error responses may reuse the existing `{code,message,data}` envelope because
no new internal error format is required by this slice.

Any additional request field must be approved before implementation; this
plan does not silently expand the contract.

## 5. Internal Trust Boundary

- `/internal/*` is not part of the public `/api/v1/*` contract.
- The Controller verifies the token before reading or parsing the JSON body.
- The token comes from an environment variable, not YAML and not source code.
- Missing or wrong token returns `20001` / HTTP 401.
- Client-supplied `user_id` is irrelevant and must not appear in the contract.
- No `users.role` column and no RBAC model are introduced.
- No session or Bearer token is required for this internal endpoint.

Proposed configuration:

```text
INTERNAL_API_TOKEN=<secret>
```

The runtime configuration layer should expose
`Config::internalApi.token`. `INTERNAL_API_TOKEN` must be included in
`Config::CurrentEnvironment()` and validation. It is a secret and must never
be logged.

Limitation:

- The current project has no Gateway. The internal token is an application
  layer protection only.
- Network isolation must be added later when the Gateway stage is implemented.

## 6. Schema and Idempotency Semantics

### Exact DDL Facts

`database/schema/03_job.sql` contains:

```sql
source_url VARCHAR(255) DEFAULT NULL,
UNIQUE KEY uk_source_url (source_url)
```

Therefore:

- `source_url` is nullable in the schema.
- MySQL permits multiple `NULL` values in the unique index.
- The unique key exists and is the concurrency-safe idempotency key for this
  ingestion path.
- The table uses `utf8mb4_general_ci`; uniqueness follows the database
  collation.
- URL canonicalization is out of scope. The importer must send the stable URL
  that is intended to identify the source record.

### Import Job Rules

- Every 0010 import item must provide a non-empty `source_url`.
- `source_url` must fit `VARCHAR(255)`.
- A new `source_url` creates one Job.
- Re-importing the same `source_url` updates the existing Job.
- Re-importing must not create a second Job.
- Requirements are replaced as a complete set on every successful import.
- The whole request batch is atomic.

### Manual Job Rules

- 0010 does not implement a public manual Job API.
- 0010 does not accept a missing or null `source_url`, even if `source` is
  `manual`.
- Future manual Jobs without `source_url` are a separate reviewed capability.
  They must not be added implicitly to this slice.

## 7. Requirements Semantics

Requirements use whole-set replacement:

```text
Existing: C++, MySQL, Redis
Imported: C++, PostgreSQL
Final:    C++, PostgreSQL
```

`MySQL` and `Redis` must not remain after a successful import.

The transaction shape is:

```text
BEGIN
  -> upsert Job
  -> delete old requirements for Job id
  -> insert new requirements
COMMIT
```

Any failure rolls back the complete request batch, including all Jobs and all
requirements processed before the failure.

An empty `requirements` array is valid and means "delete all requirements".

Duplicate skills within one Job item are invalid. The duplicate check should
follow the database's case-insensitive `utf8mb4_general_ci` semantics rather
than allowing an ambiguous database-level failure.

## 8. Data Validation

### Batch

- `jobs` must exist and be an array.
- `jobs.size() > 0`.
- `jobs.size() <= 100`.
- Duplicate `source_url` values within the same request are invalid.

### Required Fields

Required:

- `company`
- `title`
- `source`
- `source_url`

Optional:

- `location`
- `salary_min`
- `salary_max`
- `salary_currency`
- `description`
- `requirements`

If an optional string is present, it must be JSON string or `null` as allowed
by the schema. Empty strings should be rejected for `location` only if the
implementation chooses to normalize empty strings to `NULL`; this choice must
be documented in the final implementation.

### Length Limits

- `company`: non-empty, maximum 128 bytes.
- `title`: non-empty, maximum 128 bytes.
- `location`: optional, maximum 64 bytes.
- `salary_currency`: optional, non-empty when present, maximum 8 bytes.
- `source`: non-empty and one of `manual`, `import`, `crawler`.
- `source_url`: non-empty, maximum 255 bytes.
- `description`: optional, maximum 65535 bytes to fit `TEXT`.
- `requirements[].skill`: non-empty, maximum 64 bytes.

### Salary

- `salary_min` and `salary_max` are optional unsigned integers.
- When present, each value must be `>= 0`.
- When both are present, `salary_max >= salary_min`.
- `salary_currency` defaults to `CNY`.

### Requirement Level

The existing project skill-level convention is `1..3`:

- `1`: 了解
- `2`: 熟悉
- `3`: 精通

Job requirement levels must use the same range. Values outside `1..3` return
`10001`.

## 9. DAO Design

Add a write-only ingestion interface rather than expanding the existing
read-oriented `IJobDao`:

```cpp
enum class JobUpsertStatus
{
    kCreated,
    kUpdated
};

struct JobUpsertResult
{
    JobUpsertStatus status;
    std::uint64_t jobId = 0;
};

class IJobIngestionDao
{
public:
    virtual ~IJobIngestionDao() = default;

    virtual JobUpsertResult UpsertJob(
        jobpilot::core::ConnectionPool::Transaction &transaction,
        const JobIngestionRecord &job) = 0;

    virtual void ReplaceRequirements(
        jobpilot::core::ConnectionPool::Transaction &transaction,
        std::uint64_t jobId,
        const std::vector<JobRequirementInput> &requirements) = 0;
};
```

Rules:

- `Job Read DAO` remains read-only.
- The ingestion DAO owns only Job and requirement writes.
- All SQL is parameterized.
- All writes use the Transaction passed by Service.
- The DAO never creates an independent connection.
- The DAO does not parse HTTP, validate tokens, or validate batch size.
- `JobDao` may implement both read and ingestion interfaces only if the write
  implementation remains clearly separated.

## 10. Upsert Semantics

Recommended SQL shape:

```sql
INSERT INTO jobs (
    company,
    title,
    location,
    salary_min,
    salary_max,
    salary_currency,
    description,
    source,
    source_url,
    status
)
VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, 1)
ON DUPLICATE KEY UPDATE
    id = LAST_INSERT_ID(id),
    company = VALUES(company),
    title = VALUES(title),
    location = VALUES(location),
    salary_min = VALUES(salary_min),
    salary_max = VALUES(salary_max),
    salary_currency = VALUES(salary_currency),
    description = VALUES(description),
    source = VALUES(source),
    status = 1
```

Result classification:

- `affectedRows == 1`: new Job, result `kCreated`.
- `affectedRows == 0` or `2`: existing Job, result `kUpdated`.
- Any other affected-row value is an error.
- `LAST_INSERT_ID(id)` must make `lastInsertId` the existing Job id.
- `lastInsertId == 0` is always an error.

Important facts:

- The current MySQL connection is created without `CLIENT_FOUND_ROWS`.
- MySQL reports `0` when an upsert matches a row but does not change stored
  values.
- MySQL reports `2` when the matched row is changed.
- `update_time` uses `ON UPDATE CURRENT_TIMESTAMP`, so an unchanged re-import
  may leave `update_time` unchanged.
- The implementation must lock the observed affected-row behavior with a
  MySQL integration test; it must not assume behavior from another connector.
- MySQL `VALUES()` in `ON DUPLICATE KEY UPDATE` is deprecated in newer MySQL
  8.0 releases but is used by the existing project. Keep it for consistency
  in 0010 unless a separate reviewed compatibility change is approved.

Commit behavior:

- The DAO must not commit.
- Service commits only after every Job and requirements operation succeeds.
- If COMMIT itself fails, the service returns `60002`. The final database
  state may be uncertain; clients recover by idempotently re-importing the
  same batch.

## 11. Service Design

```cpp
ServiceResult<JobIngestionResult> JobIngestionService::Import(
    const std::vector<JobIngestionItem> &jobs);
```

Responsibilities:

- HTTP-independent.
- Validate the complete batch before opening the transaction or before the
  first write.
- Open one Transaction for the whole batch.
- Call `UpsertJob` for each item.
- Call `ReplaceRequirements` for each item.
- Aggregate `created`, `updated`, and `total`.
- Commit once.
- Map expected validation failures to `ServiceResult` errors.
- Map database exceptions to `60002`.

Non-responsibilities:

- HTTP parsing.
- Internal token validation.
- Router registration.
- SQL construction.
- Redis access.

Partial failure policy:

- The request is atomic.
- If job 87 of 100 fails, the entire transaction rolls back.
- No per-item partial success response is returned.

Recommended error mapping:

```text
empty batch / >100 / invalid field -> 10001
malformed JSON -> 10002
missing or wrong internal token -> 20001
unexpected database failure -> 60002
```

No new Job ingestion error code is required.

## 12. Controller and Router

Add:

```text
POST /internal/jobs/import
```

Controller responsibilities:

- Read `X-Internal-Token`.
- Compare it with the configured internal token before parsing JSON.
- Parse the JSON body.
- Translate JSON/body-shape failures to the existing error envelope.
- Pass a typed batch to `JobIngestionService`.
- Return the compact success response.

Router requirements:

- Register `/internal/jobs/import` as a static route.
- Do not add the route under `/api/v1/jobs`.
- Do not allow `/api/v1/jobs/{id}` to capture the internal route.
- Reuse existing HTTP method support; no P1 network change is allowed.

main wiring:

- Construct `JobIngestionDao`, `JobIngestionService`, and
  `InternalJobImportController`.
- Pass the Controller to `RegisterRoutes`.
- Keep the new pointer optional so existing tests can register only the
  controllers they need.

## 13. Error Model

Use existing codes:

| Condition | Code | HTTP |
|---|---:|---:|
| Missing/wrong internal token | `20001` | 401 |
| Malformed JSON | `10002` | 400 |
| Empty/too-large batch or invalid field | `10001` | 400 |
| Database failure or commit failure | `60002` | 500 |

No new code is required. `40001 job not found` is not relevant because this
is an upsert endpoint, not a Job lookup endpoint.

## 14. Testing Plan

### DAO

- New `source_url` creates one Job.
- Same `source_url` updates the existing Job and returns the same id.
- Identical re-import returns `kUpdated` without creating a second Job.
- Requirements are replaced as a set.
- Empty requirements deletes all old requirements.
- Duplicate source URL in one batch is rejected before SQL if this is checked
  in Service.
- Transaction rollback restores Job and requirements.
- Inputs with SQL metacharacters remain parameterized and safe.

### Service

- Empty batch.
- Batch larger than 100.
- Duplicate `source_url` in one batch.
- Invalid salary values.
- Invalid requirement level.
- Invalid source enum.
- Invalid/oversized fields.
- Atomic rollback when a later item fails.
- Correct `created`, `updated`, and `total` counters.
- DAO/database exception mapping.

### HTTP

- Missing token.
- Wrong token.
- Valid token.
- Malformed JSON.
- Invalid request fields.
- Valid batch import.
- Created/updated response.
- Repeated batch import returns `created=0`, `updated=N`.

### End-to-End Closure

```text
Internal import
  -> public Job Read search/detail
  -> Application creation using the imported Job
```

The end-to-end test should reuse the existing Job and Application fixtures
and cleanup helpers. Do not create a second Job fixture system.

## 15. Dataset

During implementation, add one minimum repeatable JSON dataset sample:

- 1 to 3 Jobs.
- Each Job has a unique `source_url`.
- Requirements cover at least two different skills.
- Re-importing the same dataset yields stable Job ids and no duplicate Jobs.

This planning task must not create the dataset file.

## 16. Implementation Steps

### Step 1: Contract and Schema Verification

- Goal: lock the internal contract, token boundary, exact `source_url`
  nullable/unique behavior, and affected-row semantics.
- Scope: `docs/04-api-design.md` review and test design.
- Dependency: this ExecPlan.
- Verification: targeted DDL inspection and a planned upsert behavior test.
- Risk: assuming MySQL affected-row behavior without integration evidence.

### Step 2: Job Ingestion Write DAO

- Goal: add write-only DAO interfaces and MySQL implementation.
- Scope: `service/include/jobpilot/service/job/`,
  `service/src/job/`.
- Dependency: Step 1.
- Verification: DAO integration tests.
- Risk: mixing read and write responsibilities in `IJobDao`.

### Step 3: JobIngestionService

- Goal: validate batches and own the single transaction boundary.
- Scope: new ingestion DTOs, Service interface, and implementation.
- Dependency: Step 2.
- Verification: fake DAO Service tests.
- Risk: partial commit or per-item implicit commits.

### Step 4: Internal Token Configuration

- Goal: load the internal secret from the environment and fail fast when
  missing at service startup.
- Scope: config struct, YAML/environment handling, config tests, `.env.example`.
- Dependency: none.
- Verification: valid token, missing token, environment override tests.
- Risk: accidentally logging or committing the secret.

### Step 5: InternalJobImportController

- Goal: authenticate, parse JSON, call Service, and return the compact result.
- Scope: new Controller and error mapping.
- Dependency: Steps 3 and 4.
- Verification: focused Controller/HTTP tests.
- Risk: wrong token handling before body parsing.

### Step 6: Router and main Wiring

- Goal: expose `/internal/jobs/import` with minimal wiring.
- Scope: `service/src/router.cpp`, `service/src/main.cpp`, CMake.
- Dependency: Step 5.
- Verification: HTTP route test.
- Risk: route shadowing or accidental public Job write API.

### Step 7: DAO Tests

- Goal: prove upsert, same-id update, requirements replacement, and rollback.
- Scope: Job ingestion DAO test target.
- Dependency: Step 2.
- Verification: MySQL integration tests.
- Risk: test data contamination; use existing cleanup helpers.

### Step 8: Service Tests

- Goal: prove batch validation, atomicity, and counters.
- Scope: Job ingestion Service test target.
- Dependency: Step 3.
- Verification: fake DAO tests.
- Risk: fake behavior diverging from MySQL affected-row behavior.

### Step 9: HTTP and Integration Tests

- Goal: prove token, request validation, import, Job Read, and Application
  closure.
- Scope: internal HTTP tests plus focused regression tests.
- Dependency: Steps 4-6.
- Verification: real HttpServer plus MySQL.
- Risk: exposing internal API without Gateway isolation.

### Step 10: Dataset Sample

- Goal: provide a small repeatable import sample.
- Scope: `dataset/`.
- Dependency: Steps 2-6.
- Verification: import twice and confirm stable counts.
- Risk: creating a test-data generator larger than needed.

### Step 11: L4 Verification

- Goal: verify the complete stage.
- Scope: no production change.
- Dependency: Steps 1-10.
- Verification: Debug CTest, ASan, UBSan, relevant TSan, service/root
  Release builds, `git diff --check`.
- Risk: over-repeating the full matrix during development.

### Step 12: Documentation and Archive

- Goal: record the delivered contract and archive the plan.
- Scope:
  - `docs/04-api-design.md`
  - `docs/ai/project-state.md`
  - `docs/quality/README.md`
  - move this plan to `docs/exec-plans/completed/`.
- Dependency: Step 11.
- Verification: docs diffs, `git diff --check`.
- Risk: leaving project-state stale.

## 17. Verification Strategy

### Incremental

- Job ingestion DAO tests.
- Job ingestion Service tests.
- Internal HTTP tests.
- Focused Job Read and Application regression tests.

### Final L4

- Debug CTest.
- ASan.
- UBSan.
- TSan only for relevant concurrent paths; if no new concurrency is
  introduced, record why it was not rerun.
- Service Release build.
- Root Release build.
- `git diff --check`.

Do not run the full matrix while writing or reviewing this ExecPlan.

## 18. Documentation Follow-up

At 0010 completion, update:

```text
docs/04-api-design.md
docs/ai/project-state.md
docs/quality/README.md
```

Known documentation drift to fix at that time:

```text
Actual:
HEAD == origin/main == a1074d5

Old project-state claims:
origin/main = 1b95602
0009 not committed
```

Do not modify `project-state.md` during the planning task.

Archive:

```text
docs/exec-plans/active/0010-p2-job-ingestion-vertical-slice.md
  -> docs/exec-plans/completed/0010-p2-job-ingestion-vertical-slice.md
```

## 19. Risks

| Priority | Risk | Mitigation |
|---|---|---|
| High | Internal token is not network isolation | Document limitation; Gateway stage adds isolation |
| High | Upsert affected-row semantics differ by connector flags | Lock behavior with MySQL integration tests |
| High | Requirements replacement commits partially | Single Transaction, rollback tests |
| Medium | Duplicate source URLs inside one batch | Reject before transaction |
| Medium | `source_url` collation differs from caller assumptions | Document exact-string key; no URL canonicalization |
| Medium | Batch causes excessive transaction/body load | Limit to 100 Jobs and retain 12 MiB body cap |
| Medium | Internal endpoint accidentally becomes public | Keep `/internal/*`, no public `/api/v1/jobs` write route |
| Medium | Secret leaks through logs/errors | Never log token; redact config output |
| Low | Empty requirements semantics misunderstood | Document and test delete-all behavior |
| Low | Job `update_time` not changing on identical re-import | Treat any matched source_url as updated; do not require timestamp change |

## 20. Stop Conditions

Stop and return to design if:

- The endpoint must accept `source_url = NULL`.
- Public users must write Jobs before Admin/RBAC is designed.
- Requirements must be merged instead of replaced.
- Partial-success batch semantics are required.
- The upsert result cannot be distinguished reliably without changing the
  MySQL connector contract.
- A schema migration is proposed.
- Crawler, MQ, AI, or Gateway functionality becomes part of the same slice.
- Gateway network isolation is required before 0010 can be considered safe.

## 21. Exit Criteria

0010 is complete only when:

- The internal import API is available.
- Internal token validation is effective.
- Batch size is constrained to 1..100.
- Job upsert is idempotent by `source_url`.
- Requirements are replaced as a complete set.
- Job and requirements writes are atomic.
- Rollback behavior is verified.
- `created`, `updated`, and `total` are correct.
- Public Job Read can see imported Jobs.
- Application can reference an imported Job.
- No public Job write permission is added.
- DAO, Service, and HTTP tests pass.
- L4 verification passes.
- Documentation is synchronized.
- This ExecPlan is archived to `completed/`.

## 22. Current Progress

- [x] Research
- [x] Capability Check
- [x] Design
- [x] ExecPlan
- [x] Contract and schema verification
- [x] Job ingestion write DAO
- [x] JobIngestionService
- [x] Internal token configuration
- [x] InternalJobImportController
- [x] Router and main wiring
- [x] DAO tests
- [x] Service tests
- [x] HTTP and integration tests
- [x] Dataset sample
- [x] L4 verification
- [x] Documentation and archive

## 24. Verification Results

- Focused Job tests: 24/24 passed.
- Debug full CTest: 108/108 passed.
- ASan full CTest: 108/108 passed.
- UBSan full CTest: 108/108 passed.
- TSan default gate: 42/42 passed.
- TSan external-service gate: 6/6 passed.
- Service Release build: passed.
- Root Release build: passed.
- `git diff --check`: passed.

The implementation is complete in the working tree. Commit and GitHub
synchronization remain separate delivery actions.

## 23. Rejected Alternatives

- **SELECT-then-INSERT deduplication**: race-prone under concurrent imports.
- **Extending `IJobDao` with all write methods**: broadens the read interface
  and forces unrelated fakes to implement ingestion behavior.
- **Public `/api/v1/jobs` write API now**: requires an Admin/RBAC model that
  does not exist in the schema.
- **Adding `users.role` in 0010**: schema and authorization scope expansion.
- **Per-item partial success**: produces ambiguous rollback and retry
  semantics for a batch importer.
- **Redis idempotency locks**: unnecessary because the database unique key is
  already authoritative.
- **Independent DAO connection**: would break batch atomicity.
- **Crawler/MQ integration now**: expands into a separate asynchronous
  subsystem.
- **URL canonicalization or fuzzy matching**: adds ambiguous identity rules
  without a product requirement.
