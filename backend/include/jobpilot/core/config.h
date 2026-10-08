#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace jobpilot::core
{

    using Environment =
        std::unordered_map<std::string, std::string>;

    class ConfigError : public std::runtime_error
    {
    public:
        explicit ConfigError(const std::string &message)
            : std::runtime_error(message)
        {
        }
    };

    struct ServerConfig
    {
        std::string host{"0.0.0.0"};
        std::uint16_t port{8080};
    };

    struct MySqlConfig
    {
        std::string host{"localhost"};
        std::uint16_t port{3306};
        std::string database{"jobpilot"};
        std::string user{"jobpilot"};
        std::string password;
        std::size_t maxConn{10};
    };

    struct RedisConfig
    {
        std::string host{"localhost"};
        std::uint16_t port{6379};
        std::size_t maxConn{4};
        int timeoutMs{1000};
    };

    struct LoggingConfig
    {
        std::string level{"info"};
    };

    struct StorageConfig
    {
        std::string root{"storage"};
        std::size_t maxFileBytes{
            10U * 1024U * 1024U};
    };

    struct InternalApiConfig
    {
        std::string token;
    };

    // 0013 AI matching. The literal defaults match the service
    // kDefaultAiLeaseSeconds / kDefaultAiMaxAttempts contract; backend/ must
    // not depend on service headers.
    struct AiConfig
    {
        bool enhancementEnabled{false};
        std::int32_t leaseSeconds{120};
        std::int32_t maxAttempts{3};
    };

    struct Config
    {
        ServerConfig server;
        MySqlConfig mysql;
        RedisConfig redis;
        LoggingConfig log;
        StorageConfig storage;
        InternalApiConfig internalApi;
        AiConfig ai;

        static Config LoadFromFile(
            const std::string &path,
            const Environment &environment);

        static Config LoadFromString(
            const std::string &yaml,
            const Environment &environment);

        static Environment CurrentEnvironment();
    };

} // namespace jobpilot::core
