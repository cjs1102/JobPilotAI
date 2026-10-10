# ExecPlan 0015: Resume Tailoring + Candidate Data + Explainable Matching

> **Stage number: `0015` — as fixed by the recorded stage table (2026-10-08).**
> `0015` = resume tailoring (Chinese + ATS), `0016` = open-source packaging. The
> `0015A` sub-number stays exactly as it is
> (`active/0015A-job-list-usability-and-intake-quality.md`); **no renumbering, no
> archived-plan edits** (§16).
>
> Status: **Active — planning only.** This document is research + design. **No
> production code, test, schema, configuration or commit is part of this task.**
>
> Task level: **L4 at stage close** (touches `profiles` + one new artifact table
> + the public `docs/04` contract + a candidate-data path that changes matching
> inputs). Implementation slices are **L1/L2**; the stage runs **one** L4.
>
> Verification rules: `docs/ai/agent-workflow.md` (§2 levels, §2.2 L4
> preconditions, §8 execution budget). Testing levels: `docs/quality/README.md`.
>
> Dependencies (all **reused, not redefined**): ADR-0009 + its 0013 amendment
> (deterministic-first AI), ADR-0010 (AI never decides lifecycle facts), ADR-0011
> (China-first, never auto-apply), ExecPlan 0013 (`match_rule_v1`,
> `ai_tasks.rule_result`) and ExecPlan 0015A (list `source_url`, intake quality
> gate, `review_status` visibility, education/job_nature/deadline query layer).
>
> Evidence base (reused, **not re-derived**): this stage adds **no** new
> competitive research. It reuses
> `docs/research/2026-10-08-open-source-job-search-agents.md` §4 (the recorded
> 0014/0015 tailoring requirements, L180-198), the **owner's verbatim roadmap
> goal** recorded in `active/0015A-...md` §17 (L746-756), `docs/ai/project-state.md`
> (Completed Plan 0014 + Next Entry, L89-140, L282-318) and the 0015A plan's
> Contracts/Testing structure (its §5, §8, §9). Only the specific `file:line`
> facts cited below were confirmed against the current tree at HEAD `bd7d451`.

## 1. Objective

Deliver the stage the stage table already promised — **简历定制 (Chinese,
ATS-aware resume tailoring)** — on top of the two capabilities it depends on:

```text
Phase A  candidate data completeness        profile education / graduation year /
                                            campus status + a PDF text contract
Phase B  explainable matching               rule_v1 as the single authoritative
                                            public ranking, deal-breaker + deadline
                                            semantics, score+reasons+skills in the
                                            list payload (no second call)
Phase C  read-only report surface           self-contained, read-only, shows score /
                                            reasons / gaps / sort order / original
                                            posting link — access decision, NO
                                            full frontend application
Phase D  THE DELIVERABLE                    per-job tailored Chinese resume
                                            (+ cover letter only if in scope),
                                            artifact tied to a resume version and
                                            an application, human confirms before
                                            any use or submission
```

The stage is complete when a reviewer can: fill the new candidate fields, see them
change a deterministic match, open a read-only report that explains the score and
links the original posting, generate a tailored Chinese resume for one job, read
the mechanical fact-provenance check that every generated claim maps to a source
fact, and confirm that **nothing was submitted anywhere** and that **no AI output
changed a public score**.

## 2. Background and Current State (file-level evidence from the real tree)

Verified at HEAD `bd7d451` (branch `main`, working tree clean at planning time).

### 2.1 What already exists and is reused (never re-derived)

| Capability | Actual state (evidence) |
|---|---|
| Public list + recommended | `GET /api/v1/jobs` and `GET /api/v1/jobs/recommended` are routed (`router.cpp:212`, `:224`) and documented (`docs/04:281` §5.1, `docs/04:440` §5.3) |
| List rows carry the original posting link | `JobSummary::sourceUrl` (`job_dao_interface.h:37-38`); **0015A delivered** the list projection and both writers (`job_controller.cpp:698` list / `:862` detail) |
| Deterministic public ranking | `rule_v1` is the only ranking: `RankedJob{score, scoreVersion, breakdown, matchedSkills, missingSkills}` (`job_ranking.h:34-42`), weights exported (`job_ranking.h:27-32`), documented (`docs/04:486`) |
| Recommended payload already explains | `score`, `score_version`, `score_breakdown`, `matched_skills`, `missing_skills` are written by the recommended writer (`job_controller.cpp:953-974`), matching `docs/04:459-469` |
| The match_result contract the 0015 payload should reuse | `MatchResult{scoreVersion, score, source, breakdown, matchedSkills, missingSkills, strengths, gaps, resumeEvidence}` (`match_result.h:33-44`) with evidence tags (`match_rule.h:17-19`) and a dedicated serializer (`match_result_json.cpp`) |
| Deterministic match scoring exists and is persisted | `POST /api/v1/ai/match` → `ai_tasks.rule_result` (`docs/04:886` §7.1, `docs/04:952` §7.4) |
| Candidate inputs available to matching | `UserProfile{targetPosition, experience, location, salaryMin/Max, summary}` (`user_dao_interface.h:23-34`), `UserSkill` (`:36-43`), `UserProfile` JSON writer (`user_controller.cpp:291-310`), profile routes `GET/PUT /api/v1/users/profile` (`router.cpp:79`, `:91`), update handler (`user_controller.cpp:579-660`) |
| 0014 job fields are queryable | `education_required`, `job_nature`, `deadline` in the filter (`job_search_filter.h:37-42`), DB columns (`database/schema/03_job.sql:23-28`), `idx_deadline` (`03_job.sql:41`), **deliberately no `idx_job_nature_education`** (`docs/03:452`) |
| Intake quality gate + review queue | `review_status` (`03_job.sql:29`), `job_review_items` (`:58-66`), `idx_review_status` (`:40`), `test_job_quality`/`test_job_review_http` targets (`service/CMakeLists.txt:253-271`), contract `docs/04:736` §5.6 |
| The artifact's owner + link target | `resumes`/`resume_versions` (`database/schema/02_resume.sql:3-21`), `applications.resume_version_id` (`04_application.sql:7`), `ResumeVersionRecord{id, resumeId, version, filePath, content}` (`resume_dao_interface.h:23-31`), `OwnedVersionExists` (`:71-74`) |
| File storage for an artifact | `FileStorage` (`resume_service.cpp:11`, `WriteTemp`/`Promote` at `:169`, `:206`), `storage.root` / `max_file_bytes` = 10 MiB (`service/config/config.yaml:23-25`) |
| Worker shape for an optional AI stage | `ai_worker/jobpilot_ai/{client,worker,merge,provider}.py`; default task type is `job_match` only (`ai_worker/jobpilot_ai/config.py:27`) |

### 2.2 What does **not** exist (this stage's real gaps — do not describe these as wired)

