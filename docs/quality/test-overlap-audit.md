# Test Overlap Audit — 0017-B

Deliverable of ExecPlan 0017 §5.2 / §7 unit B. Remote workspace
`dev@<dev-host>:<repo>` (the private host address is redacted, as
`tools/checks/release_preflight.py` requires of every tracked file), release preset
`build/presets/release`.
The audit was computed against HEAD `a4d1f5f`; the test sources are unchanged at the current HEAD
`38f29fd` (`git log --oneline a4d1f5f..38f29fd -- service/tests backend/tests` is empty and
`git status --porcelain -- service/tests backend/tests` is empty, so every `file:line` below still
points at the same code).

Companion artifacts: [`test-merge-map.json`](test-merge-map.json) (machine-readable map) and
[`../../tools/checks/test_coverage_map.py`](../../tools/checks/test_coverage_map.py) (offline
checker). The map cannot drift from this report: the checker reads the counts block below and
the live `ctest -N` list and fails when any of the three disagree.

<!-- audit-counts: {"before":321,"after":321,"removed":0,"survivors":0,"candidates_evaluated":66,"candidate_pairs_unordered":50,"record_only":47,"cap":20} -->

## 1. Verdict

| Quantity | Value |
|---|---|
| CTest cases before the audit | **312** (309 at audit time; see the 2026-10-11 note) |
| CTest cases removed by this audit (k) | **0** |
| CTest cases after the audit | **321** (312 audit-time + 9 tests added by 0028; nothing removed) |

> **2026-10-11:** three cases were added by the `needs_review` visibility fix
> (`JobDaoTest.PublicDetailHidesQueuedRowsWhileReviewStillReadsThem`,
> `JobHttpTest.HidesQueuedJobsFromPublicDetailAndList`,
> `JobQualityTest.PublicVisibilitySwitchParsesTheDocumentedValues`), so the live
> `ctest -N` count moved 309 → 312 and this block follows it. The audit itself is
> unchanged: k = 0 removals, the same 66 evaluated pairs, the same rule. The
> transcript below is the audit-time snapshot and is deliberately left as it was.
>
> **2026-10-23:** 0028 added 9 tests (6 UTF-8 unit cases plus 3 HTTP cases),
> moving the live count 312 → 321 with no removals. The map/report counts are
> updated to 321; the original 66 evaluated pairs and k = 0 conclusion remain
> unchanged.
| Candidates mechanically/manually evaluated | 66 directed (50 unordered pairs) |
| Candidates passing all clauses of the hard merge rule | 0 |
| Candidates reported as NOT PROVEN | 1 (§4, loop-iteration equivalence) |
| Cases marked RECORD-ONLY (documented, kept) | 47 |
| **Test source files edited** | **0** (`service/tests/` and `backend/tests/` are unchanged between `a4d1f5f` and the current HEAD `38f29fd`) |
| Python test methods audited (no write scope, §6) | 636 = 446 (source_adapters) + 190 (ai_worker) |

**k = 0.** Every candidate that any detector flagged fails at least one clause of the hard rule
(§4 ledger); the single semantic near-miss is reported as **not proven**, not guessed. §9.3 of the
plan makes exactly that the prescribed outcome for an unprovable candidate ("该候选改为仅记录"),
and §5.2 B3 forbids the two ways one could manufacture a merge anyway (cross-binary refactoring,
folding cases into a parameterized gtest).

**No test source file was edited, so no test binary had to be rebuilt or re-run.** With k = 0 the
acceptance steps "rebuilt affected binaries pass" and "`ctest -N` shows 309 - k" are satisfied by
309 → 309 without a build; the stage's single L4 (§8.3 of the plan) re-runs the
whole 309-case suite in debug/asan/ubsan once the stage is frozen, and re-running an
unchanged suite before that would be a process defect
([`../ai/agent-workflow.md`](../ai/agent-workflow.md) §2 rules 1-2).

## 2. Method

### 2.1 Inventory — exact commands and raw tails

```text
$ ctest --test-dir build/presets/release -N | tail -2
  Test #309: basic_test

Total Tests: 309
$ ctest --test-dir build/presets/release -N | grep -c 'Test #'
309
$ ctest --test-dir build/presets/release --show-only=json-v1 > /tmp/audit17/ctest.json
```

The JSON form reports 309 cases over **57 distinct `command[0]` paths = 56 `test_*` binaries +
`jobpilot_test`**, and the label set quoted by ExecPlan §2 (`ai application auth concurrency
external integration job resume tsan user`; 17 `external`, 2 `integration`, 59 unlabelled).

### 2.2 Case → binary → source mapping

* 289 of the 309 cases are gtest cases in 37 files under `service/tests/` and `backend/tests/`
  (33 service binaries, 24 backend binaries). They were extracted by a C++-aware scanner that
  skips comments, string/char literals and raw string literals before matching
  `TEST` / `TEST_F` and locating the matching brace, so braces inside JSON fixtures
  (e.g. `service/tests/test_ai_http.cpp:1022`) cannot truncate a body.
* The remaining 20 cases are one case per binary for the 19 backend tests with a plain `main()`
  (`test_bcrypt`, `test_buffer`, `test_buffer_sigpipe`, `test_event_loop`, `test_file_storage`,
  `test_http_request`, `test_http_response`, `test_http_router`, `test_http_server`,
  `test_multipart_form_data_parser`, `test_mysql_pool`, `test_redis_client`,
  `test_tcp_connection`, `test_tcp_connection_hangup`, `test_tcp_connection_peer_close`,
  `test_tcp_connection_send`, `test_tcp_connection_write_buffer`, `test_tcp_server`,
  `test_thread_pool`) plus `basic_test` from `service/tests/test_basic.cpp`.
  289 + 20 = 309 exactly; the tree contains no `TEST_P`, `TYPED_TEST` or macro-generated cases,
  so no case is hidden from the mapping. Appendix A is the per-binary inventory; Appendix B is the
  complete case → `file:line` index.
* Every test target has exactly one source file (`add_executable` … `tests/test_x.cpp` in
  `service/CMakeLists.txt` and `backend/CMakeLists.txt`; verified for all 33 + 24 test targets), and
  each CTest case resolves to exactly one binary via `--show-only=json-v1`. Every candidate below
  is therefore intra-binary by construction.

### 2.3 Candidate detectors (four mechanical filters + one manual sweep)

Every ordered pair of cases sharing a binary (1,533 unordered pairs) was tested with:

1. **verbatim containment** — the removed case's whole statement sequence occurs in the survivor's;
2. **normalized assertion-set containment** — string/number literals collapsed, the loose detector;
3. **identical non-assertion statement sets** — the two cases drive the same input statements;
4. **shape containment** — all literals collapsed: same call shapes, different constants.

Raw hits: filter 1 → **0**; filter 2 → 45 directed hits; filter 3 → 2 unordered pairs; filter 4 → 2
directed hits. Union after de-duplication: **65 directed candidate pairs (49 unordered)**.

Because filters 1-4 are blind to a *scalar* case that another case re-creates inside a loop, a
fifth, manual sweep was run: every case containing a `for` loop whose normalized assertion shape
matches a single-assertion case in the same file (1 raw hit, reviewed by hand). It produced the one
entry in §4 that no mechanical filter could flag — `SourceObservationValidation.
AcceptsCompleteObservationWithJobs` vs `AcceptsAllSupportedSources` — which is reported as
**NOT PROVEN** and kept. Total ledger: **66 directed candidates
(50 unordered)**.

Statistics over the 1,533 pairs:
* pairs whose non-assertion statement sets are *identical*: **2** (both are sister boundary cases
  with disjoint assertions — `ConfigTest.LoadsDefaults`/`ConfigTest.LoadsAiDefaults` and
  `SqlResultTest.ThrowsOnInvalidRowIndex`/`SqlResultTest.ThrowsOnInvalidColumnIndex`);
* pairs where one case's assertions *and* inputs are both verbatim contained in the other: **0**;
* maximum pairwise similarity (0.6·input-Jaccard + 0.4·assertion-Jaccard): **0.700**, the
  `HttpCorrectnessTest` duplicate-`Content-Length` pair, whose assertion sets are *equal*, not
  contained.

### 2.4 Decision protocol

For every detector hit both bodies were read in full and the candidate was checked against the
clauses of the hard rule: (a) same test binary, (b) same input/fixture, (c) the survivor's
assertion set *strictly contains* the removed case's assertions on the same target. A candidate
that fails any clause is KEEP and appears in §4 with the clause it failed. Nothing was merged on
similarity, naming, or intent.

### 2.5 Limits of the method (what is *not* claimed)

* The detectors are lexical/structural, not semantic. A case pair that a human would call
  "the same test with a different constant" still fails clause (b) and stays KEEP; that is the
  rule, not an oversight. The one pair where a human could argue loop-iteration equivalence is
  listed as NOT PROVEN rather than merged.
* 14 cases assert only inside a loop or a helper; their in-loop assertion was recovered by reading
  the source (Appendix B marks them and their assertion is quoted in §3). None of them is a
  candidate.
* 20 plain-`main()` binaries have exactly one CTest case each, so intra-binary overlap is
  impossible there by construction; backend `test_config`, `test_schema`, `test_sql_result`,
  `test_logger` and `test_http_correctness` are the only multi-case backend binaries.
* No cross-binary pair was considered for removal, per the hard rule.

## 3. Overlap by assertion target

Groups below are the assertion targets with **more than one covering case** — module/contract
invariant first (G1-G9), then the boundary families (G10-G13). Each row carries the case, its
layer, the `file:line` of the `TEST`/`TEST_F` and the line of the quoted assertion, and the
assertion text verbatim. Layer vocabulary: `backend/core` (backend component test), `rule` (pure
function/policy), `dao`, `service`, `http`, `e2e` (`test_source_sync_e2e`), `smoke`
(`basic_test`). "RECORD-ONLY" means the overlap is real and documented but no action is taken in
this stage (see §5); "KEEP" means the case covers a target the others do not.

Cross-layer duplication is present in G1-G8 and is deliberate: the same contract is pinned once
at the pure/DAO/service layer and once at the HTTP boundary, and the plan's §5.2 B3 forbids
collapsing those layers into one case.

| Group | Assertion target | Cases | Layers | RECORD-ONLY |
|---|---|---|---|---|
| G1 | Session / bearer authentication at every HTTP boundary | 14 | http, rule, service | 0 |
| G2 | 0014 deadline kinds + extra-field domains (parse / validate / persist) | 19 | backend/core, dao, http, rule, service | 0 |
| G3 | 0015A review gate (quality findings / needs_review visibility) | 14 | backend/core, dao, http, rule, service | 0 |
| G4 | Freshness / disappearance / close policy | 22 | dao, e2e, http, rule, service | 10 |
| G5 | AI task lifecycle: claim / lease / retry / ownership / rule-result preservation | 23 | dao, http, service | 0 |
| G6 | Idempotency: source_url / task triple / duplicate create | 11 | dao, http, service | 0 |
| G7 | Per-user ownership isolation | 11 | dao, http, service | 0 |
| G8 | Job search: filter normalization / ranking / visibility response fields | 15 | dao, http, service | 0 |
| G9 | Concurrency: duplicate-create / claim exactly once | 4 | dao, service | 0 |
| G10 | Schema / DDL invariants (backend test_schema) | 15 | backend/core | 0 |
| G11 | HTTP framing correctness (backend test_http_correctness) | 4 | backend/core | 2 |
| G12 | Source observation validation boundaries (shared MakeObservation fixture) | 20 | rule | 20 |
| G13 | Config defaults and rejection boundaries (shared BaseEnvironment fixture) | 15 | backend/core | 15 |
<!-- GENERATED-BEGIN groups -->

### G1. Session / bearer authentication at every HTTP boundary

Group size: **14** cases; layers: http, rule, service; recommendation: KEEP (no subsumption proven).

| Case | Layer | file:line | assertion (verbatim, truncated) | Recommendation |
|---|---|---|---|---|
| `AiHttpTest.PublicEndpointsRequireBearerAuthentication` | http | `service/tests/test_ai_http.cpp:583` L605 | `EXPECT_EQ(invalid->jsonBody["code"], 20001)` | KEEP |
| `AiHttpTest.RejectsMissingOrWrongInternalToken` | http | `service/tests/test_ai_http.cpp:873` L906 | `EXPECT_EQ(inputMissing->jsonBody["code"], 20001)` | KEEP |
| `JobIngestionHttpTest.RejectsAuthenticationAndInvalidRequests` | http | `service/tests/test_job_ingestion_http.cpp:504` L589 | `EXPECT_EQ(batchTooLarge->jsonBody["code"], 10001)` | KEEP |
| `JobReviewHttpTest.RejectsMissingOrWrongToken` | http | `service/tests/test_job_review_http.cpp:502` L511 | `EXPECT_EQ(queue->jsonBody["code"], 20001)` | KEEP |
| `ResumeHttpTest.RejectsAuthenticationAndMalformedInput` | http | `service/tests/test_resume_http.cpp:731` L739 | `EXPECT_EQ(unauthenticated->statusCode, 401)` | KEEP |
| `SessionAuthenticatorTest.ResolvesSessionUserId` | rule | `service/tests/test_session_authenticator.cpp:48` L61 | `EXPECT_EQ(result.value, 42U)` | KEEP |
| `SessionAuthenticatorTest.RejectsMissingSession` | rule | `service/tests/test_session_authenticator.cpp:65` L74 | `EXPECT_EQ( missing.code, ErrorCode::kUnauthorized)` | KEEP |
| `SessionAuthenticatorTest.ParsesBearerHeader` | rule | `service/tests/test_session_authenticator.cpp:81` L87 | `EXPECT_EQ(token.value(), "abc123")` | KEEP |
| `SourceSyncHttpTest.RejectsMissingOrWrongToken` | http | `service/tests/test_source_sync_http.cpp:493` L516 | `EXPECT_EQ(missing->jsonBody["code"], 20001)` | KEEP |
| `UserHttpTest.RegistersLogsInAndReturnsCurrentUser` | http | `service/tests/test_user_http.cpp:481` L519 | `EXPECT_EQ( login->jsonBody["data"]["user_id"], userId)` | KEEP |
| `UserHttpTest.ReturnsLockedBusinessErrors` | http | `service/tests/test_user_http.cpp:549` L599 | `EXPECT_EQ( unauthorized->jsonBody["code"], 20001)` | KEEP |
| `UserServiceTest.LogsInAndCreatesSession` | service | `service/tests/test_user_service.cpp:188` L221 | `EXPECT_EQ( sessionUser.value(), registration.value)` | KEEP |
| `UserServiceTest.ReturnsCurrentUserAndRejectsInvalidToken` | service | `service/tests/test_user_service.cpp:242` L273 | `EXPECT_EQ( service->GetCurrentUser( "invalid-token") .code, ErrorCode::kUnauthorized)` | KEEP |
| `UserServiceTest.RejectsExpiredSession` | service | `service/tests/test_user_service.cpp:282` L312 | `EXPECT_EQ( shortService.GetCurrentUser( login.value.token) .code, ErrorCode::kUnauthorized)` | KEEP |

### G2. 0014 deadline kinds + extra-field domains (parse / validate / persist)

Group size: **19** cases; layers: backend/core, dao, http, rule, service; recommendation: KEEP (no subsumption proven).

