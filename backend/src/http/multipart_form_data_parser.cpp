#include "jobpilot/http/multipart_form_data_parser.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace jobpilot::core
{
    namespace
    {
        constexpr std::size_t kMaxBoundaryLength = 70;
        constexpr std::size_t kMaxPartCount = 8;
        constexpr std::size_t kMaxPartHeaderBytes = 8U * 1024U;

        struct Parameter
        {
            std::string name;
            std::string value;
        };

        std::string_view Trim(std::string_view value)
        {
            while (!value.empty() &&
                   std::isspace(
                       static_cast<unsigned char>(
                           value.front())))
            {
                value.remove_prefix(1);
            }

            while (!value.empty() &&
                   std::isspace(
                       static_cast<unsigned char>(
                           value.back())))
            {
                value.remove_suffix(1);
            }

            return value;
        }

        char ToLowerAscii(char value)
        {
            return static_cast<char>(
                std::tolower(
                    static_cast<unsigned char>(
                        value)));
        }

        std::string ToLower(std::string_view value)
        {
            std::string result(
                value.begin(),
                value.end());

            std::transform(
                result.begin(),
                result.end(),
                result.begin(),
                ToLowerAscii);

            return result;
        }

        bool EqualsIgnoreCase(
            std::string_view left,
            std::string_view right)
        {
            if (left.size() != right.size())
            {
                return false;
            }

            for (std::size_t i = 0;
                 i < left.size();
                 ++i)
            {
                if (ToLowerAscii(left[i]) !=
                    ToLowerAscii(right[i]))
                {
                    return false;
                }
            }

            return true;
        }

        bool ParseParameterList(
            std::string_view input,
            std::string *value,
            std::vector<Parameter> *parameters)
        {
            if (value == nullptr ||
                parameters == nullptr)
            {
                return false;
            }

            const std::size_t semicolon =
                input.find(';');

            const std::string_view main =
                Trim(
                    input.substr(
                        0,
                        semicolon));

            if (main.empty())
            {
                return false;
            }

            *value = std::string(
                main.begin(),
                main.end());

            parameters->clear();

            std::size_t position =
                semicolon == std::string_view::npos
                    ? input.size()
                    : semicolon + 1;

            while (position < input.size())
            {
                while (position < input.size() &&
                       std::isspace(
                           static_cast<unsigned char>(
                               input[position])))
                {
                    ++position;
                }

                if (position >= input.size())
                {
                    break;
                }

                const std::size_t nameStart =
                    position;

                while (position < input.size() &&
                       input[position] != '=' &&
                       input[position] != ';')
                {
                    ++position;
                }

                const std::string_view name =
                    Trim(
                        input.substr(
                            nameStart,
                            position - nameStart));

                if (name.empty())
                {
                    return false;
                }

                while (position < input.size() &&
                       std::isspace(
                           static_cast<unsigned char>(
                               input[position])))
                {
                    ++position;
                }

                if (position >= input.size() ||
                    input[position] != '=')
                {
                    return false;
                }

                ++position;

                while (position < input.size() &&
                       std::isspace(
                           static_cast<unsigned char>(
                               input[position])))
                {
                    ++position;
                }

                std::string parsedValue;

                if (position < input.size() &&
                    input[position] == '"')
                {
                    ++position;

                    bool closed = false;

                    while (position < input.size())
                    {
                        const char character =
                            input[position++];

                        if (character == '"')
                        {
                            closed = true;
                            break;
                        }

                        if (character == '\\' &&
                            position < input.size())
                        {
                            parsedValue.push_back(
                                input[position++]);
                            continue;
                        }

                        parsedValue.push_back(character);
                    }

                    if (!closed)
                    {
                        return false;
                    }
                }
                else
                {
                    const std::size_t valueStart =
                        position;

                    while (position < input.size() &&
                           input[position] != ';')
                    {
                        ++position;
                    }

                    const std::string_view parsed =
                        Trim(
                            input.substr(
                                valueStart,
                                position - valueStart));

                    if (parsed.empty())
                    {
                        return false;
                    }

                    parsedValue.assign(
                        parsed.begin(),
                        parsed.end());
                }

                parameters->push_back(
                    Parameter{
                        ToLower(name),
                        std::move(parsedValue)});

                while (position < input.size() &&
                       std::isspace(
                           static_cast<unsigned char>(
                               input[position])))
                {
                    ++position;
                }

                if (position >= input.size())
                {
                    break;
                }

                if (input[position] != ';')
                {
                    return false;
                }

                ++position;
            }

            return true;
        }

        const Parameter *FindParameter(
            const std::vector<Parameter> &parameters,
            std::string_view name)
        {
            for (const Parameter &parameter : parameters)
            {
                if (parameter.name == name)
                {
                    return &parameter;
                }
            }

            return nullptr;
        }

        bool ParseHeaders(
            std::string_view headers,
            std::string *contentDisposition,
            std::optional<std::string> *contentType)
        {
            if (contentDisposition == nullptr ||
                contentType == nullptr)
            {
                return false;
            }

            contentDisposition->clear();
            contentType->reset();

            std::size_t position = 0;

            while (position < headers.size())
            {
                const std::size_t lineEnd =
                    headers.find("\r\n", position);

                const std::size_t end =
                    lineEnd == std::string_view::npos
                        ? headers.size()
                        : lineEnd;

                const std::string_view line =
                    headers.substr(
                        position,
                        end - position);

                const std::size_t colon =
                    line.find(':');

                if (colon == std::string_view::npos ||
                    colon == 0)
                {
                    return false;
                }

                const std::string name =
                    ToLower(
                        Trim(
                            line.substr(0, colon)));

                const std::string_view value =
                    Trim(
                        line.substr(colon + 1));

                if (value.empty())
                {
                    return false;
                }

                if (name == "content-disposition")
                {
                    if (!contentDisposition->empty())
                    {
                        return false;
                    }

                    contentDisposition->assign(
                        value.begin(),
                        value.end());
                }
                else if (name == "content-type")
                {
                    if (contentType->has_value())
                    {
                        return false;
                    }

                    *contentType = std::string(
                        value.begin(),
                        value.end());
                }

                if (lineEnd == std::string_view::npos)
                {
                    break;
                }

                position = lineEnd + 2;
            }

            return !contentDisposition->empty();
        }

        bool StartsWith(
            std::string_view value,
            std::string_view prefix)
        {
            return value.size() >= prefix.size() &&
                   value.substr(0, prefix.size()) == prefix;
        }
    } // namespace

    MultipartParseResult MultipartFormDataParser::Parse(
        std::string_view contentType,
        std::string_view body,
        MultipartFormData *form)
    {
        if (form == nullptr)
        {
            return MultipartParseResult::kMalformed;
        }

        form->parts.clear();

        std::string mediaType;
        std::vector<Parameter> contentTypeParameters;

        if (!ParseParameterList(
                contentType,
                &mediaType,
                &contentTypeParameters))
        {
            return MultipartParseResult::kMalformed;
        }

        if (!EqualsIgnoreCase(
                mediaType,
                "multipart/form-data"))
        {
            return MultipartParseResult::
                kUnsupportedContentType;
        }

        const Parameter *boundary =
            FindParameter(
                contentTypeParameters,
                "boundary");

        if (boundary == nullptr ||
            boundary->value.empty() ||
            boundary->value.size() >
                kMaxBoundaryLength ||
            boundary->value.find('\r') !=
                std::string::npos ||
            boundary->value.find('\n') !=
                std::string::npos)
        {
            return MultipartParseResult::kMalformed;
        }

        const std::string delimiter =
            "--" + boundary->value;

        if (!StartsWith(body, delimiter))
        {
            return MultipartParseResult::kMalformed;
        }

        std::size_t position =
            delimiter.size();

        if (StartsWith(
                body.substr(position),
                "--"))
        {
            return MultipartParseResult::kMalformed;
        }

        if (body.substr(position, 2) != "\r\n")
        {
            return MultipartParseResult::kMalformed;
        }

        position += 2;

        bool closed = false;
        bool hasFile = false;

        while (position < body.size())
        {
            const std::size_t headerEnd =
                body.find("\r\n\r\n", position);

            if (headerEnd == std::string_view::npos)
            {
                return MultipartParseResult::kMalformed;
            }

            const std::size_t headerSize =
                headerEnd - position;

            if (headerSize > kMaxPartHeaderBytes)
            {
                return MultipartParseResult::kLimitExceeded;
            }

            if (form->parts.size() >= kMaxPartCount)
            {
                return MultipartParseResult::kLimitExceeded;
            }

            std::string disposition;
            std::optional<std::string> partContentType;

            if (!ParseHeaders(
                    body.substr(
                        position,
                        headerSize),
                    &disposition,
                    &partContentType))
            {
                return MultipartParseResult::kMalformed;
            }

            std::string dispositionType;
            std::vector<Parameter> dispositionParameters;

            if (!ParseParameterList(
                    disposition,
                    &dispositionType,
                    &dispositionParameters) ||
                !EqualsIgnoreCase(
                    dispositionType,
                    "form-data"))
            {
                return MultipartParseResult::kMalformed;
            }

            const Parameter *name =
                FindParameter(
                    dispositionParameters,
                    "name");

            if (name == nullptr ||
                name->value.empty())
            {
                return MultipartParseResult::kMalformed;
            }

            const Parameter *filename =
                FindParameter(
                    dispositionParameters,
                    "filename");

            if (filename != nullptr &&
                filename->value.empty())
            {
                return MultipartParseResult::kMalformed;
            }

            if (filename != nullptr)
            {
                if (hasFile)
                {
                    return MultipartParseResult::kMalformed;
                }

                hasFile = true;
            }

            const std::size_t bodyStart =
                headerEnd + 4;

            const std::string marker =
                "\r\n" + delimiter;

            const std::size_t nextBoundary =
                body.find(
                    marker,
                    bodyStart);

            if (nextBoundary == std::string_view::npos)
            {
                return MultipartParseResult::kMalformed;
            }

            MultipartPart part;
            part.name = name->value;
            part.contentType = partContentType;
            part.body = body.substr(
                bodyStart,
                nextBoundary - bodyStart);

            if (filename != nullptr)
            {
                part.filename = filename->value;
            }

            form->parts.push_back(
                std::move(part));

            position =
                nextBoundary +
                marker.size();

            if (StartsWith(
                    body.substr(position),
                    "--"))
            {
                position += 2;
                closed = true;

                if (position == body.size())
                {
                    break;
                }

                if (body.substr(position) == "\r\n")
                {
                    position = body.size();
                    break;
                }

                return MultipartParseResult::kMalformed;
            }

            if (body.substr(position, 2) != "\r\n")
            {
                return MultipartParseResult::kMalformed;
            }

            position += 2;
        }

        if (!closed ||
            form->parts.empty())
        {
            return MultipartParseResult::kMalformed;
        }

        return MultipartParseResult::kSuccess;
    }

} // namespace jobpilot::core
