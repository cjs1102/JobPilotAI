# AI Agent Workflow and Verification Rules

Applies to every AI agent working in this repository (Codex, DSH, and any other
coding agent). Read this before running a test suite. `AGENTS.md` is the entry
point; this file is the binding detail for how work is planned, verified, and
reported.

## 1. Workflow stages

```text
Research -> Capability Check -> Design -> Plan (write the active ExecPlan)
  -> Implement in small verified slices -> Verify at the right level
```

- Level 1/2 edits: implement and verify directly.
- Level 3/4 work (cross-module, migration, protocol, new subsystem): follow the
  project skill `engineering-research-plan`, produce an ExecPlan under
  `docs/exec-plans/active/`, and only then edit production code.
- Never start a new stage while another plan is active.

## 2. Verification levels and WHEN to test

Testing is a cost, not a ritual. Run the **smallest set that can falsify your
change**, and run the full matrix **once per stage**, at stage close.

| Level | What it means | When to run it |
|---|---|---|
| L0 | No tests: docs, comments, formatting, config comments | `git diff --check` only |
| L1 | The one test binary that covers the touched code | After every code edit; while iterating |
| L2 | One label across the affected module, e.g. `ctest -L ai` | When a change crosses files in one module |
| L3 | The affected labels plus a compile of every touched preset target | Before committing a multi-file slice |
| L4 | Full Debug + ASan + UBSan CTest, service and root Release builds, all Python suites | **Once per stage, at close** |
| L5 | L4 plus TSan | Only when the stage adds concurrency |

Binding rules:

1. **Never re-run a suite that already passed for unchanged code.** Cite the
   prior evidence instead: the command, the raw pass line, and the commit it ran
   against. Re-running the same green suite is a process defect, not diligence.
2. **After a change, re-run only the affected minimum.** If the change touches
   only `service/src/ai/`, run the `ai` label; do not re-run `job`, `resume`,
   `user` unless their code changed too.
3. **Full L4 runs exactly once per stage**, against a frozen commit. If the code
   changes after an L4 run, only the affected levels may be re-run; a new L4 is
   justified only when production code (not docs/config comments) changed after
   that point, and the reason must be stated.
4. **Costly or non-deterministic suites are never "just re-run to be sure".**
   Flaky failures are investigated, not retried into green.
5. **Python suites are cheap** (seconds) but still follow rule 2: run the suite
   for the package you touched.

### 2.1 Change-type minimums

| Change | Minimum verification |
|---|---|
| Documentation only (production code untouched) | L0 |
| Tests only | Run the new or affected tests; do not launch full sanitizers |
| Configuration only | Run the tests whose behaviour that configuration actually affects |
| Production code | L1/L2/L3 by blast radius (see the table above) |
| Stage close | L4, once |
| New or changed concurrency that must be verified | L5 evaluation only |

### 2.2 L4 preconditions (all required)

1. The stage's production code, configuration, and any required cross-language
   E2E are finished.
2. The affected L1/L2/L3 tests already pass.
3. The verified code is frozen: nobody may edit it while the run is in progress.
4. The environment probe passes — MySQL credentials and connectivity first.
5. The build directories correspond to the current source configuration.

### 2.3 During an L4 run (exclusive)

- No other agent may build the same targets concurrently.
- No other agent may run an E2E that competes for the same database or test
  resources.
- On this dev host (4 cores, ~7.7 GB) the build and full-test phases run
  **serially** by default.
- Every phase writes its complete log to a file; the summary keeps the exit
  code, discovered test count, failure count, and the raw pass line.
- Long runtime is not evidence of a hang: check the process, resource use, and
  whether the log is still growing before concluding anything.

### 2.4 Invalidation after the code or configuration changes

- Diff the change and decide which earlier evidence it invalidates; re-run the
  smallest invalidated set first.
- Re-run a full gate only when the change affects the verification baseline as a
  whole.
- A new HEAD hash alone invalidates nothing.
- A documentation-only commit never invalidates code tests.
- A configuration change invalidates only the tests whose behaviour depends on
  that configuration.
