#pragma once

#include <cstddef>
#include <cstdint>
#include <set>
#include <string>
#include <vector>

#include "jobpilot/service/job/job_ingestion_dao_interface.h"
#include "jobpilot/service/job/job_quality_types.h"

namespace jobpilot::service
{
    // 0015A outcomes/findings live in job_quality_types.h so the ingestion DAO
    // can persist them without depending on the engine.

    struct JobQualityReport
    {
        bool rejected = false;
        // 0 = clean/visible, 1 = needs_review/hidden. Quarantine (2) is a
        // reviewer decision, never an ingest verdict.
        std::int32_t reviewStatus = 0;
        std::string diagnosticCode;  // set only when rejected
        std::vector<JobQualityFinding> findings;
    };

    // Description hard cap: 48 KiB of UTF-8 bytes (docs/04 section 5.1).
    inline constexpr std::size_t kMaxDescriptionBytes = 48U * 1024U;

    // Registry path source (0015A item 2 / decision 4). The backend Config
    // parser has no quality section, so the minimal-change option is an
    // environment override with a repo-relative default; the service is
    // documented to run from the repository root.
    inline constexpr const char *kPlatformNamesFileEnv =
        "JOB_QUALITY_PLATFORM_NAMES_FILE";
    inline constexpr const char *kDefaultPlatformNamesFile =
        "source_adapters/jobpilot_sources/data/platform_names.yaml";

    // Single source of truth for platform names, loaded from the source
    // registry/config layer (source_adapters/jobpilot_sources/data/
    // platform_names.yaml) by both languages.
    class PlatformNameRegistry
    {
    public:
        // Returns an empty registry when the file is missing/unreadable and
        // fills `warning` (fail-open: unknown names never trigger review).
        static PlatformNameRegistry Load(
            const std::string &path,
            std::string *warning);

        // Resolves kPlatformNamesFileEnv, falling back to the repo-relative
        // default, then delegates to Load(). This is the composition-boundary
        // helper used when a caller does not inject a registry explicitly.
        static PlatformNameRegistry LoadFromEnvironment(
            std::string *warning);

        static PlatformNameRegistry FromNames(
            std::vector<std::string> names);

        bool Contains(
            const std::string &company) const;

        std::size_t Size() const;

        static std::string Normalize(
            const std::string &value);

    private:
        std::set<std::string> names_;
    };

    // Pure, deterministic, no I/O: same payload -> same findings.
    JobQualityReport EvaluateJobQuality(
        const JobImportData &job,
        const PlatformNameRegistry &platforms);

    // Batch-level finding: equal normalised employer+title+location with
    // different source_url is flagged on every involved item.
    void MarkDuplicateContentInBatch(
        const std::vector<JobImportData> &jobs,
        std::vector<JobQualityReport> *reports);

    // Projects per-item reports into the shared ingest-response summary
    // (ExecPlan 0015A section 5.2). The first rejected item's diagnostic wins,
    // and its byte bound (when the code is size-based) fills
    // diagnosticLimitBytes; the caller sets diagnosticActualBytes from the
    // payload.
    JobQualitySummary SummarizeJobQuality(
        const std::vector<JobImportData> &jobs,
        const std::vector<JobQualityReport> &reports);


} // namespace jobpilot::service
