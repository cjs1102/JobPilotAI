# ExecPlan 0008: P2 Job Read Vertical Slice

> Status: Completed.

## Background

User, profile, skill, resume, authentication, transaction, storage, and
query-aware routing capabilities are complete and synchronized at
`1b95602`.

The next P2 slice should unlock downstream application and AI work without
introducing unnecessary infrastructure. `jobs` and `job_requirements` already
exist in the canonical schema.

## Goal

Deliver a public, read-only Job vertical slice:

- search active jobs with keyword, exact location, and pagination;
- return one active job with its skill requirements;
- use parameterized MySQL queries;
- expose stable response models through Controller -> Service -> DAO;
- avoid authentication, Redis, writes, and schema changes.

## Scope

- Add Job DAO, Service, Controller, and routes.
- Replace the current hard-coded `/api/v1/jobs/{id}` placeholder.
- Add `40001 job not found`.
- Add focused DAO, Service, Router, and HTTP tests.
- Add a minimal test fixture/seed helper for Job data.

## Non-Goals

- `POST`, `PUT`, or `DELETE` Job APIs.
- Job import, crawler, admin authorization, or moderation.
- Full-text search or external search infrastructure.
- Salary/location range filters beyond the locked API.
- Redis caching.
- Application, AI Task, notification, or job-matching work.
- Schema migration.
- New HTTP method or body capability.

## Existing Capabilities

- `GET` routing and query-aware path matching already exist.
- `ConnectionPool::Query` provides parameterized SQL.
- The current Router already contains a hard-coded
  `GET /api/v1/jobs/{id}` placeholder that must be replaced.
- `jobs` supports title/company/location/salary/description/source/status.
- `job_requirements` contains `(job_id, skill, level)`.
- `uk_job_skill(job_id, skill)` prevents duplicate requirements.
- Job indexes exist for company, title, location, and status.
- Existing Controller/Service/DAO patterns cover pagination and error mapping.
- GoogleTest and MySQL integration-test helpers are available.

## Capability Gaps

- No real Job DAO, Service, Controller, or response models.
- No public Job list route.
- The Job detail route is a placeholder and does not query MySQL.
- `40001 job not found` is not present in `ErrorCode`.
- No reusable Job test fixture/seed.
- No query-string decoding for keyword/location in the current service
  controllers.
- LIKE wildcard escaping is not defined.

## API Contract

Both endpoints are public and require no authentication.

### Search jobs

```text
GET /api/v1/jobs
  ?keyword=<title/company/description>
  &location=<exact location>
  &page=1
  &page_size=20
```

Rules:

- `page >= 1`.
- `1 <= page_size <= 100`.
- `status = 1` is mandatory.
- keyword is trimmed and limited to 128 bytes.
- location is trimmed and matched exactly.
- empty keyword or location removes that filter.
- sort by `update_time DESC, id DESC`.

### Job detail

```text
GET /api/v1/jobs/{id}
```

- only `status = 1` is publicly visible;
- missing or inactive job returns `40001 job not found`;
- requirements are returned in deterministic `skill ASC` order.

## Query Parameters

- `keyword`: optional; percent-decoded; trimmed; maximum 128 bytes.
- `location`: optional; percent-decoded; trimmed; exact match.
- `page`: optional positive integer, default 1.
- `page_size`: optional positive integer, default 20, maximum 100.
- duplicate parameters for the same key are invalid;
- malformed percent encoding or non-numeric values return `10001`.

Implement the minimal percent decoder in JobController. Do not add a general
query framework for this slice.

## Response Models

### JobSummary

- `id`
- `company`
- `title`
- `location`
- `salary_min`
- `salary_max`
- `salary_currency`
- `source`
- `created_at`
- `updated_at`

### JobRequirement

- `skill`
- `level`

### JobDetail

- all `JobSummary` fields;
- `description`;
- `requirements`: ordered array of `JobRequirement`;
- `source_url`;
- `status`.

