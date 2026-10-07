# JobPilotAI Open-source MVP Architecture

> Status: Accepted architecture baseline after 0010.
>
> This document records the architecture direction after reviewing comparable
> open-source job-search projects. It does not implement a new slice.

## 1. Positioning

JobPilotAI is primarily an open-source engineering showcase and a
self-hostable personal job-search tool. It is not designed as a commercial SaaS
platform.

The project should demonstrate:

- C++17 backend engineering: epoll/Reactor, HTTP, MySQL, Redis, transactions,
  search, ranking, and asynchronous task orchestration.
- AI engineering boundaries: deterministic logic first, provider-agnostic LLM
  integration second.
- Multi-source job ingestion: ATS APIs and structured feeds normalized through
  one ingestion contract.
- Open-source usability: clone, configure, start, import/discover jobs, search,
  match, tailor a resume, and track an application locally.

SaaS capabilities such as billing, platform-funded model usage, multi-tenant
administration, RBAC, and fully autonomous application submission are not MVP
requirements.

## 2. Final Architecture Direction

```text
Job Sources
  -> Source Adapters
  -> Normalize / Deduplicate
  -> Job Ingestion API
  -> MySQL Job Store
  -> Job Search / Ranking
  -> Resume / Profile
  -> AI Matching
  -> Resume Tailoring
  -> Application Tracking
```

### Runtime Responsibilities

| Component | Responsibility |
|---|---|
| C++ Backend | Public API, auth, ingestion, job store, search/ranking, application tracking, AI task orchestration |
| Source Adapters | Fetch public ATS/feed data and publish normalized jobs through the internal ingestion contract |
| Python AI Worker | Rule scoring, optional embeddings, LLM calls, structured result generation |
| AI Provider | User-selected OpenAI-compatible API, DeepSeek, Qwen, or Ollama |
| MySQL | Canonical persisted state |
| Redis | Sessions and narrowly justified caches |

Source adapters and AI workers must not write business tables directly.

## 3. Discovery, Search, and Recommendation

These are separate concerns:

- **Discovery** finds postings from ATS APIs, feeds, company career pages, or
  search-engine discovery.
- **Search** filters stored postings by title, company, location, salary,
  skills, experience, work mode, and employment type.
- **Recommendation** ranks stored postings against the user's profile, skills,
  resume, and preferences.
- **Crawler** is only one possible discovery implementation. It is not the
  architecture itself.

The MVP should implement source adapters rather than a general crawler.

Priority:

1. Greenhouse, Lever, Ashby public ATS APIs.
2. JSON/CSV/manual import.
3. RSS or structured feeds.
4. Search-engine discovery.
5. Browser/Playwright fallback only when legally and technically justified.

## 4. AI Pipeline

```text
Hard Filter
  -> Rule Score
  -> Embedding Similarity (optional)
  -> LLM Explanation / Judgment (optional)
```

The deterministic stages remain useful without an API key:

- Hard filters reject obvious mismatches.
- Rule score compares required skills, profile preferences, location, salary,
  and experience.
- Embedding adds semantic similarity when the local model is available.
- LLM adds explanation, nuanced judgment, resume suggestions, and prose.

LLM output must validate against a schema and cite evidence from the resume or
posting. Missing evidence remains a gap.

## 5. Provider Model

JobPilotAI uses Bring Your Own Key / Bring Your Own Model.

Supported provider shapes:

- OpenAI-compatible APIs.
- DeepSeek.
- Qwen.
- Ollama and local OpenAI-compatible servers.

Rules:

- No vendor is hardcoded into business logic.
- Keys are supplied through environment variables or a local secret store,
  never committed.
- `.env.example` documents variable names without secrets.
- Core search, ranking, ingestion, and application tracking work without an AI
  provider.
- The UI/API must explain when AI is disabled or degraded.

## 6. User Flow

```text
git clone
  -> configure MySQL/Redis and optional AI provider
  -> start locally
  -> import dataset or run ATS source adapters
  -> search and rank jobs
  -> select a resume
  -> run rule/embedding/LLM matching
  -> review tailored resume suggestions / cover letter
  -> manually submit
  -> track application status
```