- Record the conclusion for TSan entries in `docs/quality/README.md` (the TSan
  authority), not here.

## 3. Test environment prerequisites (harness correctness)

A failure caused by the harness is **not** a product failure; both must be
labelled explicitly in the report.

1. **Probe before a full run.** Any suite whose tests touch MySQL or Redis must
   be preceded by one credential probe, e.g.
   `MYSQL_USER=jobpilot MYSQL_PASSWORD=<pwd> ./test_user_dao --gtest_filter=UserDaoTest.CreatesAndFindsUser`.
   If the probe fails, fix the environment before starting the suite.
2. **Credentials come from the running containers**, e.g.
   `docker inspect jobpilot-mysql`. Do not guess and do not commit them.
3. **Use `127.0.0.1`, not `localhost`.** `libmysqlclient` treats `localhost` as
   a unix socket, which fails when MySQL runs in a container.
4. **Capture full ctest output to a file.** Never pipe ctest through `tail`:
   the `NN% tests passed, M tests failed out of N` line is printed before the
   label summary and gets truncated away. A missing percentage line is a capture
   artifact, not a failure signal.
5. **`LastTestsFailed.log` is only meaningful right after its own run.** CTest
   does not delete it when a later run passes, so a stale file must be reported
   as stale (with mtimes), never as a live failure.
6. **Sequential, not parallel, for ctest phases.** Overlapping sanitizer builds
   and test phases inflate runtimes and can turn timeouts into false failures.
7. **Expect long warm/cold build times.** A cold sanitizer or root Release build
   is minutes to hours on the dev host; poll detached logs rather than assuming
   a hang, and never kill a running phase.
8. **Mass failure triage.** When many tests fail at once, classify the *first
   causal error* before anything else, and separate a product defect from an
   environment, credential or configuration fault. Fix the environment first,
   then re-run only the affected tests. Indiscriminate re-runs are forbidden, and
   "retry until green" is never a fix.

## 4. Evidence and honesty

- No claim of success without the raw line that shows it (test count, pass line,
  exit status, commit SHA).
- Distinguish four outcomes in every report: **product failure**, **harness
  error**, **not verified**, **cannot verify** (with the exact blocker).
- A stage is not complete because the code is written; it is complete when the
  required level has run and its raw output is quoted.

## 5. Commits and delivery

- Commit after each verified slice, one line, Conventional Commit subject
  (`feat(ai): ...`, `docs(api): ...`, `test(job): ...`).
- Run `git diff --check` before committing. Never push unverified code.
- Update `docs/ai/project-state.md` and the active ExecPlan's progress section
  as steps land; archive the plan under `docs/exec-plans/completed/` only after
  its verification level has run.
- Contract changes (`docs/03`, `docs/04`) must be made in the contract document
  first and reviewed before code depends on them.

## 6. Parallel agents

- Split work so **write scopes are disjoint**; record the scope on the shared
  task before spawning its owner.
- The Lead owns commits, the final diff review, and the closing verification.
  Teammates and subagents never commit or push.
- Give every delegate: the authoritative files to read, the exact deliverables,
  the acceptance evidence, and the rule that a blocker must be reported as a
  blocker instead of guessed around.

## 7. Failure modes seen in this repository (do not repeat)

| Symptom | Real cause | Rule |
|---|---|---|
| ~120 DB tests fail in one preset while another preset is green | ctest ran without DB credentials | §3.1 |
| Full-suite failures reported as regressions | harness error reported as product failure | §3, §4 |
| Missing `NN% tests passed` line | ctest piped through `tail` | §3.4 |
| Stale `LastTestsFailed.log` treated as current | ctest does not clear it | §3.5 |
| Sanitizer phase "hung" | cold full-tree instrumented rebuild | §3.7 |
| MySQL "Can't connect through socket" | `localhost` instead of `127.0.0.1` | §3.3 |
| Same green suite run three times in one stage | no re-run policy | §2.1 |
