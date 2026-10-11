#include "jobpilot/service/job/job_quality.h"

#include "jobpilot/service/job/job_visibility.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <map>
#include <string>
#include <utility>

namespace jobpilot::service
{
    namespace
    {
        constexpr const char *kCompanyMissing = "company_missing";
        constexpr const char *kDescriptionTooLarge = "description_too_large";
        constexpr const char *kSourceUrlUnusable = "source_url_unusable";
        constexpr const char *kSourceUrlNotDetail = "source_url_not_detail";
        constexpr const char *kSourceUrlUnrecognised =
            "source_url_shape_unrecognised";
        constexpr const char *kEmployerIsPlatform = "employer_is_platform";
        constexpr const char *kPlatformHostUnverified =
            "platform_host_unverified";
        constexpr const char *kEmployerImplausible = "employer_implausible";
        constexpr const char *kTitleEqualsCompany = "title_equals_company";
        constexpr const char *kDescriptionPolluted = "description_polluted";
        constexpr const char *kDescriptionMarkerMinor =
            "description_marker_minor";
        constexpr const char *kDeadlineFlagInconsistent =
            "deadline_flag_inconsistent";
        constexpr const char *kDuplicateContentInBatch =
            "duplicate_content_in_batch";

        // Navigation / recommendation markers (versioned, docs/04 section 5.1).
        const std::vector<std::string> &PollutionMarkers()
        {
            static const std::vector<std::string> markers = {
                "推荐岗位", "相关职位", "热门职位", "猜你喜欢",
                "浏览更多", "登录注册"};
            return markers;
        }

        bool IsSpace(
            char value)
        {
            return std::isspace(
                       static_cast<unsigned char>(value)) != 0;
        }

        std::string Trim(
            const std::string &value)
        {
            std::size_t begin = 0;
            std::size_t end = value.size();

            while (begin < end && IsSpace(value[begin]))
            {
                ++begin;
            }

            while (end > begin && IsSpace(value[end - 1]))
            {
                --end;
            }

            return value.substr(begin, end - begin);
        }

        bool EndsWith(
            const std::string &value,
            const std::string &suffix)
        {
            return value.size() >= suffix.size() &&
                   value.compare(
                       value.size() - suffix.size(),
                       suffix.size(),
                       suffix) == 0;
        }

        bool StartsWith(
            const std::string &value,
            const std::string &prefix)
        {
            return value.rfind(prefix, 0) == 0;
        }

        std::size_t CountOccurrences(
            const std::string &haystack,
            const std::string &needle)
        {
            if (needle.empty())
            {
                return 0;
            }

            std::size_t count = 0;
            std::size_t position = haystack.find(needle);

            while (position != std::string::npos)
            {
                ++count;
                position = haystack.find(
                    needle,
                    position + needle.size());
            }

            return count;
        }

        bool HasDetailMarker(
            const std::string &path)
        {
            static const std::vector<std::string> markers = {
                "/job/", "/jobs/", "/position", "/detail",
                "/jobinfo", "/job-detail"};

            for (const std::string &marker : markers)
            {
                if (path.find(marker) != std::string::npos)
                {
                    return true;
                }
            }

            return false;
        }

        bool HasIdLikeSegment(
            const std::string &path)
        {
            std::size_t run = 0;

            for (const char character : path)
            {
                if (std::isdigit(
                        static_cast<unsigned char>(character)) != 0)
                {
                    ++run;

                    if (run >= 4)
                    {
                        return true;
                    }
                }
                else
                {
                    run = 0;
                }
            }

            return false;
        }

        bool IsClearlyNotDetail(
            const std::string &path,
            bool hasQuery)
        {
            if (path.empty() || path == "/")
            {
                return true;
            }

            const std::string trimmed =
                path.back() == '/' ? path.substr(0, path.size() - 1)
                                   : path;

            if (trimmed.empty())
            {
                return true;
            }

            static const std::vector<std::string> listPaths = {
                "/jobs", "/job", "/search", "/list", "/positions"};

            for (const std::string &candidate : listPaths)
            {
                if (trimmed == candidate)
                {
                    return true;
                }
            }

            // A query-only URL (`?keyword=...`) is never a job detail page.
            if (hasQuery && trimmed.find('/') == std::string::npos)
            {
                return true;
            }

            return false;
        }

