#include "jobpilot/http/http_response.h"

#include <sstream>

namespace jobpilot::core
{

    HttpResponse::HttpResponse()
        : statusCode_(200),
          statusMessage_("OK"),
          headers_(),
          body_(),
          keepAlive_(true)
    {
    }

    void HttpResponse::SetStatusCode(int statusCode)
    {
        statusCode_ = statusCode;
    }

    void HttpResponse::SetStatusMessage(
        const std::string &statusMessage)
    {
        statusMessage_ = statusMessage;
    }

    void HttpResponse::SetHeader(
        const std::string &name,
        const std::string &value)
    {
        headers_[name] = value;
    }

    void HttpResponse::SetBody(const std::string &body)
    {
        body_ = body;
    }

    void HttpResponse::SetKeepAlive(bool keepAlive)
    {
        keepAlive_ = keepAlive;
    }

    int HttpResponse::StatusCode() const
    {
        return statusCode_;
    }

    const std::string &HttpResponse::StatusMessage() const
    {
        return statusMessage_;
    }

    bool HttpResponse::HasHeader(const std::string &name) const
    {
        return headers_.find(name) != headers_.end();
    }

    std::string HttpResponse::GetHeader(
        const std::string &name) const
    {
        const auto iterator = headers_.find(name);

        if (iterator == headers_.end())
        {
            return "";
        }

        return iterator->second;
    }

    const HttpResponse::HeaderMap &
    HttpResponse::Headers() const
    {
        return headers_;
    }

    const std::string &HttpResponse::Body() const
    {
        return body_;
    }

    bool HttpResponse::KeepAlive() const
    {
        return keepAlive_;
    }

    std::string HttpResponse::Serialize() const
    {
        std::ostringstream stream;

        stream << "HTTP/1.1 "
               << statusCode_
               << " "
               << statusMessage_
               << "\r\n";

        for (const auto &header : headers_)
        {
            stream << header.first
                   << ": "
                   << header.second
                   << "\r\n";
        }

        stream << "Content-Length: "
               << body_.size()
               << "\r\n";

        stream << "Connection: "
               << (keepAlive_ ? "keep-alive" : "close")
               << "\r\n";

        stream << "\r\n";

        stream << body_;

        return stream.str();
    }

} // namespace jobpilot::core