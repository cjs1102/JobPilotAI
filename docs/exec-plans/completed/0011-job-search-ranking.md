# ExecPlan 0011: Job Search / Ranking

> Status: Completed and verified on 2026-10-08.
>
> Task level: L3.

## 1. Objective

Deliver a deterministic, explainable, MySQL-backed Job Search and Ranking
slice:

```text
Hard Filter
  -> MySQL candidate query
  -> Rule Ranking
  -> Stable pagination
  -> Score explanation
```

This slice improves the existing public Job Read capability and adds one
authenticated personalized ranking endpoint.

## 2. Background

Current Job Read supports:

```text
GET /api/v1/jobs?keyword=&location=&page=&page_size=
GET /api/v1/jobs/{id}
```

Current limitations:

- No skill or salary filters.
- No deterministic relevance ranking.
- No profile-aware recommendation endpoint.
- Existing sort is `update_time DESC, id DESC`.
- Job data is present but freshness lifecycle is intentionally deferred to
  0012.

Current architecture constraints:

- Open-source first and self-hosted.
- MySQL only for 0011.
- Hard Filter and Soft Ranking must remain separate.
- No LLM, Embedding, Search Engine, RabbitMQ, or freshness lifecycle.
- Ranking must be explainable, testable, and versioned.

## 3. Scope

### In Scope

- Public hard-filter extensions on `GET /api/v1/jobs`.
- Keyword, location, salary range, and skill filtering.
- Shared filter predicate for Count and List.
- Authenticated `GET /api/v1/jobs/recommended`.
- Deterministic `rule_v1` ranking.
- Score breakdown and reason data.
- Stable ordering and pagination.
- Candidate-count safety cap.
- DAO, Service, HTTP, and ranking determinism tests.
- Minimal API documentation updates.
- Necessary CMake wiring.

### Out of Scope

- Freshness lifecycle, `last_seen_at`, `closed_at`, or stale filtering.
- LLM or Embedding.
- AI Task.
- Search engine such as Meilisearch or Elasticsearch.
- Redis ranking cache.
- Saved searches.
- Work mode, employment type, experience, or remote flags.
- User feedback learning.
- Vector search.
- Schema migration.
- RabbitMQ.
- Frontend.

## 4. API Contract

### Public Search Extension

```text
GET /api/v1/jobs
  ?keyword=<title/company/description>
  &location=<exact location>
  &salary_min=<user acceptable minimum>
  &salary_max=<user acceptable maximum>
  &skill=<required skill>
  &page=1
  &page_size=20
```

Rules:

- Endpoint remains public.
- Unknown or duplicate parameters are rejected with `10001`.
- Keyword is trimmed and limited to 128 bytes.
- Location is trimmed and limited to 64 bytes.
- Skill is trimmed and limited to 64 bytes.
- Salary values are non-negative and `salary_max >= salary_min`.
- Public results use deterministic SQL ordering:

```text
create_time DESC, id DESC
```

This gives a stable first-seen order without pretending it is freshness.

### Authenticated Recommendation

```text
GET /api/v1/jobs/recommended
Authorization: Bearer <token>
```

Same hard-filter query parameters as public search.

Response:

```json
{
  "code": 0,
  "message": "success",
  "data": {
    "list": [
      {
        "id": 20001,
        "title": "C++ Backend Engineer",
        "company": "Example",
        "score": 82.5,
        "score_version": "rule_v1",
        "score_breakdown": {
          "skill": 0.91,
          "title": 0.75,
          "salary": 1.0,
          "location": 1.0,
          "first_seen": 0.80
        },
        "matched_skills": ["C++", "Linux"],
        "missing_skills": ["Kubernetes"]
      }
    ],
    "total": 120,
    "ranked_total": 120,
    "truncated": false,
    "page": 1,
    "page_size": 20
  }
}
```

`/api/v1/jobs/recommended` must be a static route registered before the
generic `{id}` route.

## 5. Hard Filter Semantics

Hard Filter answers only:

```text
Is this Job allowed into the result set?
```

It does not compute or modify score.

Base predicate:

```text
jobs.status = 1
```

Keyword:

```text
title LIKE %keyword%
OR company LIKE %keyword%
OR description LIKE %keyword%
```

LIKE wildcard escaping follows the existing Job Read implementation.

Location:

```text
jobs.location = requested_location
```

Skill:

```text
EXISTS (
  SELECT 1
  FROM job_requirements
  WHERE job_id = jobs.id
    AND skill = requested_skill
)
```

Skill comparison follows the database collation.

Salary:

```text
job.salary_max >= requested_salary_min
AND job.salary_min <= requested_salary_max
```

When either salary filter is present, Jobs without a salary range are
excluded because overlap cannot be proven. When only one bound is supplied,
only the corresponding overlap condition is required.

The exact same predicate builder must be used by Count and List.

## 6. Soft Ranking Semantics