        void AddFinding(
            JobQualityReport *report,
            const char *code,
            std::string reason,
            JobQualityOutcome outcome)
        {
            JobQualityFinding finding;
            finding.code = code;
            finding.reason = std::move(reason);
            finding.outcome = outcome;
            report->findings.push_back(std::move(finding));

            if (outcome == JobQualityOutcome::kReject)
            {
                report->rejected = true;

                if (report->diagnosticCode.empty())
                {
                    report->diagnosticCode = code;
                }
            }
            else if (outcome == JobQualityOutcome::kNeedsReview)
            {
                report->reviewStatus = 1;
            }
        }

        // Host matching is exact/subdomain on a lower-cased, port-less,
        // www-less host; never fuzzy.
        std::string NormalizeHost(
            const std::string &value)
        {
            std::string normalized;

            for (const char character : Trim(value))
            {
                normalized.push_back(
                    static_cast<char>(
                        std::tolower(
                            static_cast<unsigned char>(character))));
            }

            while (!normalized.empty() &&
                   normalized.back() == '.')
            {
                normalized.pop_back();
            }

            if (StartsWith(normalized, "www."))
            {
                normalized = normalized.substr(4);
            }

            return normalized;
        }

        // Authority of an absolute URL, without userinfo/port.
        std::string HostOf(
            const std::string &url)
        {
            const std::size_t scheme = url.find("://");

            if (scheme == std::string::npos)
            {
                return std::string();
            }

            const std::size_t start = scheme + 3;
            const std::size_t end =
                url.find_first_of("/?#", start);

            std::string authority =
                url.substr(
                    start,
                    end == std::string::npos
                        ? std::string::npos
                        : end - start);

            const std::size_t at = authority.find('@');

            if (at != std::string::npos)
            {
                authority = authority.substr(at + 1);
            }

            const std::size_t colon = authority.find(':');

            if (colon != std::string::npos)
            {
                authority = authority.substr(0, colon);
            }

            return NormalizeHost(authority);
        }
    } // namespace

    const char *ToString(
        JobQualityOutcome outcome)
    {
        switch (outcome)
        {
        case JobQualityOutcome::kReject:
            return "rejected";
        case JobQualityOutcome::kNeedsReview:
            return "needs_review";
        case JobQualityOutcome::kAdvisory:
        default:
            return "accepted_advisory";
        }
    }

    std::string PlatformNameRegistry::Normalize(
        const std::string &value)
    {
        std::string normalized;

        for (const char character : Trim(value))
        {
            normalized.push_back(
                static_cast<char>(
                    std::tolower(
                        static_cast<unsigned char>(character))));
        }

        static const std::vector<std::string> suffixes = {
            "招聘网", "人才网", "官网", "招聘", "网"};

        bool stripped = true;

        while (stripped)
        {
            stripped = false;

            for (const std::string &suffix : suffixes)
            {
                if (EndsWith(normalized, suffix) &&
                    normalized.size() > suffix.size())
                {
                    normalized.resize(
                        normalized.size() - suffix.size());
                    stripped = true;
                    break;
                }
            }
        }

        return normalized;
    }

    PlatformNameRegistry PlatformNameRegistry::FromNames(
        std::vector<std::string> names)
    {
        PlatformNameRegistry registry;

        for (const std::string &name : names)
        {
            const std::string normalized = Normalize(name);

            if (!normalized.empty())
            {
                registry.names_.insert(normalized);
            }
        }

        return registry;
    }