Contract clarification: `requirements` is an array of `JobRequirement`
objects so the stored `level` is not discarded. The abbreviated string-array
example in `docs/04-api-design.md` must be clarified during implementation
documentation; no endpoint or field is added.

List response:

```json
{
  "code": 0,
  "message": "success",
  "data": {
    "list": [],
    "total": 0,
    "page": 1,
    "page_size": 20
  }
}
```

Empty search results return `200` with an empty list, not `40001`.

## DAO Design

Add `IJobDao` and `JobDao`.

Operations:

- `SearchJobs(keyword, location, limit, offset)`;
- `CountJobs(keyword, location)`;
- `FindActiveJobById(jobId)`;
- `ListRequirements(jobId)`.

All SQL is parameterized. Count and search must share the same filter-building
helper so their predicates cannot drift.

Search predicate:

```text
status = 1
[AND location = ?]
[AND (
  title LIKE ? ESCAPE '!'
  OR company LIKE ? ESCAPE '!'
  OR COALESCE(description, '') LIKE ? ESCAPE '!'
)]
```

LIKE binding:

- treat keyword as literal text, not a wildcard expression;
- escape `!` as `!!`;
- escape `%` as `!%`;
- escape `_` as `!_`;
- bind `%<escaped>%`;
- empty keyword adds no LIKE condition.

`FindActiveJobById` filters `id = ? AND status = 1`.

## Service Design

Add `JobService`.

Operations:

- `SearchJobs(keyword, location, page, pageSize)`;
- `GetJob(jobId)`.

Responsibilities:

- trim and validate keyword/location;
- enforce page/pageSize bounds;
- safely compute offset and reject multiplication overflow;
- call DAO and map missing/inactive detail to `kJobNotFound`;
- return `kDatabaseError` for query failures;
- expose no SQL or HTTP concerns.

Offset rule:

- reject if `(page - 1) > max_size / page_size`;
- otherwise `offset = (page - 1) * page_size`.

## Controller Design

Add `JobController`.

Responsibilities:

- parse and percent-decode query parameters;
- parse `{id}` with the existing Router path format;
- call `JobService`;
- serialize `JobSummary`, `JobRequirement`, and `JobDetail`;
- map `kJobNotFound` to HTTP 404;
- keep all SQL and business rules out of the Controller.

The Controller does not authenticate requests.

## Router Integration

Extend `RegisterRoutes` with an optional `JobController *`.

Register:

- `GET /api/v1/jobs`;
- `GET /api/v1/jobs/{id}`.

When JobController is provided, replace the existing hard-coded detail
placeholder. Do not add another duplicate detail route.

Existing static-route priority is sufficient because the list and detail
paths do not compete for the same exact path.

## Error Handling

- `10001 invalid parameter`: malformed query, duplicate parameter, invalid
  pagination, keyword too long;
- `40001 job not found`: missing or inactive detail;
- `60002 database error`: query failure;
- successful empty list: `200` with `list: []`.

Add `kJobNotFound = 40001` and message `job not found`.

## Pagination

- default `page = 1`;
- default `page_size = 20`;
- maximum `page_size = 100`;
- offset is calculated only after overflow validation;
- page beyond the final page returns an empty list with the real total;
- Count and Search use the same filters.

MySQL queries remain separate. Small count/list race windows are accepted for
this public read-only slice; no transaction or snapshot isolation is added.

## Search Semantics

- Keyword searches title, company, and description.
- Keyword and location are trimmed.
- Location uses exact equality.
- `%` and `_` search literally.
- Empty keyword/location remove filters.
- Inactive jobs never appear in list, count, or detail.
- No fuzzy ranking, relevance score, or full-text index is added.

## Sorting

Deterministic ordering:

```sql
ORDER BY update_time DESC, id DESC
```

Requirements:

```sql
ORDER BY skill ASC
```

## Job Requirements Loading

Detail uses two bounded queries:

1. `FindActiveJobById(id)`;
2. `ListRequirements(jobId)`.

This is not N+1:

- only one job is requested;
- exactly one requirements query is executed;
- list responses do not load requirements.

A JOIN with row aggregation is rejected as unnecessary complexity for one
detail record.