| Gap | Evidence |
|---|---|
| `profiles` has **no** education / graduation-year / campus (应届 or 在校) column | `database/schema/01_user.sql:19-32` (column list ends at `summary`); a tree search for `graduation\|campus_status\|education_level` returns nothing |
| `UserProfile` / `ProfileToJson` / `UpdateProfile` carry no such fields | `user_dao_interface.h:23-34`, `user_controller.cpp:291-310`, `:579-660` |
| The list payload (`/api/v1/jobs`) has **no** score, reasons or matched/missing skills at all; `/recommended` has score + skills but **no human-readable reasons** | writers at `job_controller.cpp:698` (list) vs `:953-974` (recommended); `docs/04:281` vs `:440` |
| No deal-breaker concept and no deadline **veto/urgency** semantics; `deadline` is currently filter/sort-only | `docs/03:436` ("`deadline` 不参与可见性、排名或自动关闭"); `job_search_filter.h:39-41`; `openOnly` (`:41`) is a filter, not a veto |
| No `reasons` / `explanation` in the public ranking path | `RankedJob` has no reasons field (`job_ranking.h:34-42`); `MatchResult` has `strengths`/`gaps` but they are only serialized into `ai_tasks.result` |
| **PDF upload stores no text** → a PDF-only resume contributes nothing to matching | `resume_service.cpp:148-150` (`extension == ".pdf" ? std::nullopt : …`), documented in `active/0015A-...md:69` |
| No artifact table for a tailored resume / cover letter and no generation task | `04_application.sql:3-15` has no artifact; `ai_tasks.task_type` reserves `resume_optimize` (`05_ai.sql:6`) but the C++ side only knows `kJobMatchTaskType = "job_match"` (`ai_task.h:9`, `ai_task_dao.cpp:206-211`) and the worker's default is `("job_match",)` (`config.py:27`) |
| No frontend of any kind | `find . -name '*.html'` → only `source_adapters/jobpilot_sources/tests/fixtures/cn_jsonld_page.html` (a test fixture); every response writes `Content-Type: application/json` (`job_controller.cpp:103`) |
| `interview_records` / `notifications` are schema-only | `05_ai.sql:24-34`, `06_notification.sql:3-11`; the only references in code are `backend/tests/test_schema.cpp:186-187` and `:230-231` |

### 2.3 The owner's recorded intent (reused verbatim, not re-derived)

`active/0015A-job-list-usability-and-intake-quality.md:746-756` records, verbatim:

> 在现有确定性评分与候选人资料能力的实际审计结果基础上，规划只读岗位列表或报表，将匹配分数、可解释的匹配与差距、排序结果及原始岗位链接整合到完整的求职者工作流。

and `docs/research/2026-10-08-open-source-job-search-agents.md:182-188` records the
tailoring requirements: mechanical fact-provenance gate, ATS text-layer check,
relevance-weighted cutting, archive the submitted artifact, draft-only follow-up,
outcome-driven calibration. This plan adopts items 1-4 and defers 5-6 (§6, §11).

## 3. Scope

### 3.1 In scope (four phases, in dependency order)

| Phase | Deliverable | Depends on |
|---|---|---|
| **A** | Candidate data completeness: `profiles` gains education / graduation year / campus status; **and** PDF resumes stop contributing nothing (extraction, or an explicit degraded contract that is visible in the API) | — |
| **B** | Explainable matching: `rule_v1` as the **single authoritative public ranking**; deal-breaker + deadline urgency/veto semantics; list payload carries score + reasons + matched/missing skills (reuse the `MatchResult` contract) so no second call is needed | A (inputs), and the existing 0015A list payload |
| **C** | Read-only, self-contained report surface: score, explainable reasons/gaps, sort order, original posting link; explicit access decision; **no frontend application** | B |
| **D** | **The stage deliverable**: per-job tailored Chinese ATS-aware resume (+ cover letter **only if** Open Decision 1 says so), stored as an artifact tied to a resume version **and** an application, with a mechanical fact-provenance gate and a human-confirmation boundary | A+B (+C for display) |

### 3.2 Explicit Out-of-scope list

1. **No frontend application** — no SPA, no bundler, no React/Vue, no build step, no
   npm dependency, no browser-based admin console. Phase C is a report **view**,
   not an application (§11 Open Decision 2).
2. **No new China source adapter** — 0014 Step 5 remains environment-limited
   (`project-state.md:120-129`); 0015 does not touch the source allowlist,
   the registry or the capture framework.
3. **No auto-apply / auto-submit** — standing ADR-0011 non-goal; the two upstream
   projects refuse it. Phase D ends at a stored, human-confirmed draft.
4. **No renumbering** — `0015A` stays `0015A`, `0016` stays `0016`; no archived
   plan is rewritten (project-state stage table `:307-318`, 0015A §16 `:736-742`).
5. **No canonical multi-source job identity/merge** (ADR-0010 evolution boundary).
6. **No new LLM authority** — the LLM may not set `score`, `status`, `closed_at`,
   `missing_count` or any lifecycle fact (ADR-0010 §6, `docs/04:968-969`).
7. **No queue/notification/messaging infrastructure** — no RabbitMQ, no
   scheduler, no `notifications` wiring; `interview_records` stays untouched.
8. **No follow-up cadence / outcome calibration / weight learning** (research note
   items 5-6, deferred; they need submission tracking that does not exist).
9. **No PDF renderer / LaTeX / Word pipeline** in the core. If a rendered
   document is wanted it is an optional documented tool, not a core dependency
   (research note §5 caveats).
10. **No change to `rule_v1` weights, the 0012 freshness lifecycle, the 0015A
    quality gate, or the 48 KiB description cap.**

## 4. Reused vs New (the honesty table)

| Item | Reused (cite) | New in 0015 |
|---|---|---|
| Ranking authority | `rule_v1` (`job_ranking.h:27-42`) — **stays the only public ranking** | nothing; 0015 only **adds explanation** to it |
| Match vocabulary | `MatchResult` + evidence tags (`match_result.h:33-44`, `match_rule.h:17-19`) + `match_result_json.cpp` | a **list-level** projection of the same vocabulary (`reasons`/`deal_breakers`/`deadline`) |
| Deterministic match | `ScoreMatch` (`match_rule.h:26-33`), persisted in `ai_tasks.rule_result` | candidate inputs (Phase A) become first-class match components |
| List payload | 0015A already added `source_url`, filters, sort, `review_status` visibility | `score`/`score_version`/`matched_skills`/`missing_skills`/`reasons`/`deal_breakers` on `/api/v1/jobs` (opt-in) |
| Candidate profile | `profiles`, `UserProfile`, `ProfileToJson`, `UpdateProfile` (`01_user.sql:19-32`, `user_dao_interface.h:23-34`, `user_controller.cpp:291-310`, `:579-660`) | 3 nullable columns + validation + DTO/JSON/DAO/SQL plumbing |
| Resume content | `resumes`/`resume_versions`/`applications.resume_version_id` (`02_resume.sql:3-21`, `04_application.sql:7`), `IResumeDao::OwnedVersionExists` (`resume_dao_interface.h:71-74`), 1 MiB version cap (`resume_service.cpp:20-22`) | PDF text extraction **or** an explicit degraded contract; **one new artifact table** + storage |
| AI task shape | `ai_tasks` with owner/lease/`rule_result` (`05_ai.sql:3-22`), `/internal/ai/tasks/*` (`router.cpp:359-395`) | `resume_optimize` becomes a real task type **only if** Phase D uses the worker path (Open Decision 3) |
| Report surface | `/api/v1/jobs` + `/recommended` payloads; every response is JSON (`job_controller.cpp:103`) | one **read-only** rendering path (§11 Open Decision 2) |
| Docs contracts | `docs/03` §7/§8/§9 migration-contract pattern (`:258`, `:329`, `:377`) | `docs/03` §10 (new) + `docs/04` §5.1/§5.3/§7 additions |

## 5. Contracts

### 5.1 Phase A — candidate data (`docs/03` §10, `database/schema/01_user.sql`, `docs/04` §3.4)

```sql
profiles.education_level    TINYINT  DEFAULT NULL COMMENT '最高学历:1不限/2大专/3本科/4硕士/5博士(0015)',
profiles.graduation_year    SMALLINT DEFAULT NULL COMMENT '毕业年份 YYYY,合理范围 1990..2100(0015)',
profiles.campus_status      TINYINT  DEFAULT NULL COMMENT '1非应届/2应届/3在校(0015)',
KEY idx_campus (campus_status)          -- only if a real query justifies it (same rule as 0015A)
```