    PlatformNameRegistry PlatformNameRegistry::Load(
        const std::string &path,
        std::string *warning)
    {
        // The registry file is a simple versioned list; this reader accepts
        // exactly that shape (comments, `platform_names:` then `- value`
        // entries). Python reads the same file with PyYAML. On any failure the
        // registry is empty and the caller is warned: unknown names never
        // trigger a review (fail-open).
        std::ifstream stream(path);

        if (!stream.is_open())
        {
            if (warning != nullptr)
            {
                *warning =
                    "platform-name registry unavailable: " + path;
            }

            return PlatformNameRegistry{};
        }

        std::vector<std::string> collected;
        std::map<std::string, std::vector<std::string>> hosts;
        std::string line;

        enum class Section
        {
            kNone,
            kNames,
            kHosts
        };

        Section section = Section::kNone;

        while (std::getline(stream, line))
        {
            const std::string trimmed = Trim(line);

            if (trimmed.empty() || trimmed[0] == '#')
            {
                continue;
            }

            if (trimmed == "platform_names:")
            {
                section = Section::kNames;
                continue;
            }

            if (trimmed == "platform_hosts:")
            {
                section = Section::kHosts;
                continue;
            }

            if (section == Section::kNames)
            {
                if (trimmed.size() >= 2 &&
                    trimmed[0] == '-' &&
                    (trimmed[1] == ' ' || trimmed[1] == '\t'))
                {
                    std::string value = Trim(trimmed.substr(2));

                    if (value.size() >= 2 &&
                        ((value.front() == '"' && value.back() == '"') ||
                         (value.front() == '\'' && value.back() == '\'')))
                    {
                        value = value.substr(1, value.size() - 2);
                    }

                    if (!value.empty())
                    {
                        collected.push_back(value);
                    }

                    continue;
                }

                section = Section::kNone;
            }

            if (section == Section::kHosts)
            {
                const std::size_t colon = trimmed.find(':');

                if (colon == std::string::npos)
                {
                    section = Section::kNone;
                    continue;
                }

                std::string key = Trim(trimmed.substr(0, colon));

                if (key.size() >= 2 &&
                    ((key.front() == '"' && key.back() == '"') ||
                     (key.front() == '\'' && key.back() == '\'')))
                {
                    key = key.substr(1, key.size() - 2);
                }

                const std::string list =
                    Trim(trimmed.substr(colon + 1));

                if (key.empty() || list.empty())
                {
                    continue;
                }

                std::vector<std::string> entries;
                std::size_t start = 0;

                while (start <= list.size())
                {
                    const std::size_t comma = list.find(',', start);
                    const std::size_t end =
                        comma == std::string::npos
                            ? list.size()
                            : comma;

                    std::string entry =
                        Trim(list.substr(start, end - start));

                    if (entry.size() >= 2 &&
                        ((entry.front() == '"' && entry.back() == '"') ||
                         (entry.front() == '\'' && entry.back() == '\'')))
                    {
                        entry = entry.substr(1, entry.size() - 2);
                    }

                    if (!entry.empty())
                    {
                        entries.push_back(NormalizeHost(entry));
                    }

                    if (comma == std::string::npos)
                    {
                        break;
                    }

                    start = comma + 1;
                }

                if (!entries.empty())
                {
                    hosts[Normalize(key)] = std::move(entries);
                }

                continue;
            }
        }

        if (collected.empty() && warning != nullptr)
        {
            *warning =
                "platform-name registry has no platform_names entries: " +
                path;
        }

        PlatformNameRegistry registry =
            FromNames(std::move(collected));
        registry.hosts_ = std::move(hosts);
        return registry;
    }
    PlatformNameRegistry PlatformNameRegistry::LoadFromEnvironment(
        std::string *warning)
    {
        const char *configured =
            std::getenv(kPlatformNamesFileEnv);

        if (configured != nullptr && *configured != '\0')
        {
            return Load(configured, warning);
        }

        return Load(kDefaultPlatformNamesFile, warning);
    }

    bool PlatformNameRegistry::Contains(
        const std::string &company) const
    {
        const std::string normalized = Normalize(company);

        return !normalized.empty() &&
               names_.find(normalized) != names_.end();
    }

    const char *JobQualityRejectField(
        const std::string &code)
    {
        if (code == "company_missing" ||
            code == "company_too_large")
        {
            return "company";
        }
        if (code == "title_too_large")
        {
            return "title";
        }

        if (code == "location_too_large")
        {
            return "location";
        }

        if (code == "source_url_unusable" ||
            code == "source_url_too_large")
        {
            return "source_url";
        }

        if (code == "description_too_large")
        {
            return "description";
        }

        if (code == "skill_too_large")
        {
            return "requirements.skill";
        }

        if (code == "source_unsupported")
        {
            return "source";
        }

        if (code == "salary_range_invalid")
        {
            return "salary_max";
        }

        if (code == "duplicate_source_url")
        {
            return "source_url";
        }

        if (code == "batch_size_invalid")
        {
            return "jobs";
        }

        return "";
    }

