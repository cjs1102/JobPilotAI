#pragma once

#include <optional>
#include <string>

namespace jobpilot::service
{
    // 0015 Phase A step A3, owner decision 4 option (b): the explicit degraded
    // PDF text contract. `resume_versions.content` stays NULL for PDFs (no
    // extraction in this unit), so every version response carries a
    // machine-readable state instead of a silent `content = NULL`:
    // a caller can always tell "no text was extracted" from "text extracted and
    // empty". Nothing new is stored and no dependency is added.
    enum class ResumeTextStatus
    {
        // Text is available (the `.txt`/`.md` upload path, or a JSON content
        // version with non-blank text).
        kExtracted,

        // The text path ran but the result is empty/whitespace only.
        kTextEmpty,

        // A PDF was accepted as-is; no extraction is performed in this unit, so
        // the stored content is NULL by contract.
        kPdfNoText,

        // Content is NULL for any other reason. Explicit so that no NULL path is
        // ever silent.
        kUnavailable
    };

    const char *ToString(ResumeTextStatus status);

    // True only when text is actually available for matching.
    bool TextExtracted(ResumeTextStatus status);

    // Pure derivation from the stored content and the recorded file path; the
    // same seam is reused by the phase B match input bundle.
    ResumeTextStatus ClassifyResumeText(
        const std::optional<std::string> &content,
        const std::string &filePath);
}