- **Semantics fixed by contract**: `education_level` uses the **same 1..5 enum as
  `jobs.education_required`** (`03_job.sql:23` comment) so Phase B can compare them
  without a mapping table; `graduation_year` is a 4-digit calendar year;
  `campus_status` is `1|2|3` with `NULL` meaning **unknown**, never "非应届".
- **NULL rule**: NULL stays NULL. No default is invented for existing rows
  (the 0013/0014 additive-migration precedent: `docs/03:301-322`).
- **API**: `PUT /api/v1/users/profile` and `GET /api/v1/users/profile` gain the
  three optional keys (`docs/04:169` §3.4); an out-of-range value → existing
  `400` envelope; unknown/absent → `null`. No key's meaning changes.
- **Matching contract (the part 0015A explicitly deferred)**: the three fields
  become deterministic match inputs in Phase B, with these semantics:
  - `jobs.education_required IS NULL OR profile.education_level >= jobs.education_required`
    — an **unknown job requirement never excludes** (mirrors the 0015A `education_max`
    NULL rule, `docs/04:353`).
  - `profile.education_level IS NULL` → **no education verdict** and no penalty;
    the reason string must say "学历未填写" rather than score as a mismatch.
  - `campus_status` ↔ `jobs.job_nature` compatibility (`1校招/2社招/3实习`,
    `03_job.sql:26`): 应届↔校招/实习 compatible; 在校↔实习; 非应届↔社招. An
    **unknown nature or unknown status is compatible** (fail-open, same principle
    as 0015A §6.3) and produces an advisory reason only.
  - Neither rule closes, hides or reclassifies a job.
- **PDF contract** (the second Phase A deliverable): a PDF resume must never
  silently contribute nothing. Two acceptable shapes; **the implementer must pick
  one** and the API must show which applies:
  - **(a) extraction** — parse the PDF text layer (stdlib-only constraint applies
    to `source_adapters/`, not to C++; a PDF text-layer reader is still a new
    dependency decision → Open Decision 4) and store it in
    `resume_versions.content` exactly as the `.txt/.md` path does
    (`resume_service.cpp:148-150`);
  - **(b) explicit degraded contract** — keep `content = NULL` **and** expose the
    reason: `resume_versions` gains nothing, but the upload/version response and
    `GET /api/v1/resumes/{id}/versions` mark the version
    `"text_extracted": false, "text_status": "pdf_no_text"`, and the AI match
    input bundle must state that resume text is unavailable (so a caller can tell
    "no text" from "no evidence"). **Silence is forbidden**; a `null` with no
    marker is the defect this phase removes.
- **Migration path**: fresh installs via `database/schema/01_user.sql`; existing
  DBs via a recorded additive ALTER in the new `docs/03` §10; `test_schema`
  asserts both a fresh and a migrated database (0014/0015A precedent,
  `docs/03:547-604`).

#### A.degraded — the PDF rule stated as a testable contract

| Aspect | Contract |
|---|---|
| A PDF-only resume | Never contributes text silently; either `content` is populated by extraction, or the version is explicitly marked `text_status="pdf_no_text"` |
| Matching with no resume text | `resume_evidence` is **empty**, not fabricated; the match reason says the resume text is unavailable |
| Failure mode to forbid | `content = NULL` with no marker and no reason in any response |
| Acceptance | A fixture PDF with a text layer → extracted bytes round-trip or `text_status` set; a scanned/image PDF → `text_status="pdf_no_text"` and a match that still returns a deterministic score |

### 5.2 Phase B — explainable matching (`docs/04` §5.1/§5.3, `docs/03` if an index is added)

- **Authority statement (contract, not aspiration)**: `rule_v1` is and remains the
  **single authoritative public ranking**. The LLM/enhanced worker result must
  never appear in `/api/v1/jobs` or `/recommended` ordering, score or reasons
  (`docs/04:968-969`; `job_ranking.h:34-42`; ADR-0009 amendment 0013).
- **List payload (additive keys, opt-in)**: with `with_match=true` **(and only
  then)**, every item of `GET /api/v1/jobs` gains
  `score`, `score_version` (`"rule_v1"`), `matched_skills`, `missing_skills`,
  `reasons[]`, `deal_breakers[]`, and `deadline_urgency`; without the parameter the
  payload is byte-identical to today. `/recommended` already returns the score and
  skills, so it only gains `reasons[]`/`deal_breakers[]`/`deadline_urgency`.
  - `reasons[]` reuses the **`MatchResult` evidence vocabulary**, projected
    compactly: `{"code": "<stable>", "text": "<human sentence>", "evidence": "skills|job_requirements|profile|resume_text|deadline"}`.
    `code` is machine-stable; `text` is Chinese prose; `evidence` is one of the
    existing tags (`match_rule.h:17-19`). **New**: the list projection and the
    `deadline` tag; **reused**: the tag set, the DTO shape and the serializer
    patterns.
  - `score` semantics stay exactly `rule_v1` (`docs/04:486-489`); 0015 does not
    re-weight it without an owner decision (Open Decision 5).
- **Deal-breaker / deadline semantics (new, and deliberately narrow)**:
  - A **deal-breaker** is a deterministic, evidence-backed veto that is returned
    as data (`deal_breakers[]`) and **never silently drops the row** from the
    list. Candidate deal-breakers in scope: education requirement unmet, campus
    status ↔ job nature incompatible, salary range with **no intersection**, and
    an explicitly expired `deadline`. Each carries a stable `code` + reason text.
  - A **separate, opt-in** parameter `strict=true` turns those same deal-breakers
    into an exclusion; default is `false`, so no existing caller loses rows.
  - `deadline_urgency` is derived **presentation metadata only**
    (`expired | urgent<=7d | soon<=30d | normal | unknown`), computed at the API
    boundary from the stored UTC instant plus `deadline_date_only`
    (`docs/03:389-437`). It **never** closes a job, sets `status`, writes
    freshness, or changes `review_status` (F13 invariant, 0015A §7.1).
  - A NULL deadline is `unknown` and is never treated as expired.
- **Compatibility**: every key and parameter is additive; `with_match`/`strict`
  default to today's behaviour; the ordering of `/api/v1/jobs` is unchanged unless
  the caller already asked for `sort=` (0015A `docs/04:346-357`).

### 5.3 Phase C — report surface (access decision required, no full frontend)

The surface must show, per row: rank/sort basis, `score`, `score_version`,
`reasons[]`, `missing_skills[]`, `deal_breakers[]`, `deadline_urgency`,
`source_url`, company/title/location and `freshness`. Access decision (Open
Decision 2) is between:

- **(C1) offline, self-contained single file** generated by a documented command
  from the public JSON (no server change, no session, no new endpoint); or
- **(C2) a read-only server-rendered HTML page** at a new authenticated route that
  reuses the existing session auth (`docs/04:29` §1.3).

Either way the contract is: **read-only** (no write path, no POST, no state
change), **no new auth mechanism**, **no frontend build tooling**, and the
original posting link is always present in the row (reusing
`JobSummary::sourceUrl`, `job_dao_interface.h:37-38`).

### 5.4 Phase D — tailored resume artifact + generation (`docs/03` §10, `docs/04` §4/§7)

- **New table** (the only new table in 0015):