    std::size_t JobQualityRejectLimitBytes(
        const std::string &code)
    {
        if (code == "description_too_large")
        {
            return kMaxDescriptionBytes;
        }

        if (code == "company_too_large" ||
            code == "title_too_large")
        {
            return 128;
        }

        if (code == "location_too_large" ||
            code == "skill_too_large")
        {
            return 64;
        }

        if (code == "source_url_too_large")
        {
            return 255;
        }

        return 0;
    }

    std::size_t PlatformNameRegistry::Size() const
    {
        return names_.size();
    }

    std::size_t PlatformNameRegistry::HostMappingCount() const
    {
        return hosts_.size();
    }

    bool PlatformNameRegistry::HostConfirmsPlatform(
        const std::string &host,
        const std::string &normalizedPlatform) const
    {
        const std::string normalized = NormalizeHost(host);

        if (normalized.empty() || normalizedPlatform.empty())
        {
            return false;
        }

        const auto mapping = hosts_.find(normalizedPlatform);

        if (mapping == hosts_.end())
        {
            return false;
        }

        for (const std::string &candidate : mapping->second)
        {
            if (candidate.empty())
            {
                continue;
            }

            if (normalized == candidate)
            {
                return true;
            }

            if (normalized.size() > candidate.size() &&
                normalized.compare(
                    normalized.size() - candidate.size(),
                    candidate.size(),
                    candidate) == 0 &&
                normalized[normalized.size() -
                           candidate.size() - 1] == '.')
            {
                return true;
            }
        }

        return false;
    }

