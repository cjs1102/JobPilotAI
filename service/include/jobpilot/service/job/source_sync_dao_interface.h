#pragma once

#include <cstddef>

#include "jobpilot/database/mysql_pool.h"
#include "jobpilot/service/job/source_observation.h"

namespace jobpilot::service
{
    struct SourceObservationApplyResult
    {
        std::size_t created = 0;
        std::size_t updated = 0;
        std::size_t seen = 0;
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
