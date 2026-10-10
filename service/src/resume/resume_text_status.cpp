#include "jobpilot/service/resume/resume_text_status.h"

#include <cctype>
#include <cstddef>

namespace jobpilot::service
{
    namespace
    {
        bool IsBlank(
            const std::string &value)
        {
            for (const char character : value)
            {
                if (std::isspace(
                        static_cast<unsigned char>(character)) == 0)
                {
                    return false;
                }
            }

            return true;
        }

        std::string LowercaseExtension(
            const std::string &filePath)
        {
            const std::size_t dot = filePath.find_last_of('.');

            if (dot == std::string::npos)
            {
                return std::string();
            }

            std::string extension;

            for (const char character : filePath.substr(dot))
            {
                extension.push_back(
                    static_cast<char>(
                        std::tolower(
                            static_cast<unsigned char>(character))));
            }

            return extension;
        }
    } // namespace

    const char *ToString(ResumeTextStatus status)
    {
        switch (status)
        {
        case ResumeTextStatus::kExtracted:
            return "ok";
        case ResumeTextStatus::kTextEmpty:
            return "text_empty";
        case ResumeTextStatus::kPdfNoText:
            return "pdf_no_text";
        case ResumeTextStatus::kUnavailable:
            return "text_unavailable";
        }

        return "text_unavailable";
    }

    bool TextExtracted(ResumeTextStatus status)
    {
        return status == ResumeTextStatus::kExtracted;
    }

    ResumeTextStatus ClassifyResumeText(
        const std::optional<std::string> &content,
        const std::string &filePath)
    {
        if (content.has_value())
        {
            return IsBlank(content.value())
                       ? ResumeTextStatus::kTextEmpty
                       : ResumeTextStatus::kExtracted;
        }

        // No stored content. A PDF is the documented, explicit degradation;
        // anything else stays explicit too instead of becoming a silent NULL.
        return LowercaseExtension(filePath) == ".pdf"
                   ? ResumeTextStatus::kPdfNoText
                   : ResumeTextStatus::kUnavailable;
    }
} // namespace jobpilot::service