    JobQualityReport EvaluateJobQuality(
        const JobImportData &job,
        const PlatformNameRegistry &platforms)
    {
        JobQualityReport report;

        const std::string company = Trim(job.company);

        if (company.empty())
        {
            // 0015A option B: a missing employer is a FIXABLE data-quality
            // problem, not unusable input: the row is stored hidden and a
            // reviewer supplies the real employer. Reject is reserved for input
            // that cannot be handled safely at all.
            AddFinding(
                &report,
                kCompanyMissing,
                "employer is missing after normalisation; a reviewer must "
                "supply the real employer",
                JobQualityOutcome::kNeedsReview);
        }

        if (job.description.has_value())
        {
            const std::string &description = job.description.value();

            if (description.size() > kMaxDescriptionBytes)
            {
                AddFinding(
                    &report,
                    kDescriptionTooLarge,
                    "description is " +
                        std::to_string(description.size()) +
                        " bytes, over the " +
                        std::to_string(kMaxDescriptionBytes) +
                        " byte cap (not truncated)",
                    JobQualityOutcome::kReject);
            }
            else if (!description.empty())
            {
                std::size_t markerBytes = 0;
                std::size_t distinct = 0;

                for (const std::string &marker : PollutionMarkers())
                {
                    const std::size_t hits =
                        CountOccurrences(description, marker);

                    if (hits > 0)
                    {
                        ++distinct;
                        markerBytes += hits * marker.size();
                    }
                }

                const std::size_t sharePercent =
                    description.empty()
                        ? 0
                        : (markerBytes * 100) / description.size();

                if (distinct >= 3 && sharePercent >= 20)
                {
                    AddFinding(
                        &report,
                        kDescriptionPolluted,
                        "description contains " +
                            std::to_string(distinct) +
                            " navigation/recommendation markers (" +
                            std::to_string(sharePercent) +
                            "% of its bytes)",
                        JobQualityOutcome::kNeedsReview);
                }
                else if (distinct > 0)
                {
                    AddFinding(
                        &report,
                        kDescriptionMarkerMinor,
                        std::to_string(distinct) +
                            " minor navigation marker(s); kept verbatim",
                        JobQualityOutcome::kAdvisory);
                }
            }
        }

        const std::string url = Trim(job.sourceUrl);

        if (!StartsWith(url, "http://") &&
            !StartsWith(url, "https://"))
        {
            AddFinding(
                &report,
                kSourceUrlUnusable,
                "source_url is not an absolute http(s) URL",
                JobQualityOutcome::kReject);
        }
        else
        {
            const std::size_t authorityEnd =
                url.find('/', url.find("//") + 2);
            const std::size_t queryStart = url.find('?');
            const std::string path =
                authorityEnd == std::string::npos
                    ? std::string()
                    : url.substr(
                          authorityEnd,
                          (queryStart == std::string::npos
                               ? url.size()
                               : queryStart) -
                              authorityEnd);
            const bool hasQuery =
                queryStart != std::string::npos;

            if (IsClearlyNotDetail(path, hasQuery))
            {
                AddFinding(
                    &report,
                    kSourceUrlNotDetail,
                    "source_url looks like a site root/list/search page, "
                    "not a job detail page",
                    JobQualityOutcome::kNeedsReview);
            }
            else if (!HasDetailMarker(path) &&
                     !HasIdLikeSegment(path))
            {
                AddFinding(
                    &report,
                    kSourceUrlUnrecognised,
                    "source_url shape is unrecognised; accepted as advisory",
                    JobQualityOutcome::kAdvisory);
            }
        }

        if (!company.empty())
        {
            if (platforms.Size() > 0 &&
                platforms.Contains(company))
            {
                // 0015A option B (plan section 6.1 code 2/3): the verdict is
                // asserted ONLY when the deterministic hostname mapping
                // confirms the same platform. Company, host and registry are
                // exact-matched; the real employer is never inferred from the
                // title or any other free text.
                const std::string host = HostOf(job.sourceUrl);
                const std::string normalizedCompany =
                    PlatformNameRegistry::Normalize(company);
                const bool hostConfirms =
                    platforms.HostConfirmsPlatform(
                        host,
                        normalizedCompany);

                if (hostConfirms)
                {
                    AddFinding(
                        &report,
                        kEmployerIsPlatform,
                        "employer equals a registered platform name (\"" +
                            company +
                            "\") and the source host (\"" + host +
                            "\") is that platform's own domain; the "
                            "platform is not the employer",
                        JobQualityOutcome::kNeedsReview);
                }
                else
                {
                    // Ambiguous/unknown host, or a different platform's host:
                    // do not assert a verdict. Recorded as advisory and the
                    // row stays visible (fail-open).
                    AddFinding(
                        &report,
                        kPlatformHostUnverified,
                        "employer equals a registered platform name (\"" +
                            company +
                            "\") but the source host (\"" + host +
                            "\") does not confirm it; no verdict asserted, "
                            "kept advisory",
                        JobQualityOutcome::kAdvisory);
                }
            }

            const std::string normalized =
                PlatformNameRegistry::Normalize(company);
            bool allDigits = !normalized.empty();

            for (const char character : normalized)
            {
                if (std::isdigit(
                        static_cast<unsigned char>(character)) == 0)
                {
                    allDigits = false;
                    break;
                }
            }

            if (normalized.size() < 2 || allDigits)
            {
                AddFinding(
                    &report,
                    kEmployerImplausible,
                    "employer name looks implausible for a company",
                    JobQualityOutcome::kAdvisory);
            }

            if (PlatformNameRegistry::Normalize(job.title) ==
                    normalized &&
                !normalized.empty())
            {
                AddFinding(
                    &report,
                    kTitleEqualsCompany,
                    "title equals employer; the real employer is unknown",
                    JobQualityOutcome::kNeedsReview);
            }
        }

        if (job.deadline.has_value() &&
            job.deadlineDateOnly.has_value() &&
            job.deadlineDateOnly.value())
        {
            const std::string &deadline = job.deadline.value();

            if (deadline.size() == 19 &&
                deadline.substr(11) != "04:00:00")
            {
                AddFinding(
                    &report,
                    kDeadlineFlagInconsistent,
                    "date-only deadline does not carry the Beijing-noon UTC "
                    "instant (F13); not auto-corrected",
                    JobQualityOutcome::kNeedsReview);
            }
        }

        return report;
    }