The human always performs the final submission.

## 7. Scope Classification

| Capability | Classification |
|---|---|
| C++ network/backend, MySQL, Redis, transactions | Must Have |
| User/auth, profile/skills, resume versions | Must Have |
| Job Read and Job Ingestion | Must Have |
| Greenhouse/Lever/Ashby source adapters | Must Have |
| Deterministic search and ranking | Must Have |
| Rule + optional embedding/LLM matching | Must Have |
| Resume tailoring with provenance/fact gate | Good to Have |
| Minimal local dashboard | Good to Have |
| In-app notifications | Good to Have |
| Interview preparation | Good to Have |
| Gateway/Nginx | Future Work |
| RabbitMQ | Future Work |
| General-purpose crawler / browser fallback | Future Work |
| Billing, multi-tenant SaaS, RBAC | Future Work |
| AutoApply | Do Not Do |

## 8. Implementation Boundaries

### 0011 Job Search / Ranking

- MySQL only.
- Hard Filter and Soft Ranking are separate concepts.
- No LLM, embedding, freshness lifecycle, search engine, or MQ.
- Ranking must be deterministic, explainable, testable, and versioned.

Hard Filter answers inclusion:

```text
status = 1
location / salary / skill hard constraints
```

Soft Ranking scores only jobs that passed the hard filter.

An attribute must not be both a hard exclusion and a repeated score penalty.
For example, if a skill is mandatory, missing it excludes the job; it must not
then reduce the score of an already excluded job.

### 0012 Job Source Adapter + Freshness

- Manual Greenhouse / Lever / Ashby board configuration.
- Adapter calls the dedicated observation API
  `POST /internal/jobs/source-sync`; `POST /internal/jobs/import` stays the
  manual/dataset path. No adapter writes MySQL directly.
- Complete observation and source status are introduced here.
- `/internal/jobs/source-sync` batch limits: `0..200` when `complete=true`
  (`0` is an empty board), `1..200` when `complete=false`.
  `/internal/jobs/import` keeps its `1..100` manual-batch limit.
- `source-sync` is the only path that may update `last_seen_at`, missing
  counters, `closed_at`, or reopen a job.
- `last_seen_at`, disappearance, stale, and closed semantics are introduced
  here.
- A single missing fetch never closes a job.
- Current `source_url UNIQUE` remains unchanged.

### 0013 AI Matching

- C++ owns AI Task state and internal API boundaries.
- Python Worker claims tasks and performs optional AI stages.
- Atomic claim, lease, timeout, retry, and stale recovery are required.
- Provider abstraction supports OpenAI-compatible APIs, DeepSeek, Qwen, and
  Ollama.
- Rule-only fallback must work without a provider key.
- LLM is an enhancement, not a prerequisite.

## 9. Why Not SaaS First

- Job-search data is personal and local-first is a stronger trust model.
- ATS and provider integrations are unstable and better isolated behind
  adapters than operated as a multi-tenant platform.
- Platform-funded LLM usage creates cost, abuse, compliance, and rate-limit
  problems unrelated to the project's engineering story.
- A small, complete, runnable project demonstrates more engineering depth than
  a broad unfinished SaaS surface.

## 10. Roadmap

```text
0011 Job Search / Ranking
0012 Job Source Adapter + Freshness
0013 AI Matching
0014 Resume Tailoring
0015 Open-source Packaging / Demo / CI
```

Order rationale:

1. 0011 establishes deterministic C++ search/ranking on the existing data.
2. 0012 adds real jobs and the freshness evidence needed for trustworthy
   search.
3. 0013 adds semantic/LLM matching on top of a stable job lifecycle.
4. 0014 tailors documents after matching has an explainable basis.
5. 0015 packages the complete end-to-end open-source experience.

Detailed lifecycle semantics are in
[ADR-0010](../adr/0010-job-freshness-and-lifecycle.md).