```sql
CREATE TABLE IF NOT EXISTS resume_artifacts (
  id                BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
  user_id           BIGINT UNSIGNED NOT NULL,
  job_id            BIGINT UNSIGNED DEFAULT NULL,
  application_id    BIGINT UNSIGNED DEFAULT NULL,
  base_version_id   BIGINT UNSIGNED NOT NULL,   -- the resume_versions row it was derived from
  artifact_type     VARCHAR(16)  NOT NULL,      -- 'resume' | 'cover_letter'
  language          VARCHAR(8)   NOT NULL DEFAULT 'zh',
  content           MEDIUMTEXT   DEFAULT NULL,  -- generated text (ATS plain text)
  file_path         VARCHAR(255) DEFAULT NULL,  -- optional rendered artifact in FileStorage
  content_bytes     INT UNSIGNED DEFAULT NULL,  -- set when content is stored
  generator         VARCHAR(32)  NOT NULL,      -- 'rule_v1' | 'llm' | 'manual'
  source_version    VARCHAR(32)  DEFAULT NULL,  -- e.g. 'tailor_rule_v1'
  fact_check_status TINYINT      NOT NULL DEFAULT 0, -- 0 unchecked / 1 pass / 2 fail
  fact_check_report JSON         DEFAULT NULL,
  confirmed_at      DATETIME     DEFAULT NULL,  -- HUMAN confirmation (Phase D boundary)
  create_time       DATETIME     NOT NULL DEFAULT CURRENT_TIMESTAMP,
  update_time       DATETIME     NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  KEY idx_user_job (user_id, job_id),
  KEY idx_application (application_id),
  KEY idx_base_version (base_version_id),
  KEY idx_user_type_created (user_id, artifact_type, id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
```

- **Reuse, not redefine**: `base_version_id` references the existing
  `resume_versions.id` (`02_resume.sql:12-21`) and is validated through the
  existing ownership seam `IResumeDao::OwnedVersionExists`
  (`resume_dao_interface.h:71-74`); `application_id` references the existing
  `applications.id` (`04_application.sql:3-15`) and uses the existing
  `applications.resume_version_id` linkage (`:7`) as the "what was submitted"
  anchor. `job_id` is the existing `jobs.id`. **The artifact table never
  duplicates** `resumes`/`resume_versions` content as the source of truth — it
  stores a *derived* artifact plus its provenance.
- **Generation contract**: a tailored resume is produced **deterministically
  first**. The C++ side produces the rule-based draft (`generator='rule_v1'`,
  `source_version='tailor_rule_v1'`) and stores it; an optional worker/LLM pass may
  only *add* wording, exactly like 0013's `rule_result` rule (`docs/04:952-969`).
  A stage that cannot produce a draft without an API key would violate ADR-0009.
- **Fact-provenance gate (mechanical, mandatory)**: every generated claim must map
  to a source fact (profile field, skill record, resume-version content, or job
  requirement). The gate is a deterministic function; its result is persisted in
  `fact_check_status` + `fact_check_report`, and **`fact_check_status=2` (fail)
  blocks `confirmed_at`** and blocks any use. This is research-note item 1
  (`docs/research/...md:182`), implemented as a mechanism.
- **ATS-aware formatting contract (Chinese)**: plain-text/UTF-8 first,
  section-ordered, no images-only content, no tables-as-layout, stable headings
  the ATS can key on, and a documented text-layer check when a rendered artifact
  is produced (research-note item 2, `:183`). **The core stays text-first**; a
  rendered PDF/DOCX is an optional documented tool (Out-of-scope 9).
- **Human-confirmation boundary (hard)**: `confirmed_at IS NULL` means the artifact
  is a **draft**. No endpoint, worker or script may submit it anywhere; 0015 adds
  no submission capability at all. A confirmed artifact is only *associated*
  (application/job), never sent.
- **Migration path**: fresh install via a new `database/schema/07_resume_artifact.sql`
  (next free lexical number; `02_resume.sql` and `06_notification.sql` exist) or an
  appended block in `02_resume.sql`; existing DBs via the recorded ALTER/CREATE in
  `docs/03` §10; `test_schema` asserts both fresh and migrated databases.

### 5.5 Docs that must change **in the same change** as the behaviour

| Doc | Section | Change |
|---|---|---|
| `docs/03-database-design.md` | new **§10 0015 Additive Candidate + Artifact Migration Contract**; §3 DDL for `profiles`; §5 index table | columns, the new table, the ALTER, invariants (mirrors §7/§8/§9 structure) |
| `docs/04-api-design.md` | §3.4 (profile fields), §5.1/§5.3 (`with_match`/`strict`/reasons/deal-breakers/urgency), §4.1/§4.4 (PDF text status), §7.2 (`resume_optimize` reality) | additive keys + the degraded contract |
| `database/schema/01_user.sql` (+ new artifact file) | DDL | same as `docs/03` |
| `service/config/config.yaml` | comments | only if a switch is added (e.g. `with_match` default / artifact storage path) |
| `docs/ai/project-state.md` | Current Stage / Next Entry / stage table | 0015 status |
| `AGENTS.md` | "Current phase" | points at the active 0015 plan |
| `active/0015A-...md` | **not edited** except its own progress checkboxes if the Lead chooses | 0015A is a separate active plan |

`docs/03`/`docs/04` are locked contracts (`AGENTS.md` §"Hard Constraints"):
the contract is updated first and reviewed, then code depends on it.

## 6. Selected Design

### 6.1 Phase A — candidate data

```text
profiles (+3 nullable columns)                 [reused table, additive columns]
  education_level  1..5  == jobs.education_required enum   [single enum, no mapping]
  graduation_year  YYYY  (1990..2100)
  campus_status    1 非应届 / 2 应届 / 3 在校   (NULL = unknown, never "非应届")
        |
        +--> UserProfile DTO + ProfileToJson + UpdateProfile validation   [reused seams]
        +--> docs/04 §3.4 additive keys
        +--> Phase B deterministic match inputs
PDF resume
  (a) extract the text layer -> resume_versions.content       [reused storage path]
  (b) or keep NULL + text_status="pdf_no_text" in responses   [explicit degradation]
```

Rejected here: reusing the free-text `profiles.experience` column for education
(unqueryable, unmatchable, unvalidatable); auto-defaulting `campus_status` from
`graduation_year` (fabricates a candidate fact that then drives a match verdict);
storing education as a string (breaks the numeric comparison against
`jobs.education_required`).

### 6.2 Phase B — explainable matching

```text
rule_v1 (unchanged weights, unchanged authority)          [reused]
  + candidate components (education / campus / deadline evidence)   [new inputs]
  -> RankedJob{score, breakdown, matched, missing}          [reused]
  -> list/JSON projection:
       score, score_version, matched_skills, missing_skills  [reused vocabulary]
       reasons[]        (MatchResult evidence tags)          [new projection]
       deal_breakers[]  (data, not a silent drop)            [new, narrow]
       deadline_urgency (presentation only, never lifecycle)  [new, narrow]
  strict=false (default: all rows stay) | strict=true (opt-in exclusion)
  with_match=false (default: today's payload) | true (adds the keys)
```

Rejected here: making the LLM result part of the public ranking; dropping
deal-breaker rows by default; turning `deadline` into a lifecycle/visibility
authority (F13 forbids it); a second "explain" endpoint per row (the brief
explicitly wants one call); re-implementing the score in a new module instead of
reusing `ScoreJob`/`ScoreMatch`.

### 6.3 Phase C — report surface

One rendering path, read-only, showing the full explanation for every row plus the
original link. It reuses the contract from Phase B verbatim — it fetches the same
JSON, so the report can never show a number the API does not return.

Rejected here: a SPA/React application; a new unauthenticated public route; a
server-side template engine dependency; showing anything the API does not return
(no client-side re-scoring); making the report a write surface.

### 6.4 Phase D — tailored resume

```text
base resume version (owned, exists)            [reused: resume_versions + OwnedVersionExists]
job (id, requirements, education/job_nature/deadline)  [reused: jobs + 0014/0015A]
application (optional link)                    [reused: applications.resume_version_id]
        |
        v
tailor_rule_v1 deterministic draft            [new, pure C++ function]
        |
        +--> optional worker/LLM wording pass  [reused worker shape; may only add]
        +--> mechanical fact-provenance gate   [new, mandatory; fail blocks confirm]
        +--> artifact stored (text + optional file)  [new table + reused FileStorage]
        +--> HUMAN confirmation (confirmed_at) [the hard boundary: draft until then]
```