| Case | Layer | file:line | assertion (verbatim, truncated) | Recommendation |
|---|---|---|---|---|
| `SchemaTest.Creates0014JobColumnsAndNoDeferredIndex` | backend/core | `backend/tests/test_schema.cpp:564` L606 | `EXPECT_NE( names.find("idx_source_scope_status"), names.end())` | KEEP |
| `SchemaTest.LegacyJobRowReadsWithNull0014Fields` | backend/core | `backend/tests/test_schema.cpp:612` L652 | `EXPECT_EQ(cleanup.affectedRows, 1U)` | KEEP |
| `DeadlineTime.ConvertsBetweenStoredUtcAndBeijing` | rule | `service/tests/test_deadline_time.cpp:36` L44 | `EXPECT_EQ( DeadlineToBeijingLocal("2026-10-08 04:00:00"), std::optional<std::string>("2026-10-08 12:00:00"))` | KEEP |
| `DeadlineTime.RoundTripsDateOnlyValuesWithoutShifting` | rule | `service/tests/test_deadline_time.cpp:56` L72 | `ASSERT_TRUE(stored.has_value()) << date; EXPECT_TRUE(IsDateOnlyStoredDeadline(stored.value())) << date;` | KEEP |
| `DeadlineTime.AppliesDayMonthAndYearBoundaries` | rule | `service/tests/test_deadline_time.cpp:84` L89 | `EXPECT_EQ( DeadlineToBeijingDate("2026-10-07 16:00:00"), std::optional<std::string>("2026-10-08"))` | KEEP |
| `DeadlineTime.RejectsUnrepresentableOrInvalidInputs` | rule | `service/tests/test_deadline_time.cpp:127` L132 | `EXPECT_FALSE( DeadlineToBeijingLocal("2026-10-08").has_value())` | KEEP |
| `DeadlineTime.RecognisesOnlyTheNoonStorageConvention` | rule | `service/tests/test_deadline_time.cpp:143` L146 | `EXPECT_FALSE( IsDateOnlyStoredDeadline("2026-10-08 03:59:59"))` | KEEP |
| `DeadlineTime.ConversionDoesNotUseTheServerTimeZone` | rule | `service/tests/test_deadline_time.cpp:156` L174 | `EXPECT_EQ( DeadlineToBeijingLocal(utc), std::optional<std::string>("2026-10-08 00:00:00"))` | KEEP |
| `DeadlineValidation.AcceptsBothKindsAndPreservesExplicitTime` | rule | `service/tests/test_deadline_time.cpp:191` L205 | `EXPECT_EQ( DeadlineToBeijingLocal( explicitTime.deadline.value()), std::optional<std::string>("2026-10-31 18:30:00"))` | KEEP |
| `DeadlineValidation.RejectsMismatchedOrMalformedDeadlines` | rule | `service/tests/test_deadline_time.cpp:217` L226 | `EXPECT_FALSE(ValidateJobExtraFields(flagWithoutDeadline))` | KEEP |
| `DeadlineValidation.EnforcesEnumAndRangeDomains` | rule | `service/tests/test_deadline_time.cpp:240` L276 | `EXPECT_FALSE(ValidateJobExtraFields(overYears))` | KEEP |
| `DeadlineNormalization.DefaultsOnlyNextToAPresentDeadline` | rule | `service/tests/test_deadline_time.cpp:290` L296 | `EXPECT_FALSE( explicitDeadline.deadlineDateOnly.value())` | KEEP |
| `JobDaoTest.ReadsBackThe0014FieldsForBothDeadlineKinds` | dao | `service/tests/test_job_dao.cpp:680` L756 | `EXPECT_EQ( explicitDetail->summary.deadline, std::optional<std::string>("2026-10-31 10:30:00"))` | KEEP |
| `JobIngestionHttpTest.Validates0014ExtraFieldValues` | http | `service/tests/test_job_ingestion_http.cpp:786` L872 | `EXPECT_EQ(statusFor(flagWithoutDeadline), 400)` | KEEP |
| `JobIngestionHttpTest.RoundTripsBothDeadlineKindsThroughApi` | http | `service/tests/test_job_ingestion_http.cpp:901` L1092 | `EXPECT_EQ( explicitDetail->jsonBody["data"]["deadline"], "2026-10-31 18:30:00")` | KEEP |
| `JobIngestionServiceTest.ValidatesThe0014ExtraFields` | service | `service/tests/test_job_ingestion_service.cpp:385` L409 | `EXPECT_EQ( service.Import({explicitTime}).code, ErrorCode::kSuccess)` | KEEP |
| `SourceSyncDaoTest.KeepsObserved0014FieldsWhenALaterFetchOmitsThem` | dao | `service/tests/test_source_sync_dao.cpp:639` L696 | `EXPECT_EQ(Text(replacedStored, 0, 0), "2026-12-24 10:30:00")` | KEEP |
| `SourceSyncHttpTest.ParsesAndRejectsThe0014ExtraFields` | http | `service/tests/test_source_sync_http.cpp:929` L1006 | `EXPECT_EQ(statusFor(educationOutOfRange), 400)` | KEEP |
| `SourceSyncHttpTest.KeepsObservedDeadlineWhenANewFetchOmitsIt` | http | `service/tests/test_source_sync_http.cpp:1037` L1088 | `EXPECT_EQ( jobpilot::test::RequireValue(stored.value(0, 0)), "2026-10-31 04:00:00")` | KEEP |

### G3. 0015A review gate (quality findings / needs_review visibility)

Group size: **14** cases; layers: backend/core, dao, http, rule, service; recommendation: KEEP (no subsumption proven).

| Case | Layer | file:line | assertion (verbatim, truncated) | Recommendation |
|---|---|---|---|---|
| `SchemaTest.StoresTheFullReviewOutcomeVocabulary` | backend/core | `backend/tests/test_schema.cpp:256` L266 | `EXPECT_EQ(RequireValue(result.value(0, 0)), "varchar(24)")` | KEEP |
| `JobDaoTest.FiltersAndSortsByThe0015AFocusFields` | dao | `service/tests/test_job_dao.cpp:217` L335 | `EXPECT_EQ(educationAscRows[0].id, socialId)` | KEEP |
| `JobDaoTest.PublicVisibilityExcludesNeedsReviewJobs` | dao | `service/tests/test_job_dao.cpp:822` L859 | `EXPECT_EQ(rankedVisible[0].id, cleanId)` | KEEP |
| `JobHttpTest.AppliesFreshnessVisibilityAndResponseFields` | http | `service/tests/test_job_http.cpp:980` L1085 | `EXPECT_TRUE( nullUrlSearch->jsonBody["data"]["list"][0] ["source_url"] .is_null())` | KEEP |
| `JobIngestionServiceTest.RecordsFindingsAndReviewStatus` | service | `service/tests/test_job_ingestion_service.cpp:476` L564 | `EXPECT_EQ( platformItems.value(0, 1).value(), "needs_review")` | KEEP |
| `JobIngestionServiceTest.RegistryIsFailOpenAndAdvisoryStaysVisible` | service | `service/tests/test_job_ingestion_service.cpp:694` L710 | `EXPECT_EQ( unknownResult.value.quality.needsReview, 0U)` | KEEP |
| `JobQualityTest.MissingCompanyGoesToReviewNotReject` | rule | `service/tests/test_job_quality.cpp:91` L101 | `EXPECT_EQ(report.reviewStatus, 1)` | KEEP |
| `JobQualityTest.NonHttpSourceUrlIsRejected` | rule | `service/tests/test_job_quality.cpp:106` L114 | `EXPECT_EQ(report.diagnosticCode, "source_url_unusable")` | KEEP |
| `JobQualityTest.DescriptionOver48KiBIsRejectedNotTruncated` | rule | `service/tests/test_job_quality.cpp:118` L131 | `EXPECT_EQ(overCap.diagnosticCode, "description_too_large")` | KEEP |
| `JobQualityTest.RootOrListUrlGoesToReviewButStaysStored` | rule | `service/tests/test_job_quality.cpp:135` L143 | `EXPECT_EQ(report.reviewStatus, 1)` | KEEP |
| `JobQualityTest.PlatformEmployerWithConfirmingHostGoesToReview` | rule | `service/tests/test_job_quality.cpp:152` L166 | `EXPECT_EQ(confirmed.reviewStatus, 1)` | KEEP |
| `JobQualityTest.TitleEqualsCompanyGoesToReview` | rule | `service/tests/test_job_quality.cpp:217` L223 | `EXPECT_EQ(report.reviewStatus, 1)` | KEEP |
| `JobReviewHttpTest.QueueListsReasonStatusAndIdentifiers` | http | `service/tests/test_job_review_http.cpp:547` L571 | `EXPECT_GE(response->jsonBody["total"].get<std::size_t>(), 1U)` | KEEP |
| `JobReviewHttpTest.ApproveMakesTheRowVisibleAndIsIdempotent` | http | `service/tests/test_job_review_http.cpp:613` L640 | `EXPECT_EQ(approved->jsonBody["review_status"], 0)` | KEEP |

### G4. Freshness / disappearance / close policy

Group size: **22** cases; layers: dao, e2e, http, rule, service; recommendation: RECORD-ONLY for the listed sibling family, KEEP otherwise.

| Case | Layer | file:line | assertion (verbatim, truncated) | Recommendation |
|---|---|---|---|---|
| `JobDaoTest.AppliesFreshnessVisibilityAndFields` | dao | `service/tests/test_job_dao.cpp:536` L673 | `EXPECT_EQ( dao->SearchJobs(filter, 20, 0).size(), 2U)` | KEEP |
| `FreshnessClassification.AppliesPolicyBoundaries` | rule | `service/tests/test_source_observation.cpp:482` L490 | `EXPECT_EQ( ClassifyFreshness(false, kFreshMaxAgeSeconds + 1), std::optional<FreshnessState>(FreshnessState::kAging))` | KEEP |
| `FreshnessClassification.HandlesClosedAndUnobservedJobs` | rule | `service/tests/test_source_observation.cpp:505` L506 | `EXPECT_EQ( ClassifyFreshness(true, std::nullopt), std::optional<FreshnessState>(FreshnessState::kClosed))` | KEEP |
| `MissingClosePolicy.AppliesCountAndGraceThresholds` | rule | `service/tests/test_source_observation.cpp:524` L530 | `EXPECT_TRUE( ShouldCloseMissingJob(1, kDefaultGraceSeconds + 1))` | KEEP |
| `MissingClosePolicy.HonorsCustomPolicy` | rule | `service/tests/test_source_observation.cpp:538` L543 | `EXPECT_FALSE(ShouldCloseMissingJob(0, 3600, custom))` | KEEP |
| `SourceSyncDaoTest.CompleteAbsenceMarksMissingWithoutClosing` | dao | `service/tests/test_source_sync_dao.cpp:245` L264 | `EXPECT_EQ(Text(one, 0, 2), "missing")` | RECORD-ONLY |
| `SourceSyncDaoTest.SecondCompleteAbsenceCloses` | dao | `service/tests/test_source_sync_dao.cpp:271` L295 | `EXPECT_EQ(Text(one, 0, 2), "closed")` | RECORD-ONLY |
| `SourceSyncDaoTest.GraceWindowClosesOnFirstAbsence` | dao | `service/tests/test_source_sync_dao.cpp:301` L318 | `EXPECT_EQ(Text(one, 0, 2), "closed")` | RECORD-ONLY |
| `SourceSyncDaoTest.GraceBoundaryAtFortyEightHoursDoesNotClose` | dao | `service/tests/test_source_sync_dao.cpp:324` L341 | `EXPECT_EQ(Text(one, 0, 2), "missing")` | RECORD-ONLY |
| `SourceSyncDaoTest.IncompleteObservationIsIgnored` | dao | `service/tests/test_source_sync_dao.cpp:347` L359 | `EXPECT_EQ(result.missing, 0U)` | RECORD-ONLY |
| `SourceSyncDaoTest.IncompleteObservationDoesNotAdvanceMissingCount` | dao | `service/tests/test_source_sync_dao.cpp:373` L391 | `EXPECT_EQ(incomplete.missing, 0U)` | RECORD-ONLY |
| `SourceSyncDaoTest.ReappearanceReopensAndResetsCounters` | dao | `service/tests/test_source_sync_dao.cpp:404` L409 | `ASSERT_EQ( CloseUnseen( MakeObservation(scopeA, "2026-10-01 01:00:00")) .closed, 0U)` | RECORD-ONLY |
| `SourceSyncDaoTest.ScopeIsolationPreventsClosingOtherBoards` | dao | `service/tests/test_source_sync_dao.cpp:443` L472 | `EXPECT_EQ(Text(one, 0, 2), "closed")` | RECORD-ONLY |
| `SourceSyncDaoTest.DoesNotTouchOtherSourcesOrManualJobs` | dao | `service/tests/test_source_sync_dao.cpp:484` L502 | `EXPECT_EQ(result.missing, 0U)` | RECORD-ONLY |
| `SourceSyncDaoTest.ManualRecordIsUnchangedByACollidingObservation` | dao | `service/tests/test_source_sync_dao.cpp:522` L624 | `EXPECT_EQ(closed.missing, 0U)` | RECORD-ONLY |
| `SourceSyncE2ETest.SyncVisibilityDisappearanceAndReopen` | e2e | `service/tests/test_source_sync_e2e.cpp:507` L631 | `EXPECT_EQ( afterFirstMiss->jsonBody["data"]["list"][0]["source_status"], "missing")` | KEEP |
| `SourceSyncHttpTest.ClosesAfterGraceWindowWhenBoardIsEmpty` | http | `service/tests/test_source_sync_http.cpp:833` L868 | `EXPECT_EQ(grace->jsonBody["closed"], 1)` | KEEP |
| `SourceSyncHttpTest.KeepsOtherScopesUntouched` | http | `service/tests/test_source_sync_http.cpp:880` L916 | `EXPECT_EQ(firstMiss->jsonBody["closed"], 0)` | KEEP |
| `SourceSyncHttpTest.ManualRecordWinsOverObservation` | http | `service/tests/test_source_sync_http.cpp:1100` L1191 | `EXPECT_EQ( jobpilot::test::RequireValue(stored.value(0, 7)), "2026-12-01 04:00:00")` | KEEP |
| `SourceSyncServiceTest.IncompleteObservationNeverCloses` | service | `service/tests/test_source_sync_service.cpp:262` L278 | `EXPECT_EQ(result.value.closed, 0U)` | KEEP |
| `SourceSyncServiceTest.EmptyCompleteBoardStillRunsClosePass` | service | `service/tests/test_source_sync_service.cpp:286` L302 | `EXPECT_EQ(result.value.closed, 0U)` | KEEP |
| `SourceSyncServiceTest.RejectsWriteNothingAndPreserveManualPriority` | service | `service/tests/test_source_sync_service.cpp:389` L447 | `EXPECT_EQ( jobpilot::test::RequireValue(manual.value(0, 1)), "Manual Job")` | KEEP |

### G5. AI task lifecycle: claim / lease / retry / ownership / rule-result preservation

Group size: **23** cases; layers: dao, http, service; recommendation: KEEP (no subsumption proven).