Soft Ranking answers:

```text
Among Jobs that passed Hard Filter, which are more relevant?
```

Hard-filtered attributes are removed from the score and weights are
renormalized. A condition must never both exclude a Job and penalize it.

### rule_v1 Formula

```text
score = 100 * weighted_average(
  skill_fit      0.55,
  title_fit      0.15,
  salary_fit     0.15,
  location_fit   0.10,
  first_seen_fit 0.05
)
```

Components:

### skill_fit

```text
sum(required_level * min(user_level, required_level))
---------------------------------------------------
          sum(required_level)
```

Only matched user skills contribute.

If a Job has no requirements:

- remove `skill_fit`;
- renormalize remaining weights.

### title_fit

Deterministic token overlap between `profiles.target_position` and Job title:

```text
matched_title_tokens / target_position_tokens
```

Normalization is ASCII case folding and whitespace tokenization. No
stemming, synonym list, or LLM.

### salary_fit

Range overlap ratio between profile salary range and Job salary range.

If either range is unavailable:

- remove this component;
- renormalize.

### location_fit

- `1.0` exact normalized location match.
- Component removed when profile location or Job location is unavailable.

### first_seen_fit

Uses `jobs.create_time` as the first-seen proxy:

```text
max(0, 1 - age_days / 30)
```

This is not a freshness lifecycle. It only provides a small deterministic
newness boost.

### Stable Sort

```text
score DESC
create_time DESC
id DESC
```

### Score Versioning

- Formula changes require a new version such as `rule_v2`.
- Version is returned in every ranked item.
- Tests pin component calculations and ordering.

## 7. Candidate Loading and Pagination

Recommended endpoint flow:

```text
load profile and skills
-> Count hard-filtered Jobs
-> load up to kMaxRankingCandidates with minimal fields
-> bulk load requirements for candidates
-> score in C++
-> stable sort
-> in-memory page slice
```

Recommended cap:

```text
kMaxRankingCandidates = 2000
```

Rules:

- `total` is the true hard-filtered count.
- `ranked_total` is the number of candidates actually scored.
- `truncated=true` when `total > ranked_total`.
- Page results beyond the ranked candidate set may be empty.
- Candidate ordering before the cap is `create_time DESC, id DESC`.

This cap prevents an unbounded in-memory ranking operation while keeping the
open-source MVP simple. A future slice may move scoring into SQL or a cache.

## 8. DAO Design

Add a narrow ranking read interface rather than expanding `IJobDao`, because
existing fakes should not be forced to implement ranking methods:

```cpp
class IJobRankingDao
{
public:
    virtual std::vector<JobRankingRecord> SearchRankingCandidates(
        const JobSearchFilter &filter,
        std::size_t limit) = 0;

    virtual std::size_t CountRankingCandidates(
        const JobSearchFilter &filter) = 0;

    virtual std::vector<JobRequirement> ListRequirementsForJobs(
        const std::vector<std::uint64_t> &jobIds) = 0;
};
```

`JobDao` may implement both the existing read interface and this ranking
interface.

The shared hard-filter predicate builder must be reused by:

- `IJobDao::SearchJobs`;
- `IJobDao::CountJobs`;
- `IJobRankingDao::SearchRankingCandidates`;
- `IJobRankingDao::CountRankingCandidates`.

All SQL remains parameterized.

## 9. Service Design

### Public Search

`JobService` remains responsible for:

- query validation;
- shared hard-filter conversion;
- SQL-backed pagination for `GET /api/v1/jobs`.

### Ranking

Add `JobRankingService`:

```cpp
ServiceResult<RankedJobPage> RankJobs(
    std::uint64_t userId,
    const JobSearchFilter &filter,
    std::size_t page,
    std::size_t pageSize);
```

Responsibilities:

- authenticate through `ISessionAuthenticator`;
- load profile and skills through a narrow user-profile reader;
- load candidates plus requirements without N+1 queries;
- calculate `rule_v1`;
- produce stable pagination;
- return breakdown and match/missing skills.

If the user has no usable profile or skills:

```text
return 10001 invalid parameter
```

The public search endpoint remains available for browsing.

## 10. Controller / Router

`JobController` adds:

```cpp
void Recommended(
    const HttpRequest &request,
    HttpResponse &response);
```

Responsibilities:

- parse Bearer token;
- parse query parameters;
- call `JobRankingService`;
- serialize rank breakdown.

Router:

```text
GET /api/v1/jobs          -> Search
GET /api/v1/jobs/recommended -> Recommended
GET /api/v1/jobs/{id}     -> Detail
```

Static route must be registered before `{id}`.

## 11. Testing Plan

### DAO

- Keyword, location, salary, and skill hard filters.
- Count/List predicate consistency.
- Salary null behavior.
- LIKE wildcard escaping.
- Candidate cap and deterministic candidate order.
- Bulk requirement loading.
- Parameterized SQL.

