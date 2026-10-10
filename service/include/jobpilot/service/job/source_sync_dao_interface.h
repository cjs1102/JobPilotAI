#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "jobpilot/database/mysql_pool.h"
#include "jobpilot/service/job/source_observation.h"

namespace jobpilot::service
{
    struct SourceObservationApplyResult
    {
        std::size_t created = 0;
        std::size_t updated = 0;
        std::size_t seen = 0;

        // F14 (0014): observations skipped because a source="manual" record
        // owns the collided source_url. The URLs keep the conflict
        // diagnosable in the response and in the service log.
        std::size_t skippedManual = 0;
        std::vector<std::string> skippedManualUrls;
    };

    struct SourceObservationCloseResult
    {
        std::size_t missing = 0;
        std::size_t closed = 0;
    };

    class ISourceSyncDao
    {
    public:
        virtual ~ISourceSyncDao() = default;

        // Upserts every observed job of this (source, source_scope) and
        // refreshes its freshness fields ("complete success and Job present",
        // ADR-0010 section 3). A reappearing job reopens and resets counters.
        virtual SourceObservationApplyResult ApplyObservation(
            jobpilot::core::ConnectionPool::Transaction &transaction,
            const SourceObservation &observation) = 0;

        // Marks jobs of this (source, source_scope) that the observation did
        // not contain as missing, then closes those that reached the policy
        // count or exceeded the grace window since last_seen_at. Never touches
        // another scope or source, and never runs for an incomplete
        // observation.
        virtual SourceObservationCloseResult CloseUnseenForScope(
            jobpilot::core::ConnectionPool::Transaction &transaction,
            const SourceObservation &observation,
            const MissingPolicy &policy) = 0;
    };

} // namespace jobpilot::service