| Case | Layer | file:line | assertion (verbatim, truncated) | Recommendation |
|---|---|---|---|---|
| `AiHttpTest.EmptyQueueClaimReturnsNull` | http | `service/tests/test_ai_http.cpp:910` L922 | `EXPECT_TRUE(claimed->jsonBody["task"].is_null())` | KEEP |
| `AiHttpTest.RunsTheWorkerLifecycleOverHttp` | http | `service/tests/test_ai_http.cpp:928` L956 | `EXPECT_EQ(claimed->jsonBody["task"]["retry_count"], 1)` | KEEP |
| `AiHttpTest.RetryableFailRequeuesAndPermanentFailTerminates` | http | `service/tests/test_ai_http.cpp:1104` L1150 | `EXPECT_EQ(reclaimed->jsonBody["task"]["retry_count"], 2)` | KEEP |
| `AiHttpTest.ClampsRequestedLeaseSeconds` | http | `service/tests/test_ai_http.cpp:1254` L1289 | `ASSERT_TRUE(minimal->jsonBody["task"].is_object())` | KEEP |
| `AiTaskDaoTest.CreatesPendingAndResolvedTasks` | dao | `service/tests/test_ai_task_dao.cpp:146` L175 | `EXPECT_EQ( resolvedRecord->status, AiTaskStatus::kSuccess)` | KEEP |
| `AiTaskDaoTest.EnforcesTaskOwnership` | dao | `service/tests/test_ai_task_dao.cpp:183` L199 | `EXPECT_FALSE( dao->FindOwnedTask( otherUser.value(), taskId.value()) .has_value())` | KEEP |
| `AiTaskDaoTest.ReusesOnlyOpenJobMatchTask` | dao | `service/tests/test_ai_task_dao.cpp:213` L238 | `EXPECT_EQ(open->id, pendingId.value())` | KEEP |
| `AiTaskDaoTest.ClaimsOldestPendingTaskAndHoldsLease` | dao | `service/tests/test_ai_task_dao.cpp:265` L276 | `EXPECT_EQ(claimed.task->retryCount, 1)` | KEEP |
| `AiTaskDaoTest.NeverHandsTheSameTaskToTwoWorkers` | dao | `service/tests/test_ai_task_dao.cpp:296` L303 | `ASSERT_TRUE(CreateTask(false).has_value()); } ` | KEEP |
| `AiTaskDaoTest.ReclaimsExpiredLeaseForAnotherWorker` | dao | `service/tests/test_ai_task_dao.cpp:367` L387 | `EXPECT_EQ(recovered.task->retryCount, 2)` | KEEP |
| `AiTaskDaoTest.FailsExpiredLeaseBeyondAttemptCap` | dao | `service/tests/test_ai_task_dao.cpp:399` L412 | `EXPECT_EQ(first.task->retryCount, 1)` | KEEP |
| `AiTaskDaoTest.RequeuesRetryableFailureBelowTheCap` | dao | `service/tests/test_ai_task_dao.cpp:431` L463 | `EXPECT_EQ(again.task->retryCount, 2)` | KEEP |
| `AiTaskDaoTest.FailsPermanentFailureImmediately` | dao | `service/tests/test_ai_task_dao.cpp:467` L488 | `EXPECT_EQ(failed->status, AiTaskStatus::kFailed)` | KEEP |
| `AiTaskDaoTest.CompletionRequiresOwnerAndLiveLease` | dao | `service/tests/test_ai_task_dao.cpp:495` L509 | `EXPECT_FALSE( dao->CompleteTask( wrongOwner, taskId.value(), "worker-b", "{\"score\": 90}"))` | KEEP |
| `AiTaskDaoTest.CreateJobMatchTaskIfAbsentIsIdempotent` | dao | `service/tests/test_ai_task_dao.cpp:565` L578 | `EXPECT_EQ(first->status, AiTaskStatus::kPending)` | KEEP |
| `AiTaskDaoTest.CreateJobMatchTaskIfAbsentResolvesNow` | dao | `service/tests/test_ai_task_dao.cpp:608` L621 | `EXPECT_EQ(resolved->status, AiTaskStatus::kSuccess)` | KEEP |
| `AiTaskDaoTest.ClaimNextTaskOwnsItsTransaction` | dao | `service/tests/test_ai_task_dao.cpp:635` L647 | `EXPECT_EQ(claimed.task->retryCount, 1)` | KEEP |
| `AiTaskDaoTest.CompleteOwnedTaskEnforcesOwnerAndKeepsRuleResult` | dao | `service/tests/test_ai_task_dao.cpp:672` L695 | `EXPECT_EQ(completed->status, AiTaskStatus::kSuccess)` | KEEP |
| `AiTaskDaoTest.FailOwnedTaskRequeuesTerminatesAndRefusesOwner` | dao | `service/tests/test_ai_task_dao.cpp:712` L748 | `EXPECT_EQ(reclaimed.task->retryCount, 2)` | KEEP |
| `AiTaskServiceTest.ClaimDelegatesAndAppliesConfiguredPolicy` | service | `service/tests/test_ai_task_service.cpp:1077` L1157 | `EXPECT_EQ(claimed.value.task->retryCount, 1)` | KEEP |
| `AiTaskServiceTest.CompleteAndFailDelegateWithOwnerRefusal` | service | `service/tests/test_ai_task_service.cpp:1163` L1210 | `EXPECT_EQ( aiTaskDao.capturedFailPolicy.leaseSeconds, 30)` | KEEP |
| `AiTaskServiceTest.BuildsTheWorkerInputBundle` | service | `service/tests/test_ai_task_service.cpp:1286` L1322 | `EXPECT_EQ( aiTaskDao.capturedClaimedWorkerId, "worker-a")` | KEEP |
| `AiTaskServiceTest.RefusesInputWithoutALiveLease` | service | `service/tests/test_ai_task_service.cpp:1394` L1416 | `EXPECT_EQ( service.BuildTaskInput("worker-b", 1024).code, ErrorCode::kAiTaskNotFound)` | KEEP |

### G6. Idempotency: source_url / task triple / duplicate create

Group size: **11** cases; layers: dao, http, service; recommendation: KEEP (no subsumption proven).

| Case | Layer | file:line | assertion (verbatim, truncated) | Recommendation |
|---|---|---|---|---|
| `AiHttpTest.CreatesResolvedTaskAndIsIdempotent` | http | `service/tests/test_ai_http.cpp:678` L757 | `EXPECT_FALSE(data["updated_at"].is_null())` | KEEP |
| `AiHttpTest.ReusesTheOpenTaskOverHttp` | http | `service/tests/test_ai_http.cpp:761` L775 | `EXPECT_EQ( reused->jsonBody["data"]["task_id"] .get<std::uint64_t>(), pendingId)` | KEEP |
| `AiTaskDaoTest.ReusesOnlyOpenJobMatchTask` | dao | `service/tests/test_ai_task_dao.cpp:213` L238 | `EXPECT_EQ(open->id, pendingId.value())` | KEEP |
| `AiTaskDaoTest.CreateJobMatchTaskIfAbsentIsIdempotent` | dao | `service/tests/test_ai_task_dao.cpp:565` L595 | `EXPECT_TRUE(JsonEquals(reused->ruleResult, "{\"score\":1}"))` | KEEP |
| `AiTaskServiceTest.ReusesTheOpenTaskForTheSameTriple` | service | `service/tests/test_ai_task_service.cpp:959` L989 | `EXPECT_EQ(aiTaskDao.reuseCalls, 1U)` | KEEP |
| `ApplicationServiceTest.ValidatesRelationshipsAndDuplicate` | service | `service/tests/test_application_service.cpp:160` L174 | `EXPECT_EQ( service->Create( userId, activeJob, resume.versionId) .code, ErrorCode::kDuplicateApplication)` | KEEP |
| `JobIngestionDaoTest.UpdatesExistingJobByIdempotentSourceUrl` | dao | `service/tests/test_job_ingestion_dao.cpp:114` L183 | `EXPECT_EQ(job->summary.company, "Updated Company")` | KEEP |
| `JobIngestionHttpTest.ImportsIdempotentlyAndCompletesClosure` | http | `service/tests/test_job_ingestion_http.cpp:616` L713 | `EXPECT_EQ( updatedDetail->jsonBody["data"]["requirements"][1] ["skill"], "PostgreSQL")` | KEEP |
| `JobIngestionServiceTest.CreatesUpdatesAndReplacesRequirements` | service | `service/tests/test_job_ingestion_service.cpp:274` L294 | `EXPECT_EQ(created.value.updated, 0U)` | KEEP |
| `JobReviewHttpTest.ApproveMakesTheRowVisibleAndIsIdempotent` | http | `service/tests/test_job_review_http.cpp:613` L642 | `EXPECT_EQ(approved->jsonBody["idempotent"], false)` | KEEP |
| `UserDaoTest.RejectsDuplicateEmail` | dao | `service/tests/test_user_dao.cpp:86` L93 | `EXPECT_THROW( dao->CreateUser( "second", email, "hash-two"), std::exception)` | KEEP |

### G7. Per-user ownership isolation

Group size: **11** cases; layers: dao, http, service; recommendation: KEEP (no subsumption proven).

| Case | Layer | file:line | assertion (verbatim, truncated) | Recommendation |
|---|---|---|---|---|
| `AiHttpTest.HidesForeignTasksBehindTheSameNotFoundCode` | http | `service/tests/test_ai_http.cpp:816` L842 | `EXPECT_EQ(foreign->jsonBody["code"], 50001)` | KEEP |
| `AiTaskDaoTest.EnforcesTaskOwnership` | dao | `service/tests/test_ai_task_dao.cpp:183` L199 | `EXPECT_FALSE( dao->FindOwnedTask( otherUser.value(), taskId.value()) .has_value())` | KEEP |
| `AiTaskServiceTest.OwnershipRefusalUsesOneNotFoundCode` | service | `service/tests/test_ai_task_service.cpp:1000` L1035 | `EXPECT_EQ( service.GetOwnedTask(kUserId, 2048).code, ErrorCode::kAiTaskNotFound)` | KEEP |
| `ApplicationHttpTest.RejectsAuthRelationshipsAndTransitions` | http | `service/tests/test_application_http.cpp:573` L624 | `EXPECT_EQ(resumeError->jsonBody["code"], 40002)` | KEEP |
| `ApplicationServiceTest.ListsAndAggregatesOwnedApplications` | service | `service/tests/test_application_service.cpp:301` L343 | `EXPECT_EQ(statistics.value.interview, 1U)` | KEEP |
| `ResumeArtifactDaoTest.OwnershipRejectsForeignVersionAndApplication` | dao | `service/tests/test_resume_artifact_dao.cpp:263` L292 | `EXPECT_EQ( artifactDao->CheckOwnership( transaction, userId, versionId, applicationId), ArtifactOwnership::kOk)` | KEEP |
| `ResumeDaoTest.ListsOnlyOwnedResumeVersions` | dao | `service/tests/test_resume_dao.cpp:160` L212 | `EXPECT_FALSE( resumeDao ->FindOwnedResume( secondUserId.value(), resumeId) .has_value())` | KEEP |
| `ResumeHttpTest.EnforcesCrossUserIsolation` | http | `service/tests/test_resume_http.cpp:793` L852 | `EXPECT_TRUE( secondList->jsonBody["data"]["list"].empty())` | KEEP |
| `ResumeServiceTest.UploadsListsAndVersionsTextAndPdf` | service | `service/tests/test_resume_service.cpp:185` L238 | `EXPECT_EQ( versions.value[1].content.value(), "second version")` | KEEP |
| `UserHttpTest.ManagesProfileAndSkillsWithOwnership` | http | `service/tests/test_user_http.cpp:626` L715 | `EXPECT_TRUE( secondProfile->jsonBody["data"]["target_position"] .is_null())` | KEEP |
| `UserServiceTest.ManagesSkillsWithValidationAndIsolation` | service | `service/tests/test_user_service.cpp:635` L677 | `EXPECT_EQ( service->UpsertSkill( firstLogin.value.userId, "C++", 0) .code, ErrorCode::kInvalidParam)` | KEEP |

### G8. Job search: filter normalization / ranking / visibility response fields

Group size: **15** cases; layers: dao, http, service; recommendation: KEEP (no subsumption proven).

| Case | Layer | file:line | assertion (verbatim, truncated) | Recommendation |
|---|---|---|---|---|
| `JobDaoTest.SearchesFiltersAndSortsActiveJobs` | dao | `service/tests/test_job_dao.cpp:92` L137 | `EXPECT_EQ( dao->CountJobs( JobSearchFilter{ token, std::nullopt, std::nullopt, std::nullopt, std::nullopt}), 2U)` | KEEP |
| `JobDaoTest.FiltersAndSortsByThe0015AFocusFields` | dao | `service/tests/test_job_dao.cpp:217` L335 | `EXPECT_EQ(educationAscRows[0].id, socialId)` | KEEP |
| `JobDaoTest.TreatsLikeWildcardsAsLiteralCharacters` | dao | `service/tests/test_job_dao.cpp:341` L397 | `EXPECT_EQ( dao->CountJobs( JobSearchFilter{ "' OR 1=1 --", std::nullopt, std::nullopt, std::nullopt, std::nullopt}), 0U)` | KEEP |
| `JobDaoTest.FiltersSalaryAndSkillAndLoadsBulkRequirements` | dao | `service/tests/test_job_dao.cpp:464` L521 | `EXPECT_EQ(dao->CountJobs(filter), 1U)` | KEEP |
| `JobHttpTest.SearchesWithPaginationAndPercentDecoding` | http | `service/tests/test_job_http.cpp:493` L511 | `EXPECT_EQ( firstPage->jsonBody["data"]["list"][0]["id"], newerId)` | KEEP |
| `JobHttpTest.EnforcesKeywordLengthBoundary` | http | `service/tests/test_job_http.cpp:596` L620 | `EXPECT_EQ( rejected->jsonBody["code"], 10001)` | KEEP |
| `JobHttpTest.RejectsMalformedQueryAndPath` | http | `service/tests/test_job_http.cpp:626` L634 | `EXPECT_EQ( invalidPage->jsonBody["code"], 10001)` | KEEP |
| `JobHttpTest.FiltersBySalaryAndSkill` | http | `service/tests/test_job_http.cpp:667` L680 | `EXPECT_EQ( response->jsonBody["data"]["list"][0]["id"], newerId)` | KEEP |
| `JobRankingServiceTest.SortsPaginatesAndReportsTruncation` | service | `service/tests/test_job_ranking_service.cpp:189` L231 | `EXPECT_GT( result.value.list[0].score, 0.0)` | KEEP |
| `JobRankingServiceTest.RequiresUsableProfileOrSkills` | service | `service/tests/test_job_ranking_service.cpp:237` L245 | `EXPECT_EQ( service.RankJobs(1, {}, 1, 20).code, ErrorCode::kInvalidParam)` | KEEP |
| `JobServiceTest.TrimsFiltersAndBuildsPagination` | service | `service/tests/test_job_service.cpp:110` L137 | `EXPECT_EQ(dao.capturedKeyword.value(), "C++")` | KEEP |
| `JobServiceTest.EmptyFiltersBecomeNullopt` | service | `service/tests/test_job_service.cpp:144` L158 | `ASSERT_EQ(result.code, ErrorCode::kSuccess)` | KEEP |
| `JobServiceTest.NormalizesExtendedFilters` | service | `service/tests/test_job_service.cpp:167` L189 | `EXPECT_EQ( service.SearchJobs( JobSearchFilter{ std::nullopt, std::nullopt, std::uint32_t(40), std::uint32_t(20), std::nullopt}, 1, 20) .code, ErrorC…` | KEEP |
| `JobServiceTest.RejectsInvalidPagination` | service | `service/tests/test_job_service.cpp:205` L221 | `EXPECT_EQ( service.SearchJobs( {}, max, 2) .code, ErrorCode::kInvalidParam)` | KEEP |
| `JobServiceTest.NormalizesFreshnessFilters` | service | `service/tests/test_job_service.cpp:282` L310 | `EXPECT_EQ( service.SearchJobs(tooLarge, 1, 20).code, ErrorCode::kInvalidParam)` | KEEP |

### G9. Concurrency: duplicate-create / claim exactly once

Group size: **4** cases; layers: dao, service; recommendation: KEEP (no subsumption proven).

| Case | Layer | file:line | assertion (verbatim, truncated) | Recommendation |
|---|---|---|---|---|
| `AiTaskDaoTest.NeverHandsTheSameTaskToTwoWorkers` | dao | `service/tests/test_ai_task_dao.cpp:296` L303 | `ASSERT_TRUE(CreateTask(false).has_value()); } ` | KEEP |
| `ApplicationServiceTest.ConcurrentCreationProducesOneDuplicate` | service | `service/tests/test_application_service.cpp:348` L420 | `EXPECT_EQ(duplicate, 1U)` | KEEP |
| `ResumeServiceTest.ConcurrentVersionsAreUnique` | service | `service/tests/test_resume_service.cpp:474` L536 | `EXPECT_EQ(stored.value.size(), 3U)` | KEEP |
| `UserServiceTest.SupportsConcurrentRegistrationAndSessions` | service | `service/tests/test_user_service.cpp:321` L398 | `EXPECT_FALSE(failed.load()); for (const std::string &token : threadTokens)` | KEEP |

### G10. Schema / DDL invariants (backend test_schema)

Group size: **15** cases; layers: backend/core; recommendation: KEEP (no subsumption proven).