    void MarkDuplicateContentInBatch(
        const std::vector<JobImportData> &jobs,
        std::vector<JobQualityReport> *reports)
    {
        if (reports == nullptr || reports->size() != jobs.size())
        {
            return;
        }

        std::map<std::string, std::size_t> byContent;

        for (std::size_t index = 0; index < jobs.size(); ++index)
        {
            const JobImportData &job = jobs[index];
            const std::string key =
                PlatformNameRegistry::Normalize(job.company) + "\x1f" +
                PlatformNameRegistry::Normalize(job.title) + "\x1f" +
                PlatformNameRegistry::Normalize(
                    job.location.value_or(std::string()));

            byContent[key] += 1;
        }

        for (std::size_t index = 0; index < jobs.size(); ++index)
        {
            const JobImportData &job = jobs[index];
            const std::string key =
                PlatformNameRegistry::Normalize(job.company) + "\x1f" +
                PlatformNameRegistry::Normalize(job.title) + "\x1f" +
                PlatformNameRegistry::Normalize(
                    job.location.value_or(std::string()));

            if (byContent[key] < 2)
            {
                continue;
            }

            // Same employer+title+location with different normalised URLs.
            for (std::size_t other = 0; other < jobs.size(); ++other)
            {
                if (other == index)
                {
                    continue;
                }

                const JobImportData &candidate = jobs[other];
                const std::string otherKey =
                    PlatformNameRegistry::Normalize(candidate.company) +
                    "\x1f" +
                    PlatformNameRegistry::Normalize(candidate.title) +
                    "\x1f" +
                    PlatformNameRegistry::Normalize(
                        candidate.location.value_or(std::string()));

                if (otherKey == key &&
                    PlatformNameRegistry::Normalize(candidate.sourceUrl) !=
                        PlatformNameRegistry::Normalize(job.sourceUrl))
                {
                    AddFinding(
                        &(*reports)[index],
                        kDuplicateContentInBatch,
                        "another job in this batch has the same employer, "
                        "title and location under a different URL",
                        JobQualityOutcome::kNeedsReview);
                    break;
                }
            }
        }
    }

    JobQualitySummary SummarizeJobQuality(
        const std::vector<JobImportData> &jobs,
        const std::vector<JobQualityReport> &reports)
    {
        JobQualitySummary summary;

        const std::size_t count =
            jobs.size() < reports.size()
                ? jobs.size()
                : reports.size();

        summary.items.reserve(count);

        for (std::size_t index = 0; index < count; ++index)
        {
            const JobQualityReport &report = reports[index];

            JobQualityItemResult item;
            item.index = index;
            item.sourceUrl = jobs[index].sourceUrl;
            item.reviewStatus = report.reviewStatus;
            item.rejected = report.rejected;
            item.diagnosticCode = report.diagnosticCode;
            item.findings = report.findings;

            if (report.rejected)
            {
                ++summary.rejected;

                for (const JobQualityFinding &finding : report.findings)
                {
                    if (finding.outcome == JobQualityOutcome::kReject)
                    {
                        item.diagnosticReason = finding.reason;
                        break;
                    }
                }

                item.diagnosticField =
                    JobQualityRejectField(report.diagnosticCode);

                if (summary.diagnosticCode.empty())
                {
                    summary.diagnosticCode = item.diagnosticCode;
                    summary.diagnosticField = item.diagnosticField;
                    summary.diagnosticReason = item.diagnosticReason;
                    summary.diagnosticLimitBytes =
                        JobQualityRejectLimitBytes(
                            item.diagnosticCode);
                }
            }
            else if (report.reviewStatus == 1)
            {
                ++summary.needsReview;
            }
            else
            {
                ++summary.clean;
            }

            summary.items.push_back(std::move(item));
        }

        return summary;
    }

    bool PublicVisibilityIncludesNeedsReview()
    {
        // Read on every call, exactly like the list path has done since 0015A,
        // so the switch and the lists can never disagree about its value.
        const char *value =
            std::getenv("JOB_PUBLIC_INCLUDE_NEEDS_REVIEW");

        if (value == nullptr)
        {
            return false;
        }

        const std::string text = value;

        return text == "1" ||
               text == "true" ||
               text == "TRUE" ||
               text == "yes";
    }

} // namespace jobpilot::service
