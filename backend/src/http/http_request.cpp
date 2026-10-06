#include "jobpilot/http/http_request.h"

#include <algorithm>
#include <cctype>

namespace jobpilot::core
{
    namespace
    {
        bool EqualsCaseInsensitive(
            const std::string &left,
            const std::string &right)
        {
            if (left.size() != right.size())
            {
                return false;
            }

            return std::equal(
                left.begin(),
                left.end(),
                right.begin(),
                [](unsigned char lhs, unsigned char rhs)
                {
                    return std::tolower(lhs) ==
                           std::tolower(rhs);
                });
        }

    } // namespace

    HttpRequest::HttpRequest()
        : method_(HttpMethod::kInvalid),
          path_(),
          version_(HttpVersion::kInvalid),
          headers_(),
          body_(),
          keepAlive_(false)
    {
    }

    void HttpRequest::SetMethod(HttpMethod method)
    {
        method_ = method;
    }

    void HttpRequest::SetPath(const std::string &path)
    {
        path_ = path;
    }

    void HttpRequest::SetVersion(HttpVersion version)
    {
        version_ = version;
    }

    void HttpRequest::SetHeader(
        const std::string &name,
        const std::string &value)
    {
        headers_[name] = value;
    }

    void HttpRequest::SetBody(const std::string &body)
    {
        body_ = body;
    }

    void HttpRequest::SetKeepAlive(bool keepAlive)
    {
        keepAlive_ = keepAlive;
    }

    HttpMethod HttpRequest::Method() const
    {
        return method_;
    }

    const std::string &HttpRequest::Path() const
    {
        return path_;
    }

    HttpVersion HttpRequest::Version() const
    {
        return version_;
    }

    bool HttpRequest::HasHeader(const std::string &name) const
    {
        for (const auto &entry : headers_)
        {
            if (EqualsCaseInsensitive(
                    entry.first,
                    name))
            {
                return true;
            }
        }

        return false;
    }

    std::string HttpRequest::GetHeader(
        const std::string &name) const
    {
        for (const auto &entry : headers_)
        {
            if (EqualsCaseInsensitive(
                    entry.first,
                    name))
            {
                return entry.second;
            }
        }

        return "";
    }

    const HttpRequest::HeaderMap &HttpRequest::Headers() const
    {
        return headers_;
    }

    const std::string &HttpRequest::Body() const
    {
        return body_;
    }

    bool HttpRequest::KeepAlive() const
    {
        return keepAlive_;
    }

} // namespace jobpilot::core