| Case | Layer | file:line | assertion (verbatim, truncated) | Recommendation |
|---|---|---|---|---|
| `SchemaTest.DatabaseUsesExpectedCharsetAndCollation` | backend/core | `backend/tests/test_schema.cpp:132` L143 | `EXPECT_EQ( RequireValue(result.value(0, 1)), "utf8mb4_general_ci")` | KEEP |
| `SchemaTest.CreatesAllExpectedTables` | backend/core | `backend/tests/test_schema.cpp:149` L189 | `EXPECT_EQ(tables, expected)` | KEEP |
| `SchemaTest.CreatesRequiredColumns` | backend/core | `backend/tests/test_schema.cpp:194` L240 | `EXPECT_NE( columns["job_review_items"].find("reason_code"), columns["job_review_items"].end())` | KEEP |
| `SchemaTest.StoresTheFullReviewOutcomeVocabulary` | backend/core | `backend/tests/test_schema.cpp:256` L266 | `EXPECT_EQ(RequireValue(result.value(0, 0)), "varchar(24)")` | KEEP |
| `SchemaTest.Creates0015AReviewAndDeadlineIndexes` | backend/core | `backend/tests/test_schema.cpp:274` L294 | `EXPECT_EQ(indexes.find("idx_nature_education"), indexes.end())` | KEEP |
| `SchemaTest.Creates0015ProfileCandidateColumns` | backend/core | `backend/tests/test_schema.cpp:301` L327 | `EXPECT_EQ(types["graduation_year"], "smallint")` | KEEP |
| `SchemaTest.Creates0015ResumeArtifactTable` | backend/core | `backend/tests/test_schema.cpp:335` L403 | `EXPECT_NE(names.find("idx_user_type_created"), names.end())` | KEEP |
| `SchemaTest.CreatesRequiredUniqueIndexes` | backend/core | `backend/tests/test_schema.cpp:407` L435 | `EXPECT_NE( indexes["resume_versions"].find("uk_resume_version"), indexes["resume_versions"].end())` | KEEP |
| `SchemaTest.CreatesFreshnessObservationColumns` | backend/core | `backend/tests/test_schema.cpp:450` L480 | `EXPECT_EQ(sourceStatus->second.defaultValue, "unknown")` | KEEP |
| `SchemaTest.CreatesSourceScopeStatusIndex` | backend/core | `backend/tests/test_schema.cpp:484` L500 | `EXPECT_EQ(RequireValue(result.value(1, 2)), "source_scope")` | KEEP |
| `SchemaTest.SourceUrlUniquenessIsUnchanged` | backend/core | `backend/tests/test_schema.cpp:506` L518 | `EXPECT_EQ(RequireValue(result.value(0, 1)), "source_url")` | KEEP |
| `SchemaTest.LegacyJobRowReadsWithFreshnessDefaults` | backend/core | `backend/tests/test_schema.cpp:522` L550 | `EXPECT_EQ(RequireValue(result.value(0, 2)), "unknown")` | KEEP |
| `SchemaTest.Creates0014JobColumnsAndNoDeferredIndex` | backend/core | `backend/tests/test_schema.cpp:564` L606 | `EXPECT_NE( names.find("idx_source_scope_status"), names.end())` | KEEP |
| `SchemaTest.LegacyJobRowReadsWithNull0014Fields` | backend/core | `backend/tests/test_schema.cpp:612` L652 | `EXPECT_EQ(cleanup.affectedRows, 1U)` | KEEP |
| `SchemaTest.CreatesAiTaskLeaseColumnsAndIndex` | backend/core | `backend/tests/test_schema.cpp:657` L704 | `EXPECT_EQ(RequireValue(index.value(1, 2)), "lease_expire_at")` | KEEP |

### G11. HTTP framing correctness (backend test_http_correctness)

Group size: **4** cases; layers: backend/core; recommendation: RECORD-ONLY for the listed sibling family, KEEP otherwise.

| Case | Layer | file:line | assertion (verbatim, truncated) | Recommendation |
|---|---|---|---|---|
| `HttpCorrectnessTest.HeaderLookupIsCaseInsensitive` | backend/core | `backend/tests/test_http_correctness.cpp:11` L17 | `EXPECT_EQ(request.GetHeader("Host"), "localhost")` | KEEP |
| `HttpCorrectnessTest.ParserAcceptsLowercaseFramingHeaders` | backend/core | `backend/tests/test_http_correctness.cpp:21` L36 | `EXPECT_EQ(parser.Request().Body(), "{\"a\":1}")` | KEEP |
| `HttpCorrectnessTest.RejectsDuplicateContentLength` | backend/core | `backend/tests/test_http_correctness.cpp:42` L52 | `EXPECT_EQ( parser.Parse(&buffer), ParseResult::kError)` | RECORD-ONLY |
| `HttpCorrectnessTest.RejectsMixedCaseDuplicateContentLength` | backend/core | `backend/tests/test_http_correctness.cpp:59` L71 | `EXPECT_EQ( parser.Parse(&buffer), ParseResult::kError)` | RECORD-ONLY |

### G12. Source observation validation boundaries (shared MakeObservation fixture)

Group size: **20** cases; layers: rule; recommendation: RECORD-ONLY for the listed sibling family, KEEP otherwise.

| Case | Layer | file:line | assertion (verbatim, truncated) | Recommendation |
|---|---|---|---|---|
| `SourceObservationValidation.AcceptsCompleteObservationWithJobs` | rule | `service/tests/test_source_observation.cpp:77` L78 | `EXPECT_TRUE(ValidateSourceObservation(MakeObservation()))` | RECORD-ONLY |
| `SourceObservationValidation.AcceptsCompleteEmptyBoard` | rule | `service/tests/test_source_observation.cpp:82` L85 | `EXPECT_TRUE(ValidateSourceObservation(observation))` | RECORD-ONLY |
| `SourceObservationValidation.RejectsIncompleteEmptyBoard` | rule | `service/tests/test_source_observation.cpp:90` L94 | `EXPECT_FALSE(ValidateSourceObservation(observation))` | RECORD-ONLY |
| `SourceObservationValidation.AcceptsIncompletePartialBatch` | rule | `service/tests/test_source_observation.cpp:99` L102 | `EXPECT_TRUE(ValidateSourceObservation(observation))` | RECORD-ONLY |
| `SourceObservationSources.AcceptsOnlyConfiguredProviders` | rule | `service/tests/test_source_observation.cpp:107` L115 | `EXPECT_FALSE(IsSupportedObservationSource("Greenhouse"))` | RECORD-ONLY |
| `SourceObservationValidation.AcceptsAllSupportedSources` | rule | `service/tests/test_source_observation.cpp:119` L128 | `EXPECT_TRUE(ValidateSourceObservation(observation)) << source; }` | RECORD-ONLY |
| `SourceObservationValidation.RejectsUnsupportedOrEmptySource` | rule | `service/tests/test_source_observation.cpp:133` L141 | `EXPECT_FALSE(ValidateSourceObservation(observation)) << source; }` | RECORD-ONLY |
| `SourceObservationSources.KeepsTheAllowlistClosedUntilASourceIsVerified` | rule | `service/tests/test_source_observation.cpp:146` L158 | `EXPECT_FALSE(IsSupportedObservationSource(source)) << source; }` | RECORD-ONLY |
| `SourceObservationValidation.RequiresEveryJobToCarryTheObservationSource` | rule | `service/tests/test_source_observation.cpp:163` L174 | `EXPECT_TRUE(ValidateSourceObservation(matching)) << source; ` | RECORD-ONLY |
| `SourceObservationValidation.RejectsSourceTooLong` | rule | `service/tests/test_source_observation.cpp:187` L190 | `EXPECT_FALSE(ValidateSourceObservation(observation))` | RECORD-ONLY |
| `SourceObservationValidation.EnforcesSourceScopeBounds` | rule | `service/tests/test_source_observation.cpp:195` L202 | `EXPECT_FALSE(ValidateSourceObservation(tooLong))` | RECORD-ONLY |
| `SourceObservationValidation.RejectsMalformedObservedAt` | rule | `service/tests/test_source_observation.cpp:232` L235 | `EXPECT_FALSE(ValidateSourceObservation(observation))` | RECORD-ONLY |
| `SourceObservationValidation.RejectsEmptyObservedAt` | rule | `service/tests/test_source_observation.cpp:240` L243 | `EXPECT_FALSE(ValidateSourceObservation(observation))` | RECORD-ONLY |
| `SourceObservationValidation.EnforcesBatchLimits` | rule | `service/tests/test_source_observation.cpp:248` L251 | `EXPECT_TRUE(ValidateSourceObservation(completeLimit))` | RECORD-ONLY |
| `SourceObservationValidation.RejectsJobSourceMismatch` | rule | `service/tests/test_source_observation.cpp:269` L272 | `EXPECT_FALSE(ValidateSourceObservation(observation))` | RECORD-ONLY |
| `SourceObservationValidation.RejectsDuplicateSourceUrl` | rule | `service/tests/test_source_observation.cpp:277` L290 | `EXPECT_FALSE(ValidateSourceObservation(foldedCase))` | RECORD-ONLY |
| `SourceObservationValidation.RejectsInvalidJobFields` | rule | `service/tests/test_source_observation.cpp:301` L338 | `EXPECT_FALSE(ValidateSourceObservation(invertedSalary))` | RECORD-ONLY |
| `SourceObservationValidation.EnforcesObservationFieldBounds` | rule | `service/tests/test_source_observation.cpp:347` L362 | `EXPECT_FALSE(ValidateSourceObservation(malformedPublishedAt))` | RECORD-ONLY |
| `SourceObservationValidation.ValidatesThe0014ExtraFields` | rule | `service/tests/test_source_observation.cpp:366` L406 | `EXPECT_FALSE(ValidateSourceObservation(dateOnlyWrongInstant))` | RECORD-ONLY |
| `SourceObservationValidation.RejectsInvalidRequirements` | rule | `service/tests/test_source_observation.cpp:414` L430 | `EXPECT_FALSE(ValidateSourceObservation(duplicateSkill))` | RECORD-ONLY |

### G13. Config defaults and rejection boundaries (shared BaseEnvironment fixture)

Group size: **15** cases; layers: backend/core; recommendation: RECORD-ONLY for the listed sibling family, KEEP otherwise.

| Case | Layer | file:line | assertion (verbatim, truncated) | Recommendation |
|---|---|---|---|---|
| `ConfigTest.LoadsDefaults` | backend/core | `backend/tests/test_config.cpp:21` L26 | `EXPECT_EQ(config.server.port, 8080)` | RECORD-ONLY |
| `ConfigTest.LoadsYaml` | backend/core | `backend/tests/test_config.cpp:44` L69 | `EXPECT_EQ(config.server.port, 9090)` | RECORD-ONLY |
| `ConfigTest.EnvironmentOverridesYaml` | backend/core | `backend/tests/test_config.cpp:89` L136 | `EXPECT_EQ(config.mysql.password, "env-secret")` | RECORD-ONLY |
| `ConfigTest.RequiresMySqlPassword` | backend/core | `backend/tests/test_config.cpp:150` L151 | `EXPECT_THROW( Config::LoadFromString("", {}), ConfigError)` | RECORD-ONLY |
| `ConfigTest.RejectsInvalidPort` | backend/core | `backend/tests/test_config.cpp:157` L158 | `EXPECT_THROW( Config::LoadFromString( "server:\n" " port: 70000\n", BaseEnvironment()), ConfigError)` | RECORD-ONLY |
| `ConfigTest.RejectsTypeErrors` | backend/core | `backend/tests/test_config.cpp:167` L168 | `EXPECT_THROW( Config::LoadFromString( "server:\n" " port: invalid\n", BaseEnvironment()), ConfigError)` | RECORD-ONLY |
| `ConfigTest.RejectsPasswordInYaml` | backend/core | `backend/tests/test_config.cpp:177` L178 | `EXPECT_THROW( Config::LoadFromString( "mysql:\n" " password: not-allowed\n", BaseEnvironment()), ConfigError)` | RECORD-ONLY |
| `ConfigTest.ReportsMissingFile` | backend/core | `backend/tests/test_config.cpp:187` L188 | `EXPECT_THROW( Config::LoadFromFile( "/tmp/jobpilot-config-does-not-exist.yaml", BaseEnvironment()), ConfigError)` | RECORD-ONLY |
| `ConfigTest.LoadsFile` | backend/core | `backend/tests/test_config.cpp:196` L213 | `EXPECT_EQ(config.server.port, 9091)` | RECORD-ONLY |
| `ConfigTest.RejectsMalformedYaml` | backend/core | `backend/tests/test_config.cpp:220` L221 | `EXPECT_THROW( Config::LoadFromString( "server: [\n", BaseEnvironment()), ConfigError)` | RECORD-ONLY |
| `ConfigTest.RejectsInvalidStorageConfiguration` | backend/core | `backend/tests/test_config.cpp:229` L230 | `EXPECT_THROW( Config::LoadFromString( "storage:\n" " max_file_bytes: 0\n", BaseEnvironment()), ConfigError)` | RECORD-ONLY |
| `ConfigTest.LoadsAiDefaults` | backend/core | `backend/tests/test_config.cpp:246` L249 | `EXPECT_FALSE(config.ai.enhancementEnabled)` | RECORD-ONLY |
| `ConfigTest.LoadsAiYaml` | backend/core | `backend/tests/test_config.cpp:256` L264 | `EXPECT_TRUE(config.ai.enhancementEnabled)` | RECORD-ONLY |
| `ConfigTest.AiEnvironmentOverridesYaml` | backend/core | `backend/tests/test_config.cpp:271` L286 | `EXPECT_TRUE(config.ai.enhancementEnabled)` | RECORD-ONLY |
| `ConfigTest.RejectsInvalidAiConfiguration` | backend/core | `backend/tests/test_config.cpp:293` L307 | `EXPECT_THROW( Config::LoadFromString( "ai:\n" " enhancement_enabled: maybe\n", BaseEnvironment()), ConfigError)` | RECORD-ONLY |
<!-- GENERATED-END groups -->

## 4. Merge candidate ledger — all 66 candidates and the clause each one fails

Both directions of every flagged pair were evaluated, so a reciprocal pair (A→B and B→A) appears
twice when both directions were flagged; each row is one independent application of the rule.

Class totals (66 directed candidates): assertion_sets_equal_not_strict: 24 · assertion_sets_not_identical_loop_iteration_unproven: 1 · assertions_not_contained: 18 · different_input_literal_inside_assertion: 20 · inputs_not_contained: 3

Why each class fails:

* **assertion sets identical, not strictly contained** (24) — both cases assert exactly the same
  target; neither is a superset, and the inputs differ (e.g. the two `Content-Length` framings, or
  the twenty `test_source_observation.cpp` rejection boundaries that share one `EXPECT_FALSE`).
* **input literal differs inside the assertion** (20) — for the `test_config.cpp` rejection cases
  the YAML under test lives *inside* `EXPECT_THROW(...)`; collapsing literals makes the assertions
  look identical, but the inputs (`port: 70000` vs `storage.max_file_bytes: 0` vs `ai.lease_seconds: 0`)
  are different, so clause (b) fails.
* **removed case asserts a target the survivor never asserts** (18) — e.g.
  `JobIngestionDaoTest.CreatesJob` asserts `job->summary.title == "Created Job"` while
  `UpdatesExistingJobByIdempotentSourceUrl` asserts the updated title; `SchemaTest` cases each pin a
  different DDL object; `ConfigTest.LoadsDefaults` and `ConfigTest.LoadsAiDefaults` share the input
  `LoadFromString("", BaseEnvironment())` but assert disjoint field sets.
* **removed case drives input/setup the survivor does not** (3) — e.g.
  `SourceSyncDaoTest.GraceBoundaryAtFortyEightHoursDoesNotClose` repeats
  `CompleteAbsenceMarksMissingWithoutClosing`'s fixture and assertions but closes at exactly 48 h
  (`2026-10-03 00:00:00` vs `2026-10-01 01:00:00`); the timestamp *is* the boundary under test, so
  clause (b) fails.

* **loop-iteration equivalence, not mechanically provable** (1) — the manual-sweep hit:
  `SourceObservationValidation.AcceptsCompleteObservationWithJobs` (L77,
  `EXPECT_TRUE(ValidateSourceObservation(MakeObservation()))`) vs
  `SourceObservationValidation.AcceptsAllSupportedSources` (L119, a loop over
  `{"greenhouse", "lever", "ashby"}`). The survivor's first iteration builds `MakeObservation()`
  with `source = "greenhouse"` and `job.source = "greenhouse"` — byte-identical to the removed
  case's input — and asserts the same predicate, so a human could call the scalar case covered.
  It is **not proven** mechanically: the assertion statements are not identical text, the survivor's
  assertion set is not *strictly* larger, and after deletion the default-fixture assertion would
  live only inside a loop whose source list can be edited independently. Kept, per the hard rule and
  the task's "when in doubt, KEEP and report".