Generation is deterministic-first so the capability exists with **no AI key and no
worker** (ADR-0009). Cover letters are the same pipeline with
`artifact_type='cover_letter'` **only if** Open Decision 1 says in scope.

Rejected here: sending the tailored text through `resume_versions` as if it were a
user upload (it would pollute the user's own version history and destroy the
provenance link); storing the artifact only as a file with no DB row (no ownership,
no audit, no fact-check status); letting an LLM write the artifact unchecked
(research note item 1 requires a mechanism, not a prompt); any submit action.

### 6.5 Dependency table (Schema / DTO / DAO / Service / Controller / Python)

| Layer | Phase A | Phase B | Phase C | Phase D |
|---|---|---|---|---|
| Schema | `profiles` +3 columns (+ evidence-gated index) | — (maybe one index for the veto/sort path, `EXPLAIN`-gated) | — | new `resume_artifacts` table |
| DTO | `UserProfile` +3 fields | `RankedJob`/list item reason structs | — | `ResumeArtifact` DTOs |
| DAO | `IUserDao` profile read/write | reuse `IJobDao`/`IUserProfileReader` | — | new `IResumeArtifactDao` |
| Service | profile validation | `ScoreMatch`/reason projection in `JobRankingService`; `JobSearchFilter` gains `withMatch`/`strict` | report assembly | `ResumeTailorService` + fact-check + artifact write |
| Controller | `UserController` §3.4 | `JobController` list writers | report route (C2) or a documented CLI (C1) | `ResumeController` (or AI task) endpoints |
| Python | — | — | — | worker `resume_optimize` handler **only if** the worker path is chosen |

Change-order forcing: contract docs → schema → DAO → service → controller → tests;
Phase B's list keys depend on Phase A's DTO fields; Phase D depends on A+B.

### 6.6 No new services or architecture refactor

No new process, queue, cache, service binary or architecture layer. Phase D's
table is a new **DAO** inside the existing `jobpilot_service_core`; the worker (if
used) is the existing `ai_worker` package with one more task type. The report
(Phase C) is either a documented local command or one read-only route in the
existing router.

## 7. Invariants

1. **`rule_v1` is the single authoritative public ranking.** The LLM/enhanced
   result never appears in a public score, ordering or `reasons[]`
   (`docs/04:968-969`, ADR-0009 + 0013 amendment).
2. **AI never decides a lifecycle fact** — no AI path may write `status`,
   `closed_at`, `missing_count`, `review_status` (ADR-0010 §6; 0015A invariants).
