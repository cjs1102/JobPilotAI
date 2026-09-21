#pragma once

#include <string>
#include <unordered_map>

namespace jobpilot::core
{

    enum class HttpMethod
    {
        kGet,
        kPost,
        kInvalid
    };

    enum class HttpVersion
    {
        kHttp11,
        kInvalid
    };

    class HttpRequest
    {
    public:
        using HeaderMap =
            std::unordered_map<std::string, std::string>;

        HttpRequest();

        void SetMethod(HttpMethod method);
        void SetPath(const std::string &path);
        void SetVersion(HttpVersion version);
        void SetHeader(
            const std::string &name,
            const std::string &value);
        void SetBody(const std::string &body);
        void SetKeepAlive(bool keepAlive);

        HttpMethod Method() const;
        const std::string &Path() const;
        HttpVersion Version() const;

        bool HasHeader(const std::string &name) const;
        std::string GetHeader(
            const std::string &name) const;

        const HeaderMap &Headers() const;

        const std::string &Body() const;

        bool KeepAlive() const;

    private:
        HttpMethod method_;
        std::string path_;
        HttpVersion version_;
        HeaderMap headers_;
        std::string body_;
        bool keepAlive_;
    };

} // namespace jobpilot::core