<!-- GENERATED-BEGIN candidates -->
| # | removed (file:line) | survivor (file:line) | detector that flagged it | rule check that fails |
|---|---|---|---|---|
| 1 | `HttpCorrectnessTest.RejectsDuplicateContentLength` (`backend/tests/test_http_correctness.cpp:42`) | `HttpCorrectnessTest.RejectsMixedCaseDuplicateContentLength` (`backend/tests/test_http_correctness.cpp:59`) | shape_containment | assertion sets identical → no strict containment |
| 2 | `HttpCorrectnessTest.RejectsMixedCaseDuplicateContentLength` (`backend/tests/test_http_correctness.cpp:59`) | `HttpCorrectnessTest.RejectsDuplicateContentLength` (`backend/tests/test_http_correctness.cpp:42`) | shape_containment | assertion sets identical → no strict containment |
| 3 | `SourceObservationValidation.AcceptsCompleteEmptyBoard` (`service/tests/test_source_observation.cpp:82`) | `SourceObservationValidation.AcceptsIncompletePartialBatch` (`service/tests/test_source_observation.cpp:99`) | asserts_norm_subset | assertion sets identical → no strict containment |
| 4 | `SourceObservationValidation.AcceptsIncompletePartialBatch` (`service/tests/test_source_observation.cpp:99`) | `SourceObservationValidation.AcceptsCompleteEmptyBoard` (`service/tests/test_source_observation.cpp:82`) | asserts_norm_subset | assertion sets identical → no strict containment |
| 5 | `SourceObservationValidation.RejectsEmptyObservedAt` (`service/tests/test_source_observation.cpp:240`) | `SourceObservationValidation.RejectsIncompleteEmptyBoard` (`service/tests/test_source_observation.cpp:90`) | asserts_norm_subset | assertion sets identical → no strict containment |
| 6 | `SourceObservationValidation.RejectsEmptyObservedAt` (`service/tests/test_source_observation.cpp:240`) | `SourceObservationValidation.RejectsSourceTooLong` (`service/tests/test_source_observation.cpp:187`) | asserts_norm_subset | assertion sets identical → no strict containment |
| 7 | `SourceObservationValidation.RejectsEmptyObservedAt` (`service/tests/test_source_observation.cpp:240`) | `SourceObservationValidation.RejectsMalformedObservedAt` (`service/tests/test_source_observation.cpp:232`) | asserts_norm_subset | assertion sets identical → no strict containment |
| 8 | `SourceObservationValidation.RejectsEmptyObservedAt` (`service/tests/test_source_observation.cpp:240`) | `SourceObservationValidation.RejectsJobSourceMismatch` (`service/tests/test_source_observation.cpp:269`) | asserts_norm_subset | assertion sets identical → no strict containment |
| 9 | `SourceObservationValidation.RejectsIncompleteEmptyBoard` (`service/tests/test_source_observation.cpp:90`) | `SourceObservationValidation.RejectsSourceTooLong` (`service/tests/test_source_observation.cpp:187`) | asserts_norm_subset | assertion sets identical → no strict containment |
| 10 | `SourceObservationValidation.RejectsIncompleteEmptyBoard` (`service/tests/test_source_observation.cpp:90`) | `SourceObservationValidation.RejectsMalformedObservedAt` (`service/tests/test_source_observation.cpp:232`) | asserts_norm_subset | assertion sets identical → no strict containment |
| 11 | `SourceObservationValidation.RejectsIncompleteEmptyBoard` (`service/tests/test_source_observation.cpp:90`) | `SourceObservationValidation.RejectsEmptyObservedAt` (`service/tests/test_source_observation.cpp:240`) | asserts_norm_subset | assertion sets identical → no strict containment |
| 12 | `SourceObservationValidation.RejectsIncompleteEmptyBoard` (`service/tests/test_source_observation.cpp:90`) | `SourceObservationValidation.RejectsJobSourceMismatch` (`service/tests/test_source_observation.cpp:269`) | asserts_norm_subset | assertion sets identical → no strict containment |
| 13 | `SourceObservationValidation.RejectsJobSourceMismatch` (`service/tests/test_source_observation.cpp:269`) | `SourceObservationValidation.RejectsIncompleteEmptyBoard` (`service/tests/test_source_observation.cpp:90`) | asserts_norm_subset | assertion sets identical → no strict containment |
| 14 | `SourceObservationValidation.RejectsJobSourceMismatch` (`service/tests/test_source_observation.cpp:269`) | `SourceObservationValidation.RejectsSourceTooLong` (`service/tests/test_source_observation.cpp:187`) | asserts_norm_subset | assertion sets identical → no strict containment |
| 15 | `SourceObservationValidation.RejectsJobSourceMismatch` (`service/tests/test_source_observation.cpp:269`) | `SourceObservationValidation.RejectsMalformedObservedAt` (`service/tests/test_source_observation.cpp:232`) | asserts_norm_subset | assertion sets identical → no strict containment |
| 16 | `SourceObservationValidation.RejectsJobSourceMismatch` (`service/tests/test_source_observation.cpp:269`) | `SourceObservationValidation.RejectsEmptyObservedAt` (`service/tests/test_source_observation.cpp:240`) | asserts_norm_subset | assertion sets identical → no strict containment |
| 17 | `SourceObservationValidation.RejectsMalformedObservedAt` (`service/tests/test_source_observation.cpp:232`) | `SourceObservationValidation.RejectsIncompleteEmptyBoard` (`service/tests/test_source_observation.cpp:90`) | asserts_norm_subset | assertion sets identical → no strict containment |
| 18 | `SourceObservationValidation.RejectsMalformedObservedAt` (`service/tests/test_source_observation.cpp:232`) | `SourceObservationValidation.RejectsSourceTooLong` (`service/tests/test_source_observation.cpp:187`) | asserts_norm_subset | assertion sets identical → no strict containment |
| 19 | `SourceObservationValidation.RejectsMalformedObservedAt` (`service/tests/test_source_observation.cpp:232`) | `SourceObservationValidation.RejectsEmptyObservedAt` (`service/tests/test_source_observation.cpp:240`) | asserts_norm_subset | assertion sets identical → no strict containment |
| 20 | `SourceObservationValidation.RejectsMalformedObservedAt` (`service/tests/test_source_observation.cpp:232`) | `SourceObservationValidation.RejectsJobSourceMismatch` (`service/tests/test_source_observation.cpp:269`) | asserts_norm_subset | assertion sets identical → no strict containment |
| 21 | `SourceObservationValidation.RejectsSourceTooLong` (`service/tests/test_source_observation.cpp:187`) | `SourceObservationValidation.RejectsIncompleteEmptyBoard` (`service/tests/test_source_observation.cpp:90`) | asserts_norm_subset | assertion sets identical → no strict containment |
| 22 | `SourceObservationValidation.RejectsSourceTooLong` (`service/tests/test_source_observation.cpp:187`) | `SourceObservationValidation.RejectsMalformedObservedAt` (`service/tests/test_source_observation.cpp:232`) | asserts_norm_subset | assertion sets identical → no strict containment |
| 23 | `SourceObservationValidation.RejectsSourceTooLong` (`service/tests/test_source_observation.cpp:187`) | `SourceObservationValidation.RejectsEmptyObservedAt` (`service/tests/test_source_observation.cpp:240`) | asserts_norm_subset | assertion sets identical → no strict containment |
| 24 | `SourceObservationValidation.RejectsSourceTooLong` (`service/tests/test_source_observation.cpp:187`) | `SourceObservationValidation.RejectsJobSourceMismatch` (`service/tests/test_source_observation.cpp:269`) | asserts_norm_subset | assertion sets identical → no strict containment |
| 25 | `ConfigTest.LoadsAiDefaults` (`backend/tests/test_config.cpp:246`) | `ConfigTest.LoadsDefaults` (`backend/tests/test_config.cpp:21`) | identical_inputs, inputs_verbatim_subset | removed case asserts a target the survivor never asserts |
| 26 | `ConfigTest.LoadsDefaults` (`backend/tests/test_config.cpp:21`) | `ConfigTest.LoadsAiDefaults` (`backend/tests/test_config.cpp:246`) | identical_inputs, inputs_verbatim_subset | removed case asserts a target the survivor never asserts |
| 27 | `LoggerTest.RejectsUnknownLevel` (`backend/tests/test_logger.cpp:30`) | `LoggerTest.InitializesAndWrites` (`backend/tests/test_logger.cpp:12`) | inputs_verbatim_subset | removed case asserts a target the survivor never asserts |
| 28 | `SqlResultTest.ThrowsOnInvalidColumnIndex` (`backend/tests/test_sql_result.cpp:67`) | `SqlResultTest.ThrowsOnInvalidRowIndex` (`backend/tests/test_sql_result.cpp:56`) | identical_inputs, inputs_verbatim_subset | removed case asserts a target the survivor never asserts |
| 29 | `SqlResultTest.ThrowsOnInvalidRowIndex` (`backend/tests/test_sql_result.cpp:56`) | `SqlResultTest.ThrowsOnInvalidColumnIndex` (`backend/tests/test_sql_result.cpp:67`) | identical_inputs, inputs_verbatim_subset | removed case asserts a target the survivor never asserts |
| 30 | `AiTaskServiceTest.RefusesInputWithoutALiveLease` (`service/tests/test_ai_task_service.cpp:1394`) | `AiTaskServiceTest.BuildsTheWorkerInputBundle` (`service/tests/test_ai_task_service.cpp:1286`) | inputs_verbatim_subset | removed case asserts a target the survivor never asserts |
| 31 | `AiTaskServiceTest.RejectsInvalidIdentifiersWithoutDaoCalls` (`service/tests/test_ai_task_service.cpp:634`) | `AiTaskServiceTest.RefusesInputWithoutALiveLease` (`service/tests/test_ai_task_service.cpp:1394`) | inputs_verbatim_subset | removed case asserts a target the survivor never asserts |
| 32 | `AiTaskServiceTest.RejectsInvalidIdentifiersWithoutDaoCalls` (`service/tests/test_ai_task_service.cpp:634`) | `AiTaskServiceTest.RejectsMissingJobResumeAndResumeText` (`service/tests/test_ai_task_service.cpp:696`) | inputs_verbatim_subset | removed case asserts a target the survivor never asserts |
| 33 | `AiTaskServiceTest.RejectsInvalidIdentifiersWithoutDaoCalls` (`service/tests/test_ai_task_service.cpp:634`) | `AiTaskServiceTest.MapsDaoFailuresToDatabaseError` (`service/tests/test_ai_task_service.cpp:773`) | inputs_verbatim_subset | removed case asserts a target the survivor never asserts |
| 34 | `AiTaskServiceTest.RejectsInvalidIdentifiersWithoutDaoCalls` (`service/tests/test_ai_task_service.cpp:634`) | `AiTaskServiceTest.DisabledEnhancementResolvesAtCreation` (`service/tests/test_ai_task_service.cpp:846`) | inputs_verbatim_subset | removed case asserts a target the survivor never asserts |
| 35 | `AiTaskServiceTest.RejectsInvalidIdentifiersWithoutDaoCalls` (`service/tests/test_ai_task_service.cpp:634`) | `AiTaskServiceTest.OwnershipRefusalUsesOneNotFoundCode` (`service/tests/test_ai_task_service.cpp:1000`) | inputs_verbatim_subset | removed case asserts a target the survivor never asserts |
| 36 | `AiTaskServiceTest.RejectsInvalidIdentifiersWithoutDaoCalls` (`service/tests/test_ai_task_service.cpp:634`) | `AiTaskServiceTest.FailedAndDegradedTasksStillExposeTheRuleResult` (`service/tests/test_ai_task_service.cpp:1041`) | inputs_verbatim_subset | removed case asserts a target the survivor never asserts |
| 37 | `AiTaskServiceTest.RejectsInvalidIdentifiersWithoutDaoCalls` (`service/tests/test_ai_task_service.cpp:634`) | `AiTaskServiceTest.BuildsTheWorkerInputBundle` (`service/tests/test_ai_task_service.cpp:1286`) | inputs_verbatim_subset | removed case asserts a target the survivor never asserts |
| 38 | `AiTaskServiceTest.RejectsInvalidIdentifiersWithoutDaoCalls` (`service/tests/test_ai_task_service.cpp:634`) | `AiTaskServiceTest.ReportsBrokenBundleAsInternalError` (`service/tests/test_ai_task_service.cpp:1433`) | inputs_verbatim_subset | removed case asserts a target the survivor never asserts |
| 39 | `JobRankingServiceTest.RequiresUsableProfileOrSkills` (`service/tests/test_job_ranking_service.cpp:237`) | `JobRankingServiceTest.SortsPaginatesAndReportsTruncation` (`service/tests/test_job_ranking_service.cpp:189`) | inputs_verbatim_subset | removed case asserts a target the survivor never asserts |
| 40 | `MatchResultJsonTest.IsDeterministicForTheSameInput` (`service/tests/test_match_result_json.cpp:68`) | `MatchResultJsonTest.RoundTripsEveryField` (`service/tests/test_match_result_json.cpp:124`) | inputs_verbatim_subset | removed case asserts a target the survivor never asserts |
| 41 | `SourceObservationValidation.AcceptsCompleteEmptyBoard` (`service/tests/test_source_observation.cpp:82`) | `SourceObservationValidation.RejectsIncompleteEmptyBoard` (`service/tests/test_source_observation.cpp:90`) | inputs_verbatim_subset | removed case asserts a target the survivor never asserts |
| 42 | `SourceObservationValidation.AcceptsIncompletePartialBatch` (`service/tests/test_source_observation.cpp:99`) | `SourceObservationValidation.RejectsIncompleteEmptyBoard` (`service/tests/test_source_observation.cpp:90`) | inputs_verbatim_subset | removed case asserts a target the survivor never asserts |
| 43 | `ConfigTest.RejectsInvalidPort` (`backend/tests/test_config.cpp:157`) | `ConfigTest.RejectsInvalidStorageConfiguration` (`backend/tests/test_config.cpp:229`) | asserts_norm_subset | input literal differs (inside the assertion) → different input |
| 44 | `ConfigTest.RejectsInvalidPort` (`backend/tests/test_config.cpp:157`) | `ConfigTest.RejectsInvalidAiConfiguration` (`backend/tests/test_config.cpp:293`) | asserts_norm_subset | input literal differs (inside the assertion) → different input |
| 45 | `ConfigTest.RejectsMalformedYaml` (`backend/tests/test_config.cpp:220`) | `ConfigTest.RejectsInvalidAiConfiguration` (`backend/tests/test_config.cpp:293`) | asserts_norm_subset | input literal differs (inside the assertion) → different input |
| 46 | `ConfigTest.RejectsPasswordInYaml` (`backend/tests/test_config.cpp:177`) | `ConfigTest.RejectsInvalidStorageConfiguration` (`backend/tests/test_config.cpp:229`) | asserts_norm_subset | input literal differs (inside the assertion) → different input |
| 47 | `ConfigTest.RejectsPasswordInYaml` (`backend/tests/test_config.cpp:177`) | `ConfigTest.RejectsInvalidAiConfiguration` (`backend/tests/test_config.cpp:293`) | asserts_norm_subset | input literal differs (inside the assertion) → different input |
| 48 | `ConfigTest.RejectsTypeErrors` (`backend/tests/test_config.cpp:167`) | `ConfigTest.RejectsInvalidStorageConfiguration` (`backend/tests/test_config.cpp:229`) | asserts_norm_subset | input literal differs (inside the assertion) → different input |
| 49 | `ConfigTest.RejectsTypeErrors` (`backend/tests/test_config.cpp:167`) | `ConfigTest.RejectsInvalidAiConfiguration` (`backend/tests/test_config.cpp:293`) | asserts_norm_subset | input literal differs (inside the assertion) → different input |
| 50 | `SchemaTest.SourceUrlUniquenessIsUnchanged` (`backend/tests/test_schema.cpp:506`) | `SchemaTest.StoresTheFullReviewOutcomeVocabulary` (`backend/tests/test_schema.cpp:256`) | asserts_norm_subset | input literal differs (inside the assertion) → different input |
| 51 | `SchemaTest.SourceUrlUniquenessIsUnchanged` (`backend/tests/test_schema.cpp:506`) | `SchemaTest.LegacyJobRowReadsWithFreshnessDefaults` (`backend/tests/test_schema.cpp:522`) | asserts_norm_subset | input literal differs (inside the assertion) → different input |
| 52 | `SchemaTest.StoresTheFullReviewOutcomeVocabulary` (`backend/tests/test_schema.cpp:256`) | `SchemaTest.SourceUrlUniquenessIsUnchanged` (`backend/tests/test_schema.cpp:506`) | asserts_norm_subset | input literal differs (inside the assertion) → different input |
| 53 | `SchemaTest.StoresTheFullReviewOutcomeVocabulary` (`backend/tests/test_schema.cpp:256`) | `SchemaTest.LegacyJobRowReadsWithFreshnessDefaults` (`backend/tests/test_schema.cpp:522`) | asserts_norm_subset | input literal differs (inside the assertion) → different input |
| 54 | `JobIngestionDaoTest.CreatesJob` (`service/tests/test_job_ingestion_dao.cpp:86`) | `JobIngestionDaoTest.UpdatesExistingJobByIdempotentSourceUrl` (`service/tests/test_job_ingestion_dao.cpp:114`) | asserts_norm_subset | input literal differs (inside the assertion) → different input |
| 55 | `JobQualityTest.MissingCompanyGoesToReviewNotReject` (`service/tests/test_job_quality.cpp:91`) | `JobQualityTest.RootOrListUrlGoesToReviewButStaysStored` (`service/tests/test_job_quality.cpp:135`) | asserts_norm_subset | input literal differs (inside the assertion) → different input |
| 56 | `JobQualityTest.MissingCompanyGoesToReviewNotReject` (`service/tests/test_job_quality.cpp:91`) | `JobQualityTest.TitleEqualsCompanyGoesToReview` (`service/tests/test_job_quality.cpp:217`) | asserts_norm_subset | input literal differs (inside the assertion) → different input |
| 57 | `JobQualityTest.MissingCompanyGoesToReviewNotReject` (`service/tests/test_job_quality.cpp:91`) | `JobQualityTest.DateOnlyDeadlineWithWrongInstantNeedsReview` (`service/tests/test_job_quality.cpp:250`) | asserts_norm_subset | input literal differs (inside the assertion) → different input |
| 58 | `JobQualityTest.TitleEqualsCompanyGoesToReview` (`service/tests/test_job_quality.cpp:217`) | `JobQualityTest.MissingCompanyGoesToReviewNotReject` (`service/tests/test_job_quality.cpp:91`) | asserts_norm_subset | input literal differs (inside the assertion) → different input |
| 59 | `JobQualityTest.TitleEqualsCompanyGoesToReview` (`service/tests/test_job_quality.cpp:217`) | `JobQualityTest.RootOrListUrlGoesToReviewButStaysStored` (`service/tests/test_job_quality.cpp:135`) | asserts_norm_subset | input literal differs (inside the assertion) → different input |
| 60 | `JobQualityTest.TitleEqualsCompanyGoesToReview` (`service/tests/test_job_quality.cpp:217`) | `JobQualityTest.DateOnlyDeadlineWithWrongInstantNeedsReview` (`service/tests/test_job_quality.cpp:250`) | asserts_norm_subset | input literal differs (inside the assertion) → different input |
| 61 | `TailorRuleTest.RequirementsBecomeMatchedAndMissingStatements` (`service/tests/test_tailor_rule.cpp:203`) | `TailorRuleTest.NothingDerivableBecomesAPlaceholderNotAClaim` (`service/tests/test_tailor_rule.cpp:160`) | asserts_norm_subset | input literal differs (inside the assertion) → different input |
| 62 | `TailorRuleTest.RequirementsBecomeMatchedAndMissingStatements` (`service/tests/test_tailor_rule.cpp:203`) | `TailorRuleTest.RendersTheAtsPlainTextContract` (`service/tests/test_tailor_rule.cpp:239`) | asserts_norm_subset | input literal differs (inside the assertion) → different input |
| 63 | `SchemaTest.LegacyJobRowReadsWithNull0014Fields` (`backend/tests/test_schema.cpp:612`) | `SchemaTest.LegacyJobRowReadsWithFreshnessDefaults` (`backend/tests/test_schema.cpp:522`) | asserts_norm_subset | removed case drives input/setup the survivor does not |
| 64 | `ResumeServiceTest.WriteFailureDoesNotCreateMetadata` (`service/tests/test_resume_service.cpp:445`) | `ResumeServiceTest.CompensatesPromoteFailure` (`service/tests/test_resume_service.cpp:415`) | asserts_norm_subset | removed case drives input/setup the survivor does not |
| 65 | `SourceSyncDaoTest.GraceBoundaryAtFortyEightHoursDoesNotClose` (`service/tests/test_source_sync_dao.cpp:324`) | `SourceSyncDaoTest.CompleteAbsenceMarksMissingWithoutClosing` (`service/tests/test_source_sync_dao.cpp:245`) | asserts_norm_subset | removed case drives input/setup the survivor does not |
| 66 | `SourceObservationValidation.AcceptsCompleteObservationWithJobs` (`service/tests/test_source_observation.cpp:77`) | `SourceObservationValidation.AcceptsAllSupportedSources` (`service/tests/test_source_observation.cpp:119`) | manual_review_loop_equivalent | loop-iteration equivalence only, no identical assertion text → NOT PROVEN |
<!-- GENERATED-END candidates -->