## Test Strategy

### DAO tests

- empty result;
- one and multiple jobs;
- keyword matches title, company, and description;
- literal `%` and `_` behavior;
- empty keyword/location;
- exact location filtering;
- keyword + location;
- sorting by update time and id;
- inactive jobs excluded;
- active detail found;
- inactive/missing detail absent;
- requirements returned in skill order.

### Service tests

- page/pageSize validation and offset overflow;
- empty keyword/location normalization;
- result metadata and empty-list behavior;
- missing/inactive job maps to `kJobNotFound`;
- DAO failure maps to `kDatabaseError`.

### Controller/HTTP tests

- public empty list;
- seeded list with pagination;
- keyword and location query decoding;
- invalid pagination;
- inactive jobs absent;
- active detail shape and requirements;
- inactive/missing detail returns 404 `40001`;
- no authentication required.

### SQL safety tests

- keyword containing `%` and `_` behaves literally;
- SQL injection-like input remains a bound parameter and is not interpreted.

## Fixture / Seed Strategy

No reusable Job seed exists. `database/schema/` contains DDL only, and Docker
initialization runs only when the MySQL volume is first created.

Use a minimal test fixture in `service/tests/test_job_support.h`:

- insert active/inactive jobs with unique company/title/source_url values;
- insert requirements for selected jobs;
- clean up jobs and requirements by tracked job ids or unique source URLs;
- never depend on fixed IDs.

Create a separate production/demo seed only if a later deployment task
requires it. Do not add a seed-generation system to 0008.

## Implementation Order

1. Add Job DTOs, `IJobDao`, and `JobDao`.
2. Add Job DAO integration tests and fixture helper.
3. Add `JobService` validation/pagination and Service tests.
4. Add `JobController`, error mapping, and query decoding.
5. Replace the Router placeholder and wire JobService in `main`.
6. Add HTTP integration tests.
7. Run L4 verification and update project state.

## Verification Level

- DAO/Service work: L1/L2 focused tests.
- HTTP/Controller integration: focused HTTP tests.
- Router integration: existing Router regression plus Job HTTP tests.
- Stage completion: L4 full Debug CTest, full ASan, full UBSan.
- TSan is not required unless implementation introduces concurrency or
  cross-thread behavior.

## Risks

| Priority | Risk | Mitigation |
|---|---|---|
| Medium | LIKE wildcard injection | Escape `!`, `%`, `_` and bind as parameter |
| Medium | Count/list predicate drift | Shared filter builder in DAO |
| Medium | Offset overflow | Validate before multiplication |
| Medium | Placeholder route remains | Replace it in Router integration tests |
| Medium | Inactive detail leaks existence | Return `40001` for inactive and missing |
| Medium | Duplicate parameter ambiguity | Reject duplicate keys |
| Low | LIKE index limitations | Accept for MVP; no premature full-text search |
| Low | Count/list race window | Accept for public read; no transaction |

## Stop Conditions

Stop and return to design if:

- Job write/admin behavior becomes required;
- public detail must expose inactive jobs;
- full-text or ranking semantics are required;
- keyword must match salary/source or additional fields;
- schema changes are required;
- Redis caching becomes required;
- query decoding requires a general URL/query framework;
- pagination must provide stable snapshot consistency across Count and List.

## Current Progress

- [x] Research
- [x] Capability Check
- [x] Design
- [x] ExecPlan
- [x] Job DAO
- [x] Job Service
- [x] Job Controller and Router
- [x] Job tests and fixtures
- [x] L4 verification

## Verification Results

- Job DAO tests: 3/3 passed.
- Job Service tests: 5/5 passed.
- Job HTTP tests: 3/3 passed.
- Shared User/Resume/Auth/Router regression: 11/11 passed.
- Debug full CTest: 87/87 passed.
- ASan full CTest: 87/87 passed.
- UBSan full CTest: 87/87 passed.
- Service and root Release builds passed.
- TSan was not rerun because 0008 introduced no concurrency or cross-thread
  behavior.