3. **A NULL candidate field never becomes a negative match.** Unknown education /
   campus / nature is compatible and yields an advisory reason, not a penalty or a
   deal-breaker (mirrors 0015A's fail-open principle).
4. **No silent zero-text resume.** A PDF either has extracted text or is explicitly
   marked degraded (Phase A contract); matching must be able to tell "no text"
   from "no evidence".
5. **Deal-breakers are data by default.** `strict=false` means no row is dropped;
   a veto is reported, never silently applied (the 0015A "never silently hide"
   principle).
6. **`deadline` remains presentation/filter only** — never closes a job, never
   writes freshness, never changes `visibility`; F13 (`docs/03:389-437`) is
   untouched.
7. **Every generated claim maps to a source fact.** `fact_check_status=2` blocks
   confirmation and use; unreferenced generated text is dropped, never persisted
   as a fact (research note item 1).
8. **Human confirmation is the boundary.** `confirmed_at IS NULL` ⇒ draft; 0015
   contains **no** submission, upload-to-employer or auto-apply path.
9. **Artifact provenance is explicit**: `base_version_id` is owned and exists;
   `application_id` (when set) is owned; the artifact never overwrites a
   `resume_versions` row.
10. **Additive contracts**: no existing key, parameter, status or error code
    changes meaning; defaults (`with_match=false`, `strict=false`) reproduce
    today's payload byte for byte.
11. **Read-only report**: Phase C performs no write and adds no auth mechanism.
12. **Existing behaviour preserved**: 0012 freshness, 0015A quality gate +
    `review_status` visibility, the 48 KiB description cap, `uk_source_url`
    idempotency and the `manual`-wins rule (F14) are untouched.
13. **`docs/03`/`docs/04` change in the same change as the behaviour** (0015A §5.6
    precedent).
14. **Deterministic core**: with `ai.enhancement_enabled=false`
    (`service/config/config.yaml:34`) a match **and** a tailored draft must still
    be produced; no key, no worker required.
15. **No credential leakage**: tokens/keys are never logged; a log-scan test
    covers any new path (0015A precedent).
16. **No new frontend build tooling** and no new core runtime dependency without
    an explicit decision (§11 Open Decision 4).

## 8. Testing Plan (targeted L1/L2 slices, ONE L4 at stage close)

Reused targeting discipline: `docs/ai/agent-workflow.md` §2, §2.1, §8. Tests are
named against **existing** binaries where the change lands in them.

| Phase | L1 (per touched binary) | Cases |
|---|---|---|
| A (profile) | `test_user_service`, `test_user_http`, `test_user_dao`, `test_schema` | enum/range validation (0/6 rejected, 1..5 accepted, `graduation_year` 1990..2100); NULL round-trip; `GET`/`PUT` additive keys; out-of-range → `400`; migrated + fresh DB schema assertions; unknown fields still `null` |
| A (PDF) | `test_resume_service`, `test_resume_http`, `test_resume_dao` | fixture PDF with a text layer → extraction or explicit `text_status`; image-only PDF → `text_status="pdf_no_text"`; **no path returns `content=NULL` silently**; `.txt/.md` path unchanged |
| B (match inputs) | `test_job_ranking_service`, `test_match_rule` | education/campus components: compatible, incompatible, **NULL job requirement ⇒ never excluded**, **NULL profile field ⇒ no penalty + advisory reason**; campus↔nature matrix; salary no-intersection veto; determinism (same input twice); score stays in `[0,100]` |
| B (list payload) | `test_job_http`, `test_job_dao`, `test_job_service` | `with_match=false` ⇒ byte-identical today's payload; `with_match=true` ⇒ score/version/skills/reasons/deal_breakers/urgency present; `strict=false` keeps vetoed rows; `strict=true` excludes them; `deadline_urgency` for `expired/urgent/soon/normal/unknown` incl. `deadline_date_only` handling; `CountJobs`/`total` parity for every combination; **LLM result never appears** |
| C (report) | *(C1)* a Python/CLI fixture test over the recorded JSON; *(C2)* `test_job_http` for the read-only route + auth | report shows exactly what the API returns (no client-side re-scoring); no write verb; the original link is present for every row; unauthenticated access rejected for C2 |
| D (artifact) | `test_schema`, new `test_resume_artifact_dao`, `test_resume_service`/`_http`, `test_ai_*` if the worker path is used | artifact write with owned `base_version_id`; wrong owner → existing not-found code; `application_id` ownership; draft (`confirmed_at IS NULL`) semantics; fact-check pass/fail (fail blocks confirm); provenance fields round-trip; `generator='rule_v1'` draft exists with **no** AI key |
| D (worker, only if chosen) | `ai_worker` unittest suite | `resume_optimize` claim path; merge may only add wording; fact-check failure → no confirmation; key never logged; degradation with no provider |
| Docs | `git diff --check` | `docs/03`/`docs/04`/`README`/`AGENTS`/`project-state` consistent with the landed behaviour (§5.5 same-change rule) |

**L2**: the affected labels once the slices land — `ctest -L job` (Phase B),
`ctest -L user` (Phase A profile), `ctest -L resume` (Phase A PDF/D),
`ctest -L ai` (Phase D worker path). **L4 (once, at stage close)**: service
Debug + ASan + UBSan full CTest, service and root Release builds, both Python
suites, per `docs/ai/agent-workflow.md` §2.2/§3 with the environment probe first.
Reuse unaffected evidence; never re-run a green suite (§8.1).

## 9. Phased Steps per Phase (dependencies + acceptance criteria)

### Phase A — candidate data completeness

| # | Step | Depends on | Acceptance criteria |
|---|---|---|---|
| A0 | **Recon**: confirm the `profiles` write/read path, the enum compatibility with `jobs.education_required`, and the PDF decision input (extraction dependency vs degraded contract) | — | a written decision for the PDF branch (§5.1 a/b) and the index decision for `campus_status` (evidence or "no index") |
| A1 | **Contract first**: `docs/03` new §10 + §3 DDL + §5 index table; `docs/04` §3.4 fields | A0 | contract reviewed before code; migration path recorded |
| A2 | **Profile columns + DTO + validation + DAO + JSON**: `database/schema/01_user.sql`, `UserProfile`, `ProfileToJson`, `UpdateProfile`, DAO read/write, `test_schema` fresh+migrated | A1 | `test_user_*`/`test_schema` green; NULL round-trip; out-of-range `400` |
| A3 | **PDF text contract**: either extraction into `resume_versions.content`, or the explicit `text_status` contract in the version responses + match input | A1 | §8 A(PDF) cases green; **no silent NULL path**; docs updated in the same change |

**Phase A invariants**: NULL ≠ negative; the enum is shared with the job side;
additive-only migration; no existing profile key changes meaning.
**Phase A out of scope**: resume parsing beyond PDF text (no OCR, no DOCX, no
image rendering); no "auto-fill from LinkedIn/PDF" onboarding (research note
item J is backlog).

### Phase B — explainable matching

| # | Step | Depends on | Acceptance criteria |
|---|---|---|---|
| B0 | **Contract first**: `docs/04` §5.1/§5.3 additive keys + `with_match`/`strict` + the authority statement; any index decided by `EXPLAIN` recorded in `docs/03` §9/§10 | A2 (inputs exist) | the contract names every new key and its default; authority statement present |
| B1 | **Candidate components in scoring**: extend `ScoreJob`/`ScoreMatch` inputs with education/campus; keep weights unchanged; NULL rules implemented | A2, B0 | §8 B(match inputs) cases green; score bounds unchanged; determinism |
| B2 | **Reason projection**: list-level `reasons[]` reusing `MatchResult` tags + the new `deadline` tag | B1 | reasons cite only delivered facts; no LLM text |
| B3 | **Deal-breaker + deadline urgency**: `deal_breakers[]`, `deadline_urgency`, `strict` exclusion | B1, B0 | default keeps every row; `strict=true` excludes only the documented codes; F13 untouched |
| B4 | **List/recommended writers + filter plumbing**: `JobSearchFilter.withMatch/strict`, controller writers, `docs/04` in the same change | B2, B3 | `with_match=false` byte-identical; `true` adds keys; `total` parity; no second call needed |

**Phase B invariants**: `rule_v1` authority; no LLM in the payload; additive keys;
`deadline` never lifecycle.
**Phase B out of scope**: re-weighting `rule_v1`; a learned/outcome-calibrated
score; per-requirement weights (0013 §17 amendment stays proposed); a search
engine.

### Phase C — read-only report surface

| # | Step | Depends on | Acceptance criteria |
|---|---|---|---|
| C1 | **Owner decision 2** (C1 offline file vs C2 read-only route) recorded | B4 | the decision is written into this plan's §11 before code |
| C2 | **Implement the chosen path** (no frontend tooling) | C1 | §8 C cases green; read-only; original link per row; no client-side re-scoring |
| C3 | **Docs**: how to produce/open the report + its access model (`docs/04` §5.7 or the README/tool docs) | C2 | a reader can reproduce the report from the documented command/route |

**Phase C invariants**: read-only; no new auth; no frontend build.
**Phase C out of scope**: interactivity, saved views, charts, export formats beyond
the chosen one, a dashboard server.

### Phase D — the stage deliverable

| # | Step | Depends on | Acceptance criteria |
|---|---|---|---|
| D0 | **Owner decisions 1 and 3** (cover letters in/out; worker path vs deterministic-only) recorded | B4, A3 | decisions written into §11 before code |
| D1 | **Contract first**: `docs/03` §10 artifact table + `docs/04` §4/§7; storage location decision | D0 | contract reviewed; migration path recorded |
| D2 | **`tailor_rule_v1` deterministic draft** (pure function) + `test_resume_service`/new unit tests | D1 | a draft is produced with no AI key and no worker; determinism asserted |
| D3 | **Artifact persistence**: new DAO/table, ownership checks through `OwnedVersionExists`, optional `FileStorage` artifact | D1, D2 | §8 D(artifact) cases green; provenance fields correct; no overwrite of `resume_versions` |
| D4 | **Fact-provenance gate** (mechanical) + persistence | D2 | every claim maps to a source fact or is dropped; fail ⇒ `fact_check_status=2` ⇒ confirmation blocked |
| D5 | **Human-confirmation boundary** + artifact read endpoints | D3, D4 | draft vs confirmed distinguishable; **no submission endpoint exists** |
| D6 | *(conditional)* **worker `resume_optimize` handler** — only if Open Decision 3 chooses it; may only add wording | D4, D5 | wording-only merge proven; rule draft survives worker failure; key never logged |
| D7 | **Docs sync + the stage's single L4** | A2-A3, B1-B4, C2, D1-D6 | README/AGENTS/project-state/this plan consistent; one full L4 green; `git diff --check` |

**Phase D invariants**: deterministic-first; fact-provenance blocks confirmation;
no auto-apply; artifact provenance explicit; the artifact never overwrites a user
resume version.
**Phase D out of scope**: PDF/LaTeX rendering pipeline in the core, cover-letter
sending, follow-up cadence, outcome calibration, interview generation
(`interview_records` untouched).

## 10. Compatibility and Rollback

- **Additive only**: 3 nullable `profiles` columns (+ at most one evidence-gated
  index), one new table, one new DAO, additive response keys behind defaults,
  one optional report path. No existing key, parameter, status or error code
  changes meaning.
- **Single behavioural changes, both opt-in**: `with_match`/`strict` (Phase B)
  default to today's payload; the report (Phase C) is read-only. Rollback =
  stop passing the parameter / remove the report path; **no data change**.
- **Artifact rollback**: dropping the `resume_artifacts` table or leaving it unused
  is safe (nothing else references it); the ALTER is additive, so reverting code
  while leaving columns/tables in place is the documented path (0012/0013/0014/0015A
  precedent).
- **PDF contract rollback**: extraction can fall back to the explicit
  `text_status` degraded contract without a schema change (`resume_versions` is
  untouched by either branch).
- **AI rollback**: `ai.enhancement_enabled=false` (the shipped default,
  `service/config/config.yaml:34`) leaves Phase D's deterministic draft fully
  functional; disabling the worker changes wording quality only.
- **Contract rollback**: the `docs/03`/`docs/04` additions revert with the code;
  no deployed client depends on the new keys.
- **No backfill, no destructive operation, no existing row rewritten in place.**

## 11. Open Decisions (owner-level only)

| # | Decision | Options | Why it is owner-level | Blocks |
|---|---|---|---|---|
| 1 | **Cover letters in this stage?** | (a) resume only; (b) resume + cover letter (`artifact_type='cover_letter'`) | It changes the stage deliverable's size and the artifact contract's user-visible scope | D0/D1 (the design supports (b) with no schema change) |
| 2 | **Report surface form** | (C1) offline self-contained single file produced by a documented command; (C2) read-only authenticated server-rendered route | Presentation + access model is a product decision; both are read-only and both avoid a frontend app | C1/C2 |
| 3 | **Phase D generation path** | (a) deterministic `tailor_rule_v1` only; (b) deterministic draft + optional `ai_worker` `resume_optimize` wording pass | Adding a real task type + worker handler enlarges the stage and introduces provider surface; ADR-0009 permits either but the default must stay AI-free | D0/D6 |
| 4 | **PDF handling branch + any new dependency** | (a) extract text with a new PDF text-layer reader; (b) keep it degraded with an explicit `text_status` contract; (c) extraction via an optional external tool (non-core) | A new core dependency needs an explicit decision; (b) is zero-dependency and already contract-complete | A0/A3 |
| 5 | **Score/reason weighting changes** | (a) keep `rule_v1` weights exactly; (b) add candidate components with documented re-normalization; (c) re-weight | Changing the authoritative score's meaning is a product decision and invalidates recorded 0011/0013 evidence | B1 |
| 6 | **`platform_hosts` reader in C++ → yaml-cpp?** | (a) leave the current hand-written line reader; (b) move it to yaml-cpp (already a dependency in `backend/src/core/config.cpp:11`) | It is a maintainer/robustness trade-off unrelated to 0015's deliverables; asking prevents scope creep | none (must **not** block 0015) |

Only these six are owner-level. Everything else (file names, error codes, SQL
shape, test names) is an implementer decision inside the documented contracts.

## 12. Risks

| Priority | Risk | Mitigation |
|---|---|---|
| High | A PDF resume silently contributes nothing (a "successful" match on no evidence) | Phase A's explicit contract + the `no silent NULL` test; matching must state text unavailability |
| High | The LLM result leaks into the public score/reasons | Authority invariant + a test asserting the public payload contains only `rule_v1` values |
| High | Fabricated tailored claims reach a user as if verified | mechanical fact-provenance gate; fail blocks confirmation; unreferenced text dropped |
| High | Any accidental auto-submit | No submission endpoint exists in 0015; `confirmed_at` is only an association |
| Medium | NULL candidate fields silently punish candidates | fail-open invariant + explicit advisory reason + tests for the NULL matrix |
| Medium | Deal-breakers silently hide jobs | `strict=false` default; vetoes are data; explicit opt-in |
| Medium | Weight changes invalidate 0011/0013 evidence | keep weights by default; any change is an owner decision + regression evidence |
| Medium | Artifact table pollutes/duplicates resume versions | artifact is derived with `base_version_id` provenance; never overwrites `resume_versions` |
| Medium | A new core PDF dependency bloats a self-hosted build | Open Decision 4 offers a zero-dependency degraded path; the dependency choice is explicit |
| Medium | Report surface becomes a de-facto frontend | read-only contract, no build tooling, no write path; explicit Out-of-scope 1 |
| Medium | Stage scope creep into follow-up/outcome features | §3.2 excludes them; §11 keeps only six owner decisions |
| Low | Two active plans (`0015A`, `0015`) confuse handoff | §16 records the numbering decision; `project-state.md` names the active stage at close |

## 13. Stop Conditions

Stop and report to the Lead (do not widen scope) if:

1. Phase B cannot keep `rule_v1` authoritative without changing its meaning
   (i.e. the explanation would require a different score) → stop, owner decision 5.
2. The LLM path becomes **required** for a match or a tailored draft → stop
   (violates ADR-0009 and invariant 14).
3. A PDF contract cannot avoid the silent-NULL defect without a new core
   dependency and the owner declines it → stop; ship the explicit degraded
   contract or defer Phase A's PDF half.
4. The artifact design requires overwriting or renumbering `resume_versions`, or a
   canonical resume-identity change → stop.
5. The report cannot be delivered read-only without a frontend build toolchain →
   stop (Out-of-scope 1).
6. A required `docs/03`/`docs/04` change cannot land **in the same change** as the
   behaviour → stop and report the blocker (§5.5).
7. Cover-letter scope expands into sending/tracking/follow-up → stop (Out-of-scope 8).
8. Any step would require touching the source allowlist, the 0015A quality gate,
   the 48 KiB cap, F13/F14 semantics, or archived plans → stop.
9. Work cannot be bounded to the §8 test strategy without a new L4 or a second
   full matrix → stop and report the budget problem
   (`docs/ai/agent-workflow.md` §8).

## 14. Progress Checkboxes

- [x] Reuse research (0015A §17 verbatim goal, competitive note §4, project-state)
- [x] Confirm current-tree facts (HEAD `bd7d451`)
- [x] Design + contracts + phases (this document)
- [ ] Phase A: contract (A1) → columns/DTO/DAO (A2) → PDF contract (A3)
  - [x] A1 + A2 (minus PDF): `docs/03` §10 + §3 DDL + §5 note, `docs/04` §3.4 keys,
        `database/schema/01_user.sql`, `UserProfile`/DAO/`UpdateProfile`/`ProfileToJson`,
        `test_user_*` + `test_schema` (fresh + migrated); NULL = unknown, no backfill.
  - [x] A3 (PDF text contract) — owner decision 4 = **option (b)**, explicit degraded contract,
        zero new dependencies, no extraction tool: a PDF is accepted as before and
        `resume_versions.content` stays `NULL`, but the upload/version responses and
        `GET /api/v1/resumes/{id}/versions` always carry `text_extracted` + `text_status`
        (`ok` / `text_empty` / `pdf_no_text` / `text_unavailable`). The state is **derived**
        from `content` + `file_path`, so no column, no migration, no backfill
        (`docs/03` unchanged). The classification seam `ClassifyResumeText` is exported for
        phase B's match input bundle (B1), which is where "resume text unavailable" becomes a
        match-input/reason statement.
  - [x] Phase A complete (A1+A2 profile candidate data, A3 explicit PDF text contract).
- [ ] Phase B: contract (B0) → candidate components (B1) → reasons (B2) →
      deal-breakers/urgency (B3) → writers + docs (B4)
  - [x] B0: `docs/04` §5.1 additive keys + `with_match`/`strict` + the `rule_v1`-only authority
        statement; `docs/03` §10.5 records the index decision (no new index).
  - [x] B1: `match_explanation.h/.cpp` — deadline urgency, deal-breakers as data, reasons;
        NULL on either side never vetoes (advisory reason instead); weights untouched
        (decision 5). `RankedJob` carries the projection; `/recommended` exposes it.
  - [x] B2: reasons[] on the list payload via `JobRankingService::ExplainJobs` (same seam as
        `/recommended`).
  - [x] B3/B4 writers: `with_match=true` and `strict=true` on `GET /api/v1/jobs` are landed.
        `SearchJobs`, its count and the new strict candidate window share ONE ordering helper
        (`ListOrderBy` in `job_dao.cpp`) plus one SELECT prefix, so they can never diverge;
        the strict window is paginated in the list's ordering, `total` counts the
        strict-filtered set, and a capped window (2000) is surfaced as `truncated: true`.
        Both parameters require a session, accept only `true|false`, and `with_match` absent
        is byte-identical (asserted in `test_job_http`).
- [ ] Phase C: decision 2 (C1) → implement (C2) → docs (C3)
  - [x] C1: owner default decision 2 = **C1 offline self-contained report**. Data source is the
        existing authenticated `GET /api/v1/jobs?with_match=true` (or a captured response body);
        no server surface, no new auth mechanism, no frontend tooling, no new dependency.
        Ordering is the API's own order with a positional rank; the original link is in every row.
  - [x] C2: `tools/job_report/render_report.py` (stdlib only) writes ONE self-contained HTML file
        (inline CSS, no external asset, no script); a row without reasons renders the explicit
        `无匹配理由` marker; `--input` mode performs zero network I/O and `--url` mode performs a
        single `GET` with the existing Bearer token (never a mutating verb, never the database).
  - [x] C3: documented in `docs/04` §8 + `tools/job_report/README.md`; deterministic fixture tests
        in `tools/job_report/tests/test_render_report.py` (10 cases, all green).
- [ ] Phase D: decisions 1/3 (D0) → contract (D1) → `tailor_rule_v1` (D2) →
      artifact persistence (D3) → fact gate (D4) → confirmation boundary (D5) →
      worker (D6, conditional) → docs + single L4 (D7)
  - [x] D0 (owner decisions recorded): decision 1 = **RESUME ONLY** — no cover letters in this
        stage (`artifact_type` only `'resume'`); decision 3 = **deterministic `tailor_rule_v1`**
        draft with no AI key (ADR-0009), with an optional ai_worker pass allowed only to ADD
        wording, never facts/scores/lifecycle.
  - [x] D1 (contract): `docs/03` §10.6 — `resume_artifacts` DDL + invariants (ownership via
        `OwnedVersionExists`, no fact-source duplication, `fact_check_status` 0/1/2 with 2 blocking
        `confirmed_at`, `confirmed_at IS NULL` = draft, no submission path) + the new-table
        migration; `docs/04` §9 — provenance tags, placeholder rule, fact gate, confirmation
        boundary, ATS formatting.
  - [x] D2 (`tailor_rule_v1`): pure `BuildTailorDraft` (`tailor_rule.h/.cpp`) — deterministic
        Chinese ATS plain text from profile + chosen resume version + job requirements, every
        asserted fact provenance-tagged, anything underivable becomes `【请填写：…】`, no LLM in
        the path; `test_tailor_rule` 5/5 (determinism, provenance, no-invention, matched/missing
        requirements, ATS section contract).
  - [ ] D3: artifact persistence (new `07_resume_artifact.sql` + DAO + ownership checks +
        `test_schema` on fresh AND migrated DBs, new `test_resume_artifact_dao`).
  - [ ] D4: mechanical fact-provenance gate + persistence (fail ⇒ `fact_check_status=2` ⇒
        confirmation blocked).
  - [x] D4 (fact gate): pure `FactCheck(draft)` — every fact must cite an allowed source AND appear
        in the content; every content line must be a heading, an explicit `【请填写：…】` slot,
        inside the delimited verbatim quote block (`原始简历文本（逐字引用）：` …
        `【原始简历文本结束】`), or fully covered by source facts (longest-first erasure, only
        labels/separators may remain); recorded placeholders must be rendered. Failure ⇒
        `fact_check_status=2` + JSON report (values asserted, never byte layout) ⇒ confirmation
        blocked. `test_tailor_rule` 9/9 (incl. an injected unsourced claim being caught, unknown
        provenance, a dangling fact, placeholder handling) and `test_resume_artifact_dao` 4/4
        (the persisted gate verdict blocks `Confirm`).
  - [ ] D5: human-confirmation boundary + artifact read endpoints (draft vs confirmed; **no
        submission endpoint exists**).
  - [x] D6 — **DEFERRED by owner decision (recorded in §17)**, not skipped silently: the worker
        path is optional, the deterministic `tailor_rule_v1` draft is the deliverable, and no LLM
        surface / AI key / worker handler is wired in this stage. `generator='rule_v1'` +
        `source_version='tailor_rule_v1'` remain the ONLY generator; any future wording pass is
        already bounded by the D4 fact gate (wording may only be ADDED, never invented).
  - [ ] D7: docs sync (README/AGENTS/project-state) + the stage's single L4.

## 15. Smallest First Implementable Unit

**A2 (minus the PDF half) is the smallest unit that ships value and can be
verified in isolation:**

> Add `profiles.education_level` (`TINYINT NULL`, 1..5, same enum as
> `jobs.education_required`), `profiles.graduation_year` (`SMALLINT NULL`,
> 1990..2100) and `profiles.campus_status` (`TINYINT NULL`, 1 非应届 / 2 应届 /
> 3 在校) with the `docs/03` §10 contract + `docs/04` §3.4 keys; plumb them through
> `UserProfile`, `ProfileToJson`, `UpdateProfile` validation and the profile DAO;
> assert the columns on a fresh **and** a migrated database in `test_schema`.

- **Why this first**: it is a single additive migration plus a single module
  (User), it needs no AI, no worker, no new table and no public-ranking change, and
  it unblocks Phase B's candidate components.
- **Touched write scope**: `database/schema/01_user.sql`, `docs/03` §3/§5/§10,
  `docs/04` §3.4, `service/{include,src}/jobpilot/service/user/*`,
  `service/tests/test_user_*`, `backend/tests/test_schema.cpp`.
- **Acceptance evidence**: `test_user_service`/`test_user_http`/`test_user_dao`
  green with the NULL/range/enum cases, `test_schema` green on fresh + migrated
  databases, `git diff --check` clean, and the contract docs updated in the same
  change.
- **Explicitly not in this unit**: the PDF half of Phase A, any matching change
  (Phase B), any artifact/table (Phase D).

## 16. Stage Numbering (unchanged)

`0015` = resume tailoring and `0016` = open-source packaging stay exactly as the
recorded stage table fixes them (`docs/ai/project-state.md:307-318`). The hardening
stage keeps its `0015A` name and file
(`active/0015A-job-list-usability-and-intake-quality.md`, §16 `:736-742`). This
plan **renames nothing, renumbers nothing and edits no archived plan**; it also
does not touch `0015A`'s content beyond noting (in §5.5) that its own plan is a
separate artifact.

## 17. Known Issues and Follow-ups

- **D6 (ai_worker `resume_optimize` wording pass) is DEFERRED — decision recorded
  2026-10-10, not silently dropped.** Owner decision 3 makes the worker path
  optional, and the deterministic path is the stage deliverable, so 0015 ships with
  `generator='rule_v1'` / `source_version='tailor_rule_v1'` as the **only**
  generator: no LLM surface is wired, no AI key is required, no worker handler is
  added. If it is ever revisited, the boundary is already fixed and enforced:
  a wording pass may only **add** wording, and `FactCheck` (D4) mechanically rejects
  any appended claim that does not map to a source fact — including text appended
  after the verbatim quote terminator `【原始简历文本结束】`. Reopening this needs a
  new decision entry here and in §11 (Open Decision 3), plus the wording-only merge
  test named in §8 D(worker).
- `resume_optimize` / `interview_gen` are reserved contract-only endpoints
  (`docs/04:904-911`); 0015 may make `resume_optimize` real (§11 Open Decision 3)
  and still leaves `interview_gen` and `interview_records` untouched.
- Outcome-driven calibration, follow-up cadence and application-artifact archival
  per the competitive note are **deferred** (§3.2 item 8); they need submission
  tracking that does not exist.
- Interview records/notifications remain schema-only (`05_ai.sql:24-34`,
  `06_notification.sql:3-11`); 0015 adds no code path for them.
- A rendered (PDF/DOCX) tailored resume in the core is out of scope; if the owner
  wants one it belongs in an optional documented toolchain.
- `profiles.experience` remains free text and is **not** the education source; no
  new free-text parsing is introduced.
- The C++ `platform_hosts` reader is a hand-written line parser
  (`job_quality.cpp:358-520`) with Python parity tests
  (`source_adapters/jobpilot_sources/tests/test_registry.py`,
  `source_adapters/tools/verify_platform_name_parity.cpp`); moving it to yaml-cpp is
  Open Decision 6 and **must not block 0015**.
- The report surface's access model must be documented where users look for it
  (`docs/04` §5.7 or the README), not only in this plan.
- Nothing in this document is implemented; no test, build, database or network
  operation was performed while authoring it.