## 5. What was NOT acted on

* **47 RECORD-ONLY cases** (§3, marked in the tables). They are sibling boundary
  families that share one fixture and one assertion shape and differ only in the constant under
  test: `test_source_observation.cpp` validation boundaries (20), `test_config.cpp` defaults and
  rejections (15), `test_source_sync_dao.cpp` absence/grace boundaries (10), the two duplicate
  `Content-Length` framing cases (2). Consolidating them would mean a table-driven rewrite, which
  §5.2 B3 explicitly excludes; nothing was deleted or weakened.
* **`external` / `integration` cases.** 17 `external` and 2 `integration` cases exist. The two
  `integration` cases are `mysql_pool_test` and `redis_client_test` — single-case binaries, so they
  are structurally outside the rule (same-binary clause). Of the `external` cases, 15 are the
  `SchemaTest` cases in the one `test_schema` binary; four candidate directions touch them
  (`StoresTheFullReviewOutcomeVocabulary`, `SourceUrlUniquenessIsUnchanged`,
  `LegacyJobRowReadsWithFreshnessDefaults`, `LegacyJobRowReadsWithNull0014Fields`) and all four were
  rejected, so no failure-mode case was merged away.
* **Cross-binary refactoring and parameterized gtest conversion** (§5.2 B3, excluded by design).
* **Python suites** (§6): audited, but `source_adapters/jobpilot_sources/tests/` and
  `ai_worker/jobpilot_ai/tests/` are outside this unit's write scope.
* **Production code, schema, and `jobpilot_review_scratch`**: untouched; no backfill was run.

## 6. Python suites (audited, RECORD-ONLY)

Statically enumerated with `ast` (no execution, so no suite was re-run):

| Suite | Modules | Test methods | Written? |
|---|---|---|---|
| `source_adapters/jobpilot_sources/tests/` | 26 | 446 | no (out of write scope) |
| `ai_worker/jobpilot_ai/tests/` | 8 | 190 | no (out of write scope) |

The counts match the stage's recorded expectations (`Ran 446` / `Ran 190`). Their overlap shape is
the same as the C++ boundary families: per-fixture cases over one assertion each
(`test_capture_quality.py`, `test_merge.py`, `test_provider.py`). The map and the checker speak
CTest names only; the Python ids were extracted as `Class.method` and none of the 3,299 of them
collides with a CTest case name, so a Python case cannot be covered by a map entry by accident.
RECORD-ONLY.

## 7. Machine-checked map and checker

`docs/quality/test-merge-map.json` carries `counts`, the (empty) `removed`/`survivors` arrays and
all 66 evaluated candidates with `decision`, `detector`, `rule_failure`, `reason`
and `evidence`. A future merge must add an entry with a `proof` block whose `same_binary`,
`same_input` and `assertion_superset` flags are all `true`; the checker rejects a removal without
it, and rejects any removal beyond the `cap` of 20.

`tools/checks/test_coverage_map.py` (stdlib, offline) fails when:

* (a) a `removed` name is still present in the live `ctest -N` list;
* (b) a `survivor` name is absent from it (or its recorded `binary` does not match the live one);
* (c) the map's counts disagree with the live CTest total or with this report's
  `<!-- audit-counts: ... -->` block, or `before - removed != after`;
* (d) the JSON structure is invalid (missing keys, wrong types, bad `decision`, missing proof);
* (e) any `evaluated` (kept) case or its survivor has disappeared from the live list, or the two
  no longer share a binary.

### 7.1 Positive control — the exact required invocation, unmodified repository

```text
$ python3 tools/checks/test_coverage_map.py --test-dir build/presets/release --map docs/quality/test-merge-map.json
OK   map: docs/quality/test-merge-map.json (schema_version 1)
OK   live: 309 cases in build/presets/release (ctest -N, exit 0)
OK   live: resolved 309 case -> binary mappings (--show-only=json-v1)
OK   counts: before=309 removed=0 after=309 live=309 evaluated=66 cap=20
OK   rule: 0 removals, every live case is kept as-is
OK   report: docs/quality/test-overlap-audit.md counts block agrees with the map (before=309 after=309 removed=0)
OK   evaluated: 66 kept candidate pairs still present and intra-binary
test_coverage_map: PASS (309 -> 309, removed=0, survivors=0, live=309)
$ echo $?
0
```

The CLI is exactly the invocation required by the task (only `--test-dir` and `--map`; `--report`
defaults to `test-overlap-audit.md` next to the map, and `--json` is optional).

### 7.2 Negative controls — the checker is proven to fail when it should

A guard that never fails proves nothing, so the checker was run against deliberately broken copies
of the map under `/tmp/audit17/neg/` (the committed files were never modified). Each control exits
non-zero and names the defect:

### control a_removed_case_still_live
$ python3 tools/checks/test_coverage_map.py --test-dir build/presets/release --map /tmp/audit17/neg/a_removed_case_still_live.json --report /tmp/audit17/neg/report_308.md
--- stdout ---
OK   map: /tmp/audit17/neg/a_removed_case_still_live.json (schema_version 1)
OK   live: 309 cases in build/presets/release (ctest -N, exit 0)
OK   live: resolved 309 case -> binary mappings (--show-only=json-v1)
FAIL counts: counts.after=308 but the live CTest list has 309 cases
OK   counts: before=309 removed=1 after=308 live=309 evaluated=66 cap=20
FAIL rule: removed case 'ConfigTest.LoadsDefaults' is still present in the live CTest list
OK   merged ConfigTest.LoadsDefaults -> ConfigTest.LoadsYaml (test_config)
OK   report: /tmp/audit17/neg/report_308.md counts block agrees with the map (before=309 after=308 removed=1)
OK   evaluated: 66 kept candidate pairs still present and intra-binary
test_coverage_map: FAIL (309 -> 308, removed=1, survivors=1, live=309)
--- exit code: 1

### control b_survivor_absent
$ python3 tools/checks/test_coverage_map.py --test-dir build/presets/release --map /tmp/audit17/neg/b_survivor_absent.json --report /tmp/audit17/neg/report_308.md
--- stdout ---
OK   map: /tmp/audit17/neg/b_survivor_absent.json (schema_version 1)
OK   live: 309 cases in build/presets/release (ctest -N, exit 0)
OK   live: resolved 309 case -> binary mappings (--show-only=json-v1)
FAIL counts: counts.after=308 but the live CTest list has 309 cases
OK   counts: before=309 removed=1 after=308 live=309 evaluated=66 cap=20
FAIL rule: removed case 'ConfigTest.LoadsYaml' is still present in the live CTest list
FAIL rule: survivor 'ConfigTest.DoesNotExist' is absent from the live CTest list
OK   report: /tmp/audit17/neg/report_308.md counts block agrees with the map (before=309 after=308 removed=1)
OK   evaluated: 66 kept candidate pairs still present and intra-binary
test_coverage_map: FAIL (309 -> 308, removed=1, survivors=1, live=309)
--- exit code: 1

### control c1_counts_vs_live_ctest
$ python3 tools/checks/test_coverage_map.py --test-dir build/presets/release --map /tmp/audit17/neg/c1_counts_vs_live_ctest.json --report <repo>/docs/quality/test-overlap-audit.md
--- stdout ---
OK   map: /tmp/audit17/neg/c1_counts_vs_live_ctest.json (schema_version 1)
OK   live: 309 cases in build/presets/release (ctest -N, exit 0)
OK   live: resolved 309 case -> binary mappings (--show-only=json-v1)
FAIL counts: before(309) - removed(0) != after(308)
FAIL counts: counts.after=308 but the live CTest list has 309 cases
OK   counts: before=309 removed=0 after=308 live=309 evaluated=66 cap=20
OK   rule: 0 removals, every live case is kept as-is
FAIL report: counts disagree with the map (after: report=309 map=308)
OK   evaluated: 66 kept candidate pairs still present and intra-binary
test_coverage_map: FAIL (309 -> 308, removed=0, survivors=0, live=309)
--- exit code: 1

### control c2_counts_vs_report_marker
$ python3 tools/checks/test_coverage_map.py --test-dir build/presets/release --map /tmp/audit17/neg/c2_counts_vs_report_marker.json --report /tmp/audit17/neg/report_before308.md
--- stdout ---
OK   map: /tmp/audit17/neg/c2_counts_vs_report_marker.json (schema_version 1)
OK   live: 309 cases in build/presets/release (ctest -N, exit 0)
OK   live: resolved 309 case -> binary mappings (--show-only=json-v1)
OK   counts: before=309 removed=0 after=309 live=309 evaluated=66 cap=20
OK   rule: 0 removals, every live case is kept as-is
FAIL report: counts disagree with the map (before: report=308 map=309)
OK   evaluated: 66 kept candidate pairs still present and intra-binary
test_coverage_map: FAIL (309 -> 309, removed=0, survivors=0, live=309)
--- exit code: 1

### control d_missing_proof_block
$ python3 tools/checks/test_coverage_map.py --test-dir build/presets/release --map /tmp/audit17/neg/d_missing_proof_block.json --report <repo>/docs/quality/test-overlap-audit.md
--- stdout ---
OK   map: /tmp/audit17/neg/d_missing_proof_block.json (schema_version 1)
FAIL structure: removed[0] missing key 'proof'
FAIL structure: removed[0].proof must be an object
FAIL test_coverage_map: invalid map structure
--- exit code: 1

### control e_invalid_json
$ python3 tools/checks/test_coverage_map.py --test-dir build/presets/release --map /tmp/audit17/neg/e_invalid_json.json --report <repo>/docs/quality/test-overlap-audit.md
--- stdout ---
FAIL map: invalid JSON in /tmp/audit17/neg/e_invalid_json.json: Expecting property name enclosed in double quotes: line 1 column 34 (char 33)
FAIL test_coverage_map: /tmp/audit17/neg/e_invalid_json.json could not be loaded
--- exit code: 1

Summary: (a) removed-still-live → exit 1, `FAIL rule: removed case 'ConfigTest.LoadsDefaults' is
still present in the live CTest list`; (b) absent survivor → exit 1, `FAIL rule: survivor
'ConfigTest.DoesNotExist' is absent from the live CTest list`; (c1) counts vs live `ctest -N` →
exit 1, `FAIL counts: before(309) - removed(0) != after(308)` / `counts.after=308 but the live CTest
list has 309 cases`; (c2) counts vs the report's `audit-counts` block → exit 1, `FAIL report: counts
disagree with the map (before: report=308 map=309)`; plus (d) a removal with no `proof` block and
(e) invalid JSON, both exit 1. The checker is therefore demonstrated to fail on each condition it
claims to guard, and to pass on the frozen tree (§7.1).

## 8. Reproduce

```bash
cd <repo>
ctest --test-dir build/presets/release -N | tail -2          # Total Tests: 309
python3 tools/checks/test_coverage_map.py \
  --test-dir build/presets/release \
  --map docs/quality/test-merge-map.json                     # exit 0, PASS
