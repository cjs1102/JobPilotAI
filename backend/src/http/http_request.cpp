#include "jobpilot/http/http_request.h"

namespace jobpilot::core
{

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
        return headers_.find(name) != headers_.end();
    }

    std::string HttpRequest::GetHeader(
        const std::string &name) const
    {
        const auto iterator = headers_.find(name);

        if (iterator == headers_.end())
        {
            return "";
        }

        return iterator->second;
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