# JobPilotAI ExecPlans

This is the stage index. The detailed plan bodies were removed from the public
tree to keep the repository reader-focused; each one remains available in Git
history at the commit that introduced it.

ExecPlans were used for multi-module work: architecture boundaries, compatibility,
concurrency, migrations, and rollback-sensitive changes. Small documentation or
local changes did not get a plan.

## Stage index

| Stage | Plan | Result | Merge | One-line outcome |
|---|---|---|---|---|
| 0001 | Repository baseline hardening | completed | linear (pre-PR) | Baseline build, test, and repository hygiene. |
| 0002 | P0 configuration, logging, and schema | completed | linear (pre-PR) | Config, logging, and the initial MySQL schema. |
| 0003 | Quality and performance baseline | completed | linear (pre-PR) | Sanitizer presets and the first benchmark baseline. |
| 0004 | P1 minimum correctness gate | completed | linear (pre-PR) | Bounded HTTP bodies, multipart parsing, and the P1 freeze. |
| 0005 | User vertical slice | completed | linear (pre-PR) | Register, login, Redis session, and `me`. |
| 0006 | Profile and skill vertical slice | completed | linear (pre-PR) | Authenticated profile and skill CRUD. |
| 0007A | Resume infrastructure | completed | linear (pre-PR) | Storage, transactions, and shared session authentication. |
| 0007 | Resume vertical slice | completed | linear (pre-PR) | Resume upload, list, and text versions. |
| 0008 | Job read vertical slice | completed | linear (pre-PR) | Public job search, detail, and requirements. |
| 0009 | Application vertical slice | completed | linear (pre-PR) | Applications, statistics, and the status state machine. |
| 0010 | Job ingestion vertical slice | completed | linear (pre-PR) | Internal batch import and `source_url` idempotency. |
| 0011 | Job search and ranking | completed | linear (pre-PR) | Hard filters, deterministic ranking, and recommendations. |
| 0012 | Source adapters and freshness | completed | linear (pre-PR) | Greenhouse/Lever/Ashby adapters and freshness lifecycle. |
| 0013 | AI matching | completed | linear (pre-PR) | Deterministic match task plus an optional AI worker. |
| 0014 | China campus sources and manual capture | completed | linear (pre-PR) | Capture framework and bookmarklet; Step 5 stayed environment-limited. |
| 0015 | Resume tailoring and explainable matching | completed | linear (pre-PR) | `rule_v1` explanations, tailoring drafts, and read-only report. |
| 0015A | Job-list usability and intake quality | completed | linear (pre-PR) | Filters, review gate, and the 48 KiB description cap. |
| 0016 | Open-source packaging | completed | linear (pre-PR) | Docker image, demo gate, and CI packaging. |
| 0017 | Ops hardening and acceptance closure | completed | linear (pre-PR) | Build artifact reuse, coverage map, preflight, and capture acceptance. |
| 0017A | Manual-import `published_at` | completed | linear (pre-PR) | Closed the manual-import `published_at` contract gap. |
| 0018 | nowcoder campus source | completed | `cc9b333` (PR #1) | First China source; bounded 20-row login-free window. |
| 0019 | ATS live smoke and ASan timeout | completed | `0c24c45` (PR #2) | Live ATS counts and sanitizer-aware HTTP timeout. |
| 0020 | `apply_url` end to end | completed | `c07c35a` (PR #3) | Nullable apply URL through schema, ingestion, API, and UI. |
| 0021 | docs/03–04 reconciliation | completed | `d75b14e` (PR #4) | Reconciled DB and API contracts with the live router/schema. |
| 0022 | Minimal Web UI | completed | `38bd484` (PR #5) | Native local UI with nginx/Python fallback and status board. |
| 0023 | Release readiness | completed | `539202f` (PR #6) | Security/contributing docs, locks, MariaDB, TSan, benchmark. |
| 0025 | README and external usage | completed | `01ad46b` (PR #8) | English/Chinese README, screenshots, and one-command demo. |
| 0028 | Usability defect fixes | completed | `c20795d` (PR #13) | Empty recommendations for new users, one skill row, UTF-8 boundaries, field-aware errors. |

- **0024** was an owner-only publication step, not a worker plan.
- **0026** was a lightweight LICENSE repair, recorded in
  [`../ai/project-state.md`](../ai/project-state.md).
- **0027** (repository hygiene and README) merged as PR #12 at `dac3490`.
- **0029** (status update) merged as PR #14 at `aed4abb`; no active code plan.

## Protocol

- `active/` would contain the only plan allowed to drive implementation; there is
  currently no active plan.
- Completed plan bodies are historical evidence and are not edited.
- The live state is [`../ai/project-state.md`](../ai/project-state.md).
- Cross-agent reading order: `AGENTS.md` → `docs/ai/README.md` →
  `docs/ai/project-state.md` → this index → targeted code/contracts.