```

The candidate detection itself is reproducible from the two directories alone; the audit scripts
lived in `/tmp/audit17/` (scratch, not committed) and use only the standard library. The negative
controls of §7.2 are reproducible by copying the map to `/tmp` and applying the one-line mutations
named there.

## Appendix A — per-binary inventory (57 CTest binaries, 309 cases)

<!-- GENERATED-BEGIN inventory -->
Multi-case binaries (gtest-discovered cases, `gtest_discover_tests`):

| Binary (CTest command) | Cases | Source | Labels |
|---|---|---|---|
| `test_ai_http` | 12 | `service/tests/test_ai_http.cpp` | ai |
| `test_ai_task_dao` | 15 | `service/tests/test_ai_task_dao.cpp` | — |
| `test_ai_task_service` | 13 | `service/tests/test_ai_task_service.cpp` | ai |
| `test_application_dao` | 2 | `service/tests/test_application_dao.cpp` | application |
| `test_application_http` | 2 | `service/tests/test_application_http.cpp` | application |
| `test_application_service` | 4 | `service/tests/test_application_service.cpp` | application |
| `test_config` | 15 | `backend/tests/test_config.cpp` | — |
| `test_deadline_time` | 10 | `service/tests/test_deadline_time.cpp` | job |
| `test_http_correctness` | 4 | `backend/tests/test_http_correctness.cpp` | — |
| `test_job_dao` | 9 | `service/tests/test_job_dao.cpp` | job |
| `test_job_http` | 9 | `service/tests/test_job_http.cpp` | job |
| `test_job_ingestion_dao` | 5 | `service/tests/test_job_ingestion_dao.cpp` | job |
| `test_job_ingestion_http` | 5 | `service/tests/test_job_ingestion_http.cpp` | job |
| `test_job_ingestion_service` | 7 | `service/tests/test_job_ingestion_service.cpp` | job |
| `test_job_quality` | 16 | `service/tests/test_job_quality.cpp` | job |
| `test_job_ranking_service` | 3 | `service/tests/test_job_ranking_service.cpp` | job |
| `test_job_review_http` | 7 | `service/tests/test_job_review_http.cpp` | job |
| `test_job_service` | 7 | `service/tests/test_job_service.cpp` | job |
| `test_logger` | 2 | `backend/tests/test_logger.cpp` | — |
| `test_match_result_json` | 7 | `service/tests/test_match_result_json.cpp` | ai |
| `test_match_rule` | 9 | `service/tests/test_match_rule.cpp` | — |
| `test_resume_artifact_dao` | 4 | `service/tests/test_resume_artifact_dao.cpp` | resume |
| `test_resume_dao` | 2 | `service/tests/test_resume_dao.cpp` | resume |
| `test_resume_http` | 5 | `service/tests/test_resume_http.cpp` | resume |
| `test_resume_service` | 6 | `service/tests/test_resume_service.cpp` | resume |
| `test_schema` | 15 | `backend/tests/test_schema.cpp` | external |
| `test_session_authenticator` | 3 | `service/tests/test_session_authenticator.cpp` | auth |
| `test_source_observation` | 29 | `service/tests/test_source_observation.cpp` | job |
| `test_source_sync_dao` | 14 | `service/tests/test_source_sync_dao.cpp` | job |
| `test_source_sync_e2e` | 1 | `service/tests/test_source_sync_e2e.cpp` | job |
| `test_source_sync_http` | 8 | `service/tests/test_source_sync_http.cpp` | job |
| `test_source_sync_service` | 7 | `service/tests/test_source_sync_service.cpp` | job |
| `test_sql_result` | 5 | `backend/tests/test_sql_result.cpp` | — |
| `test_tailor_rule` | 9 | `service/tests/test_tailor_rule.cpp` | resume |
| `test_user_dao` | 5 | `service/tests/test_user_dao.cpp` | user |
| `test_user_http` | 4 | `service/tests/test_user_http.cpp` | user |
| `test_user_service` | 9 | `service/tests/test_user_service.cpp` | user |

One-case binaries (`add_test`, plain `main()`, one CTest case per binary — no intra-binary
overlap is possible):

| Binary (CTest command) | Case | Source | Labels |
|---|---|---|---|
| `jobpilot_test` | `basic_test` | `service/tests/test_basic.cpp` | — |
| `test_bcrypt` | `bcrypt_test` | `backend/tests/test_bcrypt.cpp` | — |
| `test_buffer_sigpipe` | `buffer_sigpipe_test` | `backend/tests/test_buffer_sigpipe.cpp` | — |
| `test_buffer` | `buffer_test` | `backend/tests/test_buffer.cpp` | — |
| `test_event_loop` | `event_loop_test` | `backend/tests/test_event_loop.cpp` | concurrency, tsan |
| `test_file_storage` | `file_storage_test` | `backend/tests/test_file_storage.cpp` | — |
| `test_http_request` | `http_request_test` | `backend/tests/test_http_request.cpp` | — |
| `test_http_response` | `http_response_test` | `backend/tests/test_http_response.cpp` | — |
| `test_http_router` | `http_router_test` | `backend/tests/test_http_router.cpp` | — |
| `test_http_server` | `http_server_test` | `backend/tests/test_http_server.cpp` | concurrency, tsan |
| `test_multipart_form_data_parser` | `multipart_form_data_parser_test` | `backend/tests/test_multipart_form_data_parser.cpp` | — |
| `test_mysql_pool` | `mysql_pool_test` | `backend/tests/test_mysql_pool.cpp` | external, integration |
| `test_redis_client` | `redis_client_test` | `backend/tests/test_redis_client.cpp` | external, integration |
| `test_tcp_connection_hangup` | `tcp_connection_hangup_test` | `backend/tests/test_tcp_connection_hangup.cpp` | concurrency, tsan |
| `test_tcp_connection_peer_close` | `tcp_connection_peer_close_test` | `backend/tests/test_tcp_connection_peer_close.cpp` | concurrency, tsan |
| `test_tcp_connection_send` | `tcp_connection_send_test` | `backend/tests/test_tcp_connection_send.cpp` | concurrency, tsan |
| `test_tcp_connection` | `tcp_connection_test` | `backend/tests/test_tcp_connection.cpp` | concurrency, tsan |
| `test_tcp_connection_write_buffer` | `tcp_connection_write_buffer_test` | `backend/tests/test_tcp_connection_write_buffer.cpp` | concurrency, tsan |
| `test_tcp_server` | `tcp_server_test` | `backend/tests/test_tcp_server.cpp` | concurrency, tsan |
| `test_thread_pool` | `thread_pool_test` | `backend/tests/test_thread_pool.cpp` | concurrency, tsan |
<!-- GENERATED-END inventory -->

Together the two tables cover all 57 CTest binaries and all 309 cases. The one-case binaries are
listed for completeness: each is a single CTest case, so it cannot contain intra-binary overlap and
cannot satisfy the same-binary clause of the merge rule.

## Appendix B — gtest case index, 289 cases (case → `file:lines`)

<!-- GENERATED-BEGIN index -->
- **test_ai_http** (12): `AiHttpTest.PublicEndpointsRequireBearerAuthentication` L583-617; `AiHttpTest.RejectsInvalidMatchRequests` L619-676; `AiHttpTest.CreatesResolvedTaskAndIsIdempotent` L678-759; `AiHttpTest.ReusesTheOpenTaskOverHttp` L761-814; `AiHttpTest.HidesForeignTasksBehindTheSameNotFoundCode` L816-871; `AiHttpTest.RejectsMissingOrWrongInternalToken` L873-908; `AiHttpTest.EmptyQueueClaimReturnsNull` L910-926; `AiHttpTest.RunsTheWorkerLifecycleOverHttp` L928-1102; `AiHttpTest.RetryableFailRequeuesAndPermanentFailTerminates` L1104-1189; `AiHttpTest.RefusesInputWithoutALiveLease` L1191-1252; `AiHttpTest.ClampsRequestedLeaseSeconds` L1254-1296; `AiHttpTest.RejectsMalformedInternalBodies` L1298-1399
- **test_ai_task_dao** (15): `AiTaskDaoTest.CreatesPendingAndResolvedTasks` L146-181; `AiTaskDaoTest.EnforcesTaskOwnership` L183-211; `AiTaskDaoTest.ReusesOnlyOpenJobMatchTask` L213-263; `AiTaskDaoTest.ClaimsOldestPendingTaskAndHoldsLease` L265-294; `AiTaskDaoTest.NeverHandsTheSameTaskToTwoWorkers` L296-365; `AiTaskDaoTest.ReclaimsExpiredLeaseForAnotherWorker` L367-397; `AiTaskDaoTest.FailsExpiredLeaseBeyondAttemptCap` L399-429; `AiTaskDaoTest.RequeuesRetryableFailureBelowTheCap` L431-465; `AiTaskDaoTest.FailsPermanentFailureImmediately` L467-493; `AiTaskDaoTest.CompletionRequiresOwnerAndLiveLease` L495-560; `AiTaskDaoTest.CreateJobMatchTaskIfAbsentIsIdempotent` L565-606; `AiTaskDaoTest.CreateJobMatchTaskIfAbsentResolvesNow` L608-633; `AiTaskDaoTest.ClaimNextTaskOwnsItsTransaction` L635-670; `AiTaskDaoTest.CompleteOwnedTaskEnforcesOwnerAndKeepsRuleResult` L672-710; `AiTaskDaoTest.FailOwnedTaskRequeuesTerminatesAndRefusesOwner` L712-774
- **test_ai_task_service** (13): `AiTaskServiceTest.RejectsInvalidIdentifiersWithoutDaoCalls` L634-694; `AiTaskServiceTest.RejectsMissingJobResumeAndResumeText` L696-771; `AiTaskServiceTest.MapsDaoFailuresToDatabaseError` L773-844; `AiTaskServiceTest.DisabledEnhancementResolvesAtCreation` L846-915; `AiTaskServiceTest.EnabledEnhancementLeavesTheTaskPending` L917-957; `AiTaskServiceTest.ReusesTheOpenTaskForTheSameTriple` L959-998; `AiTaskServiceTest.OwnershipRefusalUsesOneNotFoundCode` L1000-1039; `AiTaskServiceTest.FailedAndDegradedTasksStillExposeTheRuleResult` L1041-1075; `AiTaskServiceTest.ClaimDelegatesAndAppliesConfiguredPolicy` L1077-1161; `AiTaskServiceTest.CompleteAndFailDelegateWithOwnerRefusal` L1163-1284; `AiTaskServiceTest.BuildsTheWorkerInputBundle` L1286-1392; `AiTaskServiceTest.RefusesInputWithoutALiveLease` L1394-1431; `AiTaskServiceTest.ReportsBrokenBundleAsInternalError` L1433-1476
- **test_application_dao** (2): `ApplicationDaoTest.CreatesAndRejectsDuplicate` L86-134; `ApplicationDaoTest.ListsFiltersStatisticsAndUpdatesStatus` L136-224
- **test_application_http** (2): `ApplicationHttpTest.CreatesListsUpdatesAndStatistics` L477-571; `ApplicationHttpTest.RejectsAuthRelationshipsAndTransitions` L573-674
- **test_application_service** (4): `ApplicationServiceTest.ValidatesRelationshipsAndDuplicate` L160-223; `ApplicationServiceTest.EnforcesStateMachineAndApplyTime` L225-299; `ApplicationServiceTest.ListsAndAggregatesOwnedApplications` L301-346; `ApplicationServiceTest.ConcurrentCreationProducesOneDuplicate` L348-422
- **test_config** (15): `ConfigTest.LoadsDefaults` L21-42; `ConfigTest.LoadsYaml` L44-87; `ConfigTest.EnvironmentOverridesYaml` L89-148; `ConfigTest.RequiresMySqlPassword` L150-155; `ConfigTest.RejectsInvalidPort` L157-165; `ConfigTest.RejectsTypeErrors` L167-175; `ConfigTest.RejectsPasswordInYaml` L177-185; `ConfigTest.ReportsMissingFile` L187-194; `ConfigTest.LoadsFile` L196-218; `ConfigTest.RejectsMalformedYaml` L220-227; `ConfigTest.RejectsInvalidStorageConfiguration` L229-244; `ConfigTest.LoadsAiDefaults` L246-254; `ConfigTest.LoadsAiYaml` L256-269; `ConfigTest.AiEnvironmentOverridesYaml` L271-291; `ConfigTest.RejectsInvalidAiConfiguration` L293-344
- **test_deadline_time** (10): `DeadlineTime.ConvertsBetweenStoredUtcAndBeijing` L36-54; `DeadlineTime.RoundTripsDateOnlyValuesWithoutShifting` L56-82; `DeadlineTime.AppliesDayMonthAndYearBoundaries` L84-125; `DeadlineTime.RejectsUnrepresentableOrInvalidInputs` L127-141; `DeadlineTime.RecognisesOnlyTheNoonStorageConvention` L143-154; `DeadlineTime.ConversionDoesNotUseTheServerTimeZone` L156-189; `DeadlineValidation.AcceptsBothKindsAndPreservesExplicitTime` L191-215; `DeadlineValidation.RejectsMismatchedOrMalformedDeadlines` L217-238; `DeadlineValidation.EnforcesEnumAndRangeDomains` L240-288; `DeadlineNormalization.DefaultsOnlyNextToAPresentDeadline` L290-317
- **test_http_correctness** (4): `HttpCorrectnessTest.HeaderLookupIsCaseInsensitive` L11-19; `HttpCorrectnessTest.ParserAcceptsLowercaseFramingHeaders` L21-40; `HttpCorrectnessTest.RejectsDuplicateContentLength` L42-57; `HttpCorrectnessTest.RejectsMixedCaseDuplicateContentLength` L59-76
- **test_job_dao** (9): `JobDaoTest.SearchesFiltersAndSortsActiveJobs` L92-161; `JobDaoTest.ProjectsSourceUrlInListAndMatchesTheDetail` L166-213; `JobDaoTest.FiltersAndSortsByThe0015AFocusFields` L217-339; `JobDaoTest.TreatsLikeWildcardsAsLiteralCharacters` L341-407; `JobDaoTest.LoadsActiveDetailAndRequirements` L409-462; `JobDaoTest.FiltersSalaryAndSkillAndLoadsBulkRequirements` L464-535; `JobDaoTest.AppliesFreshnessVisibilityAndFields` L536-678; `JobDaoTest.ReadsBackThe0014FieldsForBothDeadlineKinds` L680-818; `JobDaoTest.PublicVisibilityExcludesNeedsReviewJobs` L822-875
- **test_job_http** (9): `JobHttpTest.SearchesWithPaginationAndPercentDecoding` L493-527; `JobHttpTest.ReturnsDetailAndNotFound` L529-574; `JobHttpTest.ReturnsEmptyPageWithRealTotal` L576-594; `JobHttpTest.EnforcesKeywordLengthBoundary` L596-624; `JobHttpTest.RejectsMalformedQueryAndPath` L626-665; `JobHttpTest.FiltersBySalaryAndSkill` L667-684; `JobHttpTest.WithMatchAddsExplanationWithoutChangingTheBasePayload` L686-890; `JobHttpTest.ReturnsDeterministicRecommendations` L892-978; `JobHttpTest.AppliesFreshnessVisibilityAndResponseFields` L980-1185
- **test_job_ingestion_dao** (5): `JobIngestionDaoTest.CreatesJob` L86-112; `JobIngestionDaoTest.UpdatesExistingJobByIdempotentSourceUrl` L114-189; `JobIngestionDaoTest.ReplacesRequirementsAsSet` L191-252; `JobIngestionDaoTest.EmptyRequirementsDeletesAll` L254-291; `JobIngestionDaoTest.ObeysExternalTransactionRollback` L293-350
- **test_job_ingestion_http** (5): `JobIngestionHttpTest.RejectsAuthenticationAndInvalidRequests` L504-614; `JobIngestionHttpTest.ImportsIdempotentlyAndCompletesClosure` L616-731; `JobIngestionHttpTest.RollsBackWholeBatch` L733-784; `JobIngestionHttpTest.Validates0014ExtraFieldValues` L786-899; `JobIngestionHttpTest.RoundTripsBothDeadlineKindsThroughApi` L901-1127
- **test_job_ingestion_service** (7): `JobIngestionServiceTest.RejectsInvalidBatches` L211-272; `JobIngestionServiceTest.CreatesUpdatesAndReplacesRequirements` L274-330; `JobIngestionServiceTest.UsesOneTransactionAndRollsBackBatch` L332-383; `JobIngestionServiceTest.ValidatesThe0014ExtraFields` L385-474; `JobIngestionServiceTest.RecordsFindingsAndReviewStatus` L476-580; `JobIngestionServiceTest.RejectsUnusableInputWithADiagnostic` L582-692; `JobIngestionServiceTest.RegistryIsFailOpenAndAdvisoryStaysVisible` L694-764
- **test_job_quality** (16): `JobQualityTest.CleanJobHasNoFindingsAndStaysVisible` L79-89; `JobQualityTest.MissingCompanyGoesToReviewNotReject` L91-104; `JobQualityTest.NonHttpSourceUrlIsRejected` L106-116; `JobQualityTest.DescriptionOver48KiBIsRejectedNotTruncated` L118-133; `JobQualityTest.RootOrListUrlGoesToReviewButStaysStored` L135-146; `JobQualityTest.PlatformEmployerWithConfirmingHostGoesToReview` L152-201; `JobQualityTest.UnknownPlatformNameStaysCleanFailOpen` L203-215; `JobQualityTest.TitleEqualsCompanyGoesToReview` L217-227; `JobQualityTest.PollutedDescriptionNeedsTheConjunctiveThreshold` L229-248; `JobQualityTest.DateOnlyDeadlineWithWrongInstantNeedsReview` L250-269; `JobQualityTest.DuplicateContentInBatchFlagsBothItems` L271-286; `JobQualityTest.EvaluatorIsDeterministic` L288-301; `JobQualityTest.RegistryNormalisesSuffixesAndCase` L303-314; `JobQualityTest.NormalizeFollowsTheCanonicalTable` L319-358; `JobQualityTest.ShippedRegistryNormalizesToTheCanonicalSet` L360-399; `JobQualityTest.RegistryPathComesFromTheEnvironment` L404-443
- **test_job_ranking_service** (3): `JobRankingTest.SuppressesHardFilteredComponents` L131-187; `JobRankingServiceTest.SortsPaginatesAndReportsTruncation` L189-235; `JobRankingServiceTest.RequiresUsableProfileOrSkills` L237-250
- **test_job_review_http** (7): `JobReviewHttpTest.RejectsMissingOrWrongToken` L502-545; `JobReviewHttpTest.QueueListsReasonStatusAndIdentifiers` L547-611; `JobReviewHttpTest.ApproveMakesTheRowVisibleAndIsIdempotent` L613-684; `JobReviewHttpTest.FixValidatesAgainstTheContractsAndAudits` L686-791; `JobReviewHttpTest.RejectRequiresANoteHidesAndRetainsTheRow` L793-853; `JobReviewHttpTest.BackfillDryRunWritesNothingApplyAndRevertRestore` L855-1038; `JobReviewHttpTest.InternalTokenNeverAppearsInLogs` L1041-1112
- **test_job_service** (7): `JobServiceTest.TrimsFiltersAndBuildsPagination` L110-142; `JobServiceTest.EmptyFiltersBecomeNullopt` L144-165; `JobServiceTest.NormalizesExtendedFilters` L167-203; `JobServiceTest.RejectsInvalidPagination` L205-229; `JobServiceTest.LoadsRequirementsAndMapsMissingJob` L231-264; `JobServiceTest.MapsDaoFailures` L266-281; `JobServiceTest.NormalizesFreshnessFilters` L282-320
- **test_logger** (2): `LoggerTest.InitializesAndWrites` L12-28; `LoggerTest.RejectsUnknownLevel` L30-42
- **test_match_result_json** (7): `MatchResultJsonTest.EmitsTheDocumentedKeyOrder` L46-66; `MatchResultJsonTest.IsDeterministicForTheSameInput` L68-75; `MatchResultJsonTest.OmitsAbsentBreakdownComponents` L77-94; `MatchResultJsonTest.KeepsEmptyCollectionsAndBreakdown` L96-122; `MatchResultJsonTest.RoundTripsEveryField` L124-183; `MatchResultJsonTest.KeepsNumberPrecision` L185-208; `MatchResultJsonTest.EscapesAndPreservesUtf8` L210-246
- **test_match_rule** (9): `MatchRuleTest.IsDeterministicAndBounded` L122-152; `MatchRuleTest.IgnoresRecencyUnlikeRuleV1` L154-226; `MatchRuleTest.ReportsMatchedAndMissingSkills` L228-253; `MatchRuleTest.ReportsProfileComponentEvidence` L255-292; `MatchRuleTest.HandlesMissingProfileAndRequirements` L294-342; `MatchRuleTest.ResumeEvidenceUsesWordBoundaries` L344-371; `MatchExplanationTest.ClassifiesDeadlineUrgencyWithDateOnlyHandling` L376-454; `MatchExplanationTest.DealBreakersAreDataWithNullNeverExcluded` L456-546; `MatchExplanationTest.ReasonsCiteDeliveredFactsOnly` L548-624
- **test_resume_artifact_dao** (4): `ResumeArtifactDaoTest.CreatesFindsAndListsAnOwnedArtifact` L196-261; `ResumeArtifactDaoTest.OwnershipRejectsForeignVersionAndApplication` L263-301; `ResumeArtifactDaoTest.ConfirmationIsFactGateGuardedAndNeverTouchesVersions` L303-382; `ResumeArtifactDaoTest.PersistedGateFailureBlocksConfirmation` L386-481
- **test_resume_dao** (2): `ResumeDaoTest.CreatesListsAndVersionsOwnedResume` L57-158; `ResumeDaoTest.ListsOnlyOwnedResumeVersions` L160-224
- **test_resume_http** (5): `ResumeHttpTest.UploadsListsAndCreatesVersions` L561-666; `ResumeHttpTest.ReportsExplicitPdfTextStatus` L669-729; `ResumeHttpTest.RejectsAuthenticationAndMalformedInput` L731-791; `ResumeHttpTest.EnforcesCrossUserIsolation` L793-855; `ResumeHttpTest.ManagesTailoredArtifactsWithinTheConfirmationBoundary` L857-1101
- **test_resume_service** (6): `ResumeServiceTest.UploadsListsAndVersionsTextAndPdf` L185-286; `ResumeServiceTest.ClassifiesResumeTextStatusExplicitly` L290-377; `ResumeServiceTest.RejectsInvalidFileInput` L379-413; `ResumeServiceTest.CompensatesPromoteFailure` L415-443; `ResumeServiceTest.WriteFailureDoesNotCreateMetadata` L445-472; `ResumeServiceTest.ConcurrentVersionsAreUnique` L474-538
- **test_schema** (15): `SchemaTest.DatabaseUsesExpectedCharsetAndCollation` L132-147; `SchemaTest.CreatesAllExpectedTables` L149-192; `SchemaTest.CreatesRequiredColumns` L194-250; `SchemaTest.StoresTheFullReviewOutcomeVocabulary` L256-269; `SchemaTest.Creates0015AReviewAndDeadlineIndexes` L274-296; `SchemaTest.Creates0015ProfileCandidateColumns` L301-330; `SchemaTest.Creates0015ResumeArtifactTable` L335-405; `SchemaTest.CreatesRequiredUniqueIndexes` L407-448; `SchemaTest.CreatesFreshnessObservationColumns` L450-482; `SchemaTest.CreatesSourceScopeStatusIndex` L484-504; `SchemaTest.SourceUrlUniquenessIsUnchanged` L506-520; `SchemaTest.LegacyJobRowReadsWithFreshnessDefaults` L522-562; `SchemaTest.Creates0014JobColumnsAndNoDeferredIndex` L564-610; `SchemaTest.LegacyJobRowReadsWithNull0014Fields` L612-655; `SchemaTest.CreatesAiTaskLeaseColumnsAndIndex` L657-727
- **test_session_authenticator** (3): `SessionAuthenticatorTest.ResolvesSessionUserId` L48-63; `SessionAuthenticatorTest.RejectsMissingSession` L65-79; `SessionAuthenticatorTest.ParsesBearerHeader` L81-96
- **test_source_observation** (29): `SourceObservationValidation.AcceptsCompleteObservationWithJobs` L77-80; `SourceObservationValidation.AcceptsCompleteEmptyBoard` L82-88; `SourceObservationValidation.RejectsIncompleteEmptyBoard` L90-97; `SourceObservationValidation.AcceptsIncompletePartialBatch` L99-105; `SourceObservationSources.AcceptsOnlyConfiguredProviders` L107-117; `SourceObservationValidation.AcceptsAllSupportedSources` L119-131; `SourceObservationValidation.RejectsUnsupportedOrEmptySource` L133-144; `SourceObservationSources.KeepsTheAllowlistClosedUntilASourceIsVerified` L146-161; `SourceObservationValidation.RequiresEveryJobToCarryTheObservationSource` L163-185; `SourceObservationValidation.RejectsSourceTooLong` L187-193; `SourceObservationValidation.EnforcesSourceScopeBounds` L195-208; `DbTimestamp.ValidatesFormatAndRanges` L210-230; `SourceObservationValidation.RejectsMalformedObservedAt` L232-238; `SourceObservationValidation.RejectsEmptyObservedAt` L240-246; `SourceObservationValidation.EnforcesBatchLimits` L248-267; `SourceObservationValidation.RejectsJobSourceMismatch` L269-275; `SourceObservationValidation.RejectsDuplicateSourceUrl` L277-299; `SourceObservationValidation.RejectsInvalidJobFields` L301-345; `SourceObservationValidation.EnforcesObservationFieldBounds` L347-364; `SourceObservationValidation.ValidatesThe0014ExtraFields` L366-412; `SourceObservationValidation.RejectsInvalidRequirements` L414-437; `SourceStatusStrings.ExposesContractSpelling` L439-445; `SourceStatusStrings.RoundTripsKnownValues` L447-462; `SourceStatusStrings.RejectsUnknownSpelling` L464-472; `FreshnessStrings.ExposesContractSpelling` L474-480; `FreshnessClassification.AppliesPolicyBoundaries` L482-503; `FreshnessClassification.HandlesClosedAndUnobservedJobs` L505-522; `MissingClosePolicy.AppliesCountAndGraceThresholds` L524-536; `MissingClosePolicy.HonorsCustomPolicy` L538-546
- **test_source_sync_dao** (14): `SourceSyncDaoTest.FirstObservationCreatesJobsWithFreshnessFields` L162-207; `SourceSyncDaoTest.RepeatedObservationUpdatesWithoutDuplicating` L209-243; `SourceSyncDaoTest.CompleteAbsenceMarksMissingWithoutClosing` L245-269; `SourceSyncDaoTest.SecondCompleteAbsenceCloses` L271-299; `SourceSyncDaoTest.GraceWindowClosesOnFirstAbsence` L301-322; `SourceSyncDaoTest.GraceBoundaryAtFortyEightHoursDoesNotClose` L324-345; `SourceSyncDaoTest.IncompleteObservationIsIgnored` L347-371; `SourceSyncDaoTest.IncompleteObservationDoesNotAdvanceMissingCount` L373-402; `SourceSyncDaoTest.ReappearanceReopensAndResetsCounters` L404-441; `SourceSyncDaoTest.ScopeIsolationPreventsClosingOtherBoards` L443-482; `SourceSyncDaoTest.DoesNotTouchOtherSourcesOrManualJobs` L484-520; `SourceSyncDaoTest.ManualRecordIsUnchangedByACollidingObservation` L522-637; `SourceSyncDaoTest.KeepsObserved0014FieldsWhenALaterFetchOmitsThem` L639-699; `SourceSyncDaoTest.RollbackDiscardsObservation` L701-717
- **test_source_sync_e2e** (1): `SourceSyncE2ETest.SyncVisibilityDisappearanceAndReopen` L507-718
- **test_source_sync_http** (8): `SourceSyncHttpTest.RejectsMissingOrWrongToken` L493-537; `SourceSyncHttpTest.RejectsMalformedAndInvalidObservations` L539-682; `SourceSyncHttpTest.AppliesCompleteAndIncompleteObservations` L684-831; `SourceSyncHttpTest.ClosesAfterGraceWindowWhenBoardIsEmpty` L833-878; `SourceSyncHttpTest.KeepsOtherScopesUntouched` L880-927; `SourceSyncHttpTest.ParsesAndRejectsThe0014ExtraFields` L929-1035; `SourceSyncHttpTest.KeepsObservedDeadlineWhenANewFetchOmitsIt` L1037-1098; `SourceSyncHttpTest.ManualRecordWinsOverObservation` L1100-1224
- **test_source_sync_service** (7): `SourceSyncServiceTest.RejectsInvalidObservationsWithoutCallingDao` L181-230; `SourceSyncServiceTest.PropagatesCountersAndCompleteness` L232-260; `SourceSyncServiceTest.IncompleteObservationNeverCloses` L262-284; `SourceSyncServiceTest.EmptyCompleteBoardStillRunsClosePass` L286-308; `SourceSyncServiceTest.PreservesObservationIdentity` L310-354; `SourceSyncServiceTest.UsesOneTransactionAndRollsBackOnFailure` L356-384; `SourceSyncServiceTest.RejectsWriteNothingAndPreserveManualPriority` L389-503
- **test_sql_result** (5): `SqlResultTest.StoresColumnsAndRows` L11-30; `SqlResultTest.PreservesNullAndEmptyString` L32-43; `SqlResultTest.RejectsMismatchedRow` L45-54; `SqlResultTest.ThrowsOnInvalidRowIndex` L56-65; `SqlResultTest.ThrowsOnInvalidColumnIndex` L67-76
- **test_tailor_rule** (9): `TailorRuleTest.ProducesADeterministicDraftWithNoAiInput` L103-132; `TailorRuleTest.EveryFactCitesItsSourceAndAppearsInTheContent` L134-158; `TailorRuleTest.NothingDerivableBecomesAPlaceholderNotAClaim` L160-201; `TailorRuleTest.RequirementsBecomeMatchedAndMissingStatements` L203-237; `TailorRuleTest.RendersTheAtsPlainTextContract` L239-303; `TailorRuleTest.FactGatePassesTheEnginesOwnDraft` L306-332; `TailorRuleTest.FactGateFailsAnInjectedUnsourcedClaim` L334-364; `TailorRuleTest.FactGateFailsUnknownProvenanceAndDanglingFacts` L366-407; `TailorRuleTest.FactGateTreatsPlaceholdersAsNonClaims` L409-436
- **test_user_dao** (5): `UserDaoTest.CreatesAndFindsUser` L54-84; `UserDaoTest.RejectsDuplicateEmail` L86-101; `UserDaoTest.ReturnsNulloptForMissingUser` L103-110; `UserDaoTest.UpsertsAndReadsProfile` L112-171; `UserDaoTest.UpsertsListsAndDeletesScopedSkills` L173-241
- **test_user_http** (4): `UserHttpTest.RegistersLogsInAndReturnsCurrentUser` L481-547; `UserHttpTest.ReturnsLockedBusinessErrors` L549-624; `UserHttpTest.ManagesProfileAndSkillsWithOwnership` L626-821; `UserHttpTest.RejectsInvalidProfileAndSkillRequests` L823-936
- **test_user_service** (9): `UserServiceTest.RegistersHashedUser` L113-140; `UserServiceTest.RejectsDuplicateAndInvalidRegistration` L142-186; `UserServiceTest.LogsInAndCreatesSession` L188-240; `UserServiceTest.ReturnsCurrentUserAndRejectsInvalidToken` L242-280; `UserServiceTest.RejectsExpiredSession` L282-319; `UserServiceTest.SupportsConcurrentRegistrationAndSessions` L321-438; `UserServiceTest.AuthenticatesAndUpsertsProfile` L440-516; `UserServiceTest.ValidatesCandidateProfileRanges` L520-633; `UserServiceTest.ManagesSkillsWithValidationAndIsolation` L635-745
<!-- GENERATED-END index -->
