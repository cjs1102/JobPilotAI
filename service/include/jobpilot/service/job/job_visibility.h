#pragma once

namespace jobpilot::service
{
    // 0015A public-visibility rollback switch (JOB_PUBLIC_INCLUDE_NEEDS_REVIEW).
    //
    // `false` - the default - means every PUBLIC read returns clean rows only
    // (`review_status = 0`): the job list, the recommended list, the by-id
    // detail, the requirements that travel with it, AI match creation and
    // resume tailoring, and the job-existence check an application makes.
    //
    // `true` restores the pre-0015A visibility for those public reads only and
    // never changes stored data (ExecPlan 0015A section 10).
    //
    // Internal surfaces never consult this switch: the review queue, the review
    // decision and the AI worker's task input read rows regardless of their
    // review state, which is what keeps a queued job reviewable and processable.
    bool PublicVisibilityIncludesNeedsReview();

} // namespace jobpilot::service