### Ranking Service

- `rule_v1` component calculations.
- Hard-filtered component removal and weight renormalization.
- Stable ordering with tied scores.
- Score version.
- Breakdown values.
- Matched and missing skills.
- Missing profile/skills.
- Candidate truncation metadata.
- No double penalty: filtered location/salary/skill does not influence score.

### HTTP

- Public filters and pagination.
- Invalid/duplicate queries.
- Missing and invalid token for recommended endpoint.
- Empty recommendation result.
- Score and breakdown response.
- Route ordering with `/recommended`.
- User isolation through Session-derived `user_id`.

## 12. Implementation Steps

### Step 1: Shared JobSearchFilter

- Goal: centralize hard-filter validation and SQL predicate construction.
- Scope: Job DAO types and filter builder.
- Dependency: none.
- Verification: existing Job DAO tests.
- Risk: Count/List predicate drift.

### Step 2: Public Hard Filters

- Goal: add skill and salary filters to existing Job search.
- Scope: DAO, Service, Controller, HTTP tests.
- Dependency: Step 1.
- Verification: focused Job DAO/Service/HTTP tests.
- Risk: changing existing keyword/location behavior.

### Step 3: Ranking DAO Ports

- Goal: load candidates and requirements without N+1 queries.
- Scope: `IJobRankingDao` and `JobDao` implementation.
- Dependency: Step 1.
- Verification: DAO integration tests.
- Risk: IN-list chunking or candidate cap bugs.

### Step 4: JobRankingService

- Goal: implement `rule_v1` and stable pagination.
- Scope: ranking Service and profile reader.
- Dependency: Steps 1-3.
- Verification: fake-port Service tests.
- Risk: hard/soft component double counting.

### Step 5: Recommended API

- Goal: expose authenticated `GET /api/v1/jobs/recommended`.
- Scope: Controller, Router, main wiring.
- Dependency: Step 4.
- Verification: HTTP/integration tests.
- Risk: route ordering and authentication.

### Step 6: Documentation

- Goal: document filter and ranking semantics.
- Scope: `docs/04-api-design.md`.
- Dependency: Steps 2 and 5.
- Verification: contract review.
- Risk: documenting behavior not observed.

### Step 7: L4 Verification

- Goal: verify the complete stage.
- Dependency: Steps 1-6.
- Verification:
  - Debug full CTest;
  - ASan full CTest;
  - UBSan full CTest;
  - TSan only if concurrency is introduced;
  - service/root Release builds;
  - `git diff --check`.

## 13. Risks

| Priority | Risk | Mitigation |
|---|---|---|
| High | Hard Filter and score double-penalize the same attribute | Remove filtered components and renormalize |
| High | Count and List diverge | Shared predicate builder and consistency tests |
| Medium | Ranking candidate set is unbounded | Cap at 2000 and return truncation metadata |
| Medium | SQL score duplication | Score in C++ with bulk-loaded requirements |
| Medium | N+1 requirements query | Bulk load by candidate IDs |
| Medium | Title matching becomes arbitrary | Keep token overlap deterministic and versioned |
| Medium | Existing public search regressions | Run focused Job regression tests |
| Low | Salaries absent on imported jobs | Exclude only when salary filter is requested |

## 14. Stop Conditions

Stop and return to design if:

- freshness is required for 0011;
- work mode or employment type requires schema migration;
- ranking must use LLM or embeddings;
- ranking must be exact across an unbounded candidate set;
- existing public search cannot share the filter predicate;
- the API contract would require changing Job IDs or existing response shape.

## 15. Current Progress

- [x] Research
- [x] Capability Check
- [x] Design
- [x] ExecPlan
- [x] Shared JobSearchFilter
- [x] Public hard filters
- [x] Ranking DAO ports
- [x] JobRankingService
- [x] Recommended API
- [x] Documentation
- [x] L4 verification

## 17. Verification Results

- Focused Job DAO/Service/Ranking/HTTP tests: 20/20 passed.
- Debug full CTest: 115/115 passed.
- ASan full CTest: 115/115 passed.
- UBSan full CTest: 115/115 passed.
- Service Release build: passed.
- Root Release build: passed.
- TSan was not rerun because 0011 introduced no concurrency-sensitive path.
- `git diff --check`: passed.

The implementation is complete in the working tree. Commit and GitHub
synchronization remain separate delivery actions.

## 16. Rejected Alternatives

- **Search engine now**: current scale does not justify Meilisearch/ES.
- **Freshness in 0011**: lifecycle belongs to 0012 with complete source
  observation.
- **LLM ranking**: violates deterministic-first and adds cost/non-determinism.
- **Sort all candidates in memory without a cap**: unbounded memory risk.
- **Correlated requirement queries per Job**: N+1 pattern.
- **Use update_time as freshness**: it is a metadata-change timestamp.
- **Double-penalize filtered attributes**: ambiguous and hard to explain.
