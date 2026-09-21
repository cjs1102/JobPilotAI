#pragma once

#include <string>
#include <unordered_map>

namespace jobpilot::core
{

    class HttpResponse
    {
    public:
        using HeaderMap =
            std::unordered_map<std::string, std::string>;

        HttpResponse();

        void SetStatusCode(int statusCode);
        void SetStatusMessage(const std::string &statusMessage);

        void SetHeader(
            const std::string &name,
            const std::string &value);

        void SetBody(const std::string &body);
        void SetKeepAlive(bool keepAlive);

        int StatusCode() const;

        const std::string &StatusMessage() const;

        bool HasHeader(const std::string &name) const;

        std::string GetHeader(
            const std::string &name) const;

        const HeaderMap &Headers() const;

        const std::string &Body() const;

        bool KeepAlive() const;

        std::string Serialize() const;

    private:
        int statusCode_;
        std::string statusMessage_;
        HeaderMap headers_;
        std::string body_;
        bool keepAlive_;
    };

} // namespace jobpilot::core