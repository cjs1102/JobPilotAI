#include "jobpilot/core/config.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <sstream>
#include <type_traits>

#include <yaml-cpp/yaml.h>

namespace jobpilot::core
{
    namespace
    {
        constexpr const char *kServerHostEnv = "JOBPILOT_HOST";
        constexpr const char *kServerPortEnv = "JOBPILOT_PORT";
        constexpr const char *kMySqlHostEnv = "MYSQL_HOST";
        constexpr const char *kMySqlPortEnv = "MYSQL_PORT";
        constexpr const char *kMySqlDatabaseEnv = "MYSQL_DATABASE";
        constexpr const char *kMySqlUserEnv = "MYSQL_USER";
        constexpr const char *kMySqlPasswordEnv = "MYSQL_PASSWORD";
        constexpr const char *kMySqlMaxConnEnv = "MYSQL_MAX_CONN";
        constexpr const char *kRedisHostEnv = "REDIS_HOST";
        constexpr const char *kRedisPortEnv = "REDIS_PORT";
        constexpr const char *kRedisMaxConnEnv = "REDIS_MAX_CONN";
        constexpr const char *kRedisTimeoutEnv = "REDIS_TIMEOUT_MS";
        constexpr const char *kLogLevelEnv = "LOG_LEVEL";
        constexpr const char *kStorageRootEnv = "STORAGE_ROOT";
        constexpr const char *kStorageMaxFileBytesEnv =
            "STORAGE_MAX_FILE_BYTES";
        constexpr const char *kInternalApiTokenEnv =
            "INTERNAL_API_TOKEN";
        constexpr const char *kAiEnhancementEnabledEnv =
            "AI_ENHANCEMENT_ENABLED";
        constexpr const char *kAiLeaseSecondsEnv =
            "AI_LEASE_SECONDS";
        constexpr const char *kAiMaxAttemptsEnv =
            "AI_MAX_ATTEMPTS";

        std::string ToLower(std::string value)
        {
            std::transform(
                value.begin(),
                value.end(),
                value.begin(),
                [](unsigned char character)
                {
                    return static_cast<char>(
                        std::tolower(character));
                });

            return value;
        }

        const std::string *FindEnvironment(
            const Environment &environment,
            const char *name)
        {
            const auto iterator =
                environment.find(name);

            if (iterator == environment.end())
            {
                return nullptr;
            }

            return &iterator->second;
        }

        template <typename T>
        T ParseYamlScalar(
            const YAML::Node &node,
            const std::string &field)
        {
            try
            {
                return node.as<T>();
            }
            catch (const YAML::Exception &error)
            {
                throw ConfigError(
                    "invalid value for " + field + ": " +
                    error.what());
            }
        }

        std::uint16_t ParsePort(
            const std::string &value,
            const std::string &field)
        {
            try
            {
                std::size_t parsed = 0;

                const unsigned long port =
                    std::stoul(value, &parsed);

                if (parsed != value.size() ||
                    port == 0 ||
                    port >
                        std::numeric_limits<
                            std::uint16_t>::max())
                {
                    throw ConfigError(
                        field + " must be between 1 and 65535");
                }

                return static_cast<std::uint16_t>(port);
            }
            catch (const ConfigError &)
            {
                throw;
            }
            catch (const std::exception &)
            {
                throw ConfigError(
                    field + " must be an integer");
            }
        }

        std::size_t ParsePositiveSize(
            const std::string &value,
            const std::string &field)
        {
            try
            {
                std::size_t parsed = 0;

                const unsigned long long result =
                    std::stoull(value, &parsed);

                if (parsed != value.size() ||
                    result == 0 ||
                    result >
                        std::numeric_limits<
                            std::size_t>::max())
                {
                    throw ConfigError(
                        field + " must be a positive integer");
                }

                return static_cast<std::size_t>(result);
            }
            catch (const ConfigError &)
            {
                throw;
            }
            catch (const std::exception &)
            {
                throw ConfigError(
                    field + " must be a positive integer");
            }
        }

        int ParsePositiveInt(
            const std::string &value,
            const std::string &field)
        {
            try
            {
                std::size_t parsed = 0;

                const long result =
                    std::stol(value, &parsed);

                if (parsed != value.size() ||
                    result <= 0 ||
                    result >
                        std::numeric_limits<int>::max())
                {
                    throw ConfigError(
                        field + " must be a positive integer");
                }

                return static_cast<int>(result);
            }
            catch (const ConfigError &)
            {
                throw;
            }
            catch (const std::exception &)
            {
                throw ConfigError(
                    field + " must be a positive integer");
            }
        }

        bool ParseBool(
            const std::string &value,
            const std::string &field)
        {
            const std::string folded = ToLower(value);

            if (folded == "true" ||
                folded == "1" ||
                folded == "yes" ||
                folded == "on")
            {
                return true;
            }

            if (folded == "false" ||
                folded == "0" ||
                folded == "no" ||
                folded == "off")
            {
                return false;
            }

            throw ConfigError(
                field + " must be a boolean");
        }

        void ApplyYaml(Config *config, const YAML::Node &root)
        {
            if (!root || root.IsNull())
            {
                return;
            }

            if (!root.IsMap())
            {
                throw ConfigError(
                    "configuration root must be a mapping");
            }

            if (const YAML::Node server = root["server"])
            {
                if (!server.IsMap())
                {
                    throw ConfigError(
                        "server must be a mapping");
                }

                if (const YAML::Node host = server["host"])
                {
                    config->server.host =
                        ParseYamlScalar<std::string>(
                            host,
                            "server.host");
                }

                if (const YAML::Node port = server["port"])
                {
                    config->server.port =
                        ParsePort(
                            ParseYamlScalar<std::string>(
                                port,
                                "server.port"),
                            "server.port");
                }
            }

            if (const YAML::Node mysql = root["mysql"])
            {
                if (!mysql.IsMap())
                {
                    throw ConfigError(
                        "mysql must be a mapping");
                }

                if (mysql["password"])
                {
                    throw ConfigError(
                        "mysql.password must come from MYSQL_PASSWORD");
                }

                if (const YAML::Node host = mysql["host"])
                {
                    config->mysql.host =
                        ParseYamlScalar<std::string>(
                            host,
                            "mysql.host");
                }

                if (const YAML::Node port = mysql["port"])
                {
                    config->mysql.port =
                        ParsePort(
                            ParseYamlScalar<std::string>(
                                port,
                                "mysql.port"),
                            "mysql.port");
                }

                if (const YAML::Node database = mysql["database"])
                {
                    config->mysql.database =
                        ParseYamlScalar<std::string>(
                            database,
                            "mysql.database");
                }

                if (const YAML::Node user = mysql["user"])
                {
                    config->mysql.user =
                        ParseYamlScalar<std::string>(
                            user,
                            "mysql.user");
                }

                if (const YAML::Node maxConn = mysql["max_conn"])
                {
                    config->mysql.maxConn =
                        ParsePositiveSize(
                            ParseYamlScalar<std::string>(
                                maxConn,
                                "mysql.max_conn"),
                            "mysql.max_conn");
                }
            }

            if (const YAML::Node redis = root["redis"])
            {
                if (!redis.IsMap())
                {
                    throw ConfigError(
                        "redis must be a mapping");
                }

                if (const YAML::Node host = redis["host"])
                {
                    config->redis.host =
                        ParseYamlScalar<std::string>(
                            host,
                            "redis.host");
                }

                if (const YAML::Node port = redis["port"])
                {
                    config->redis.port =
                        ParsePort(
                            ParseYamlScalar<std::string>(
                                port,
                                "redis.port"),
                            "redis.port");
                }

                if (const YAML::Node maxConn = redis["max_conn"])
                {
                    config->redis.maxConn =
                        ParsePositiveSize(
                            ParseYamlScalar<std::string>(
                                maxConn,
                                "redis.max_conn"),
                            "redis.max_conn");
                }

                if (const YAML::Node timeout = redis["timeout_ms"])
                {
                    config->redis.timeoutMs =
                        ParsePositiveInt(
                            ParseYamlScalar<std::string>(
                                timeout,
                                "redis.timeout_ms"),
                            "redis.timeout_ms");
                }
            }

            if (const YAML::Node log = root["log"])
            {
                if (!log.IsMap())
                {
                    throw ConfigError(
                        "log must be a mapping");
                }

                if (const YAML::Node level = log["level"])
                {
                    config->log.level =
                        ParseYamlScalar<std::string>(
                            level,
                            "log.level");
                }
            }

            if (const YAML::Node storage = root["storage"])
            {
                if (!storage.IsMap())
                {
                    throw ConfigError(
                        "storage must be a mapping");
                }

                if (const YAML::Node rootPath =
                        storage["root"])
                {
                    config->storage.root =
                        ParseYamlScalar<std::string>(
                            rootPath,
                            "storage.root");
                }

                if (const YAML::Node maxFileBytes =
                        storage["max_file_bytes"])
                {
                    config->storage.maxFileBytes =
                        ParsePositiveSize(
                            ParseYamlScalar<std::string>(
                                maxFileBytes,
                                "storage.max_file_bytes"),
                            "storage.max_file_bytes");
                }
            }

            if (const YAML::Node ai = root["ai"])
            {
                if (!ai.IsMap())
                {
                    throw ConfigError(
                        "ai must be a mapping");
                }

                if (const YAML::Node enabled =
                        ai["enhancement_enabled"])
                {
                    config->ai.enhancementEnabled =
                        ParseBool(
                            ParseYamlScalar<std::string>(
                                enabled,
                                "ai.enhancement_enabled"),
                            "ai.enhancement_enabled");
                }

                if (const YAML::Node lease = ai["lease_seconds"])
                {
                    config->ai.leaseSeconds =
                        static_cast<std::int32_t>(
                            ParsePositiveInt(
                                ParseYamlScalar<std::string>(
                                    lease,
                                    "ai.lease_seconds"),
                                "ai.lease_seconds"));
                }

                if (const YAML::Node attempts =
                        ai["max_attempts"])
                {
                    config->ai.maxAttempts =
                        static_cast<std::int32_t>(
                            ParsePositiveInt(
                                ParseYamlScalar<std::string>(
                                    attempts,
                                    "ai.max_attempts"),
                                "ai.max_attempts"));
                }
            }
        }

        void ApplyEnvironment(
            Config *config,
            const Environment &environment)
        {
            if (const std::string *value =
                    FindEnvironment(
                        environment,
                        kServerHostEnv))
            {
                config->server.host = *value;
            }

            if (const std::string *value =
                    FindEnvironment(
                        environment,
                        kServerPortEnv))
            {
                config->server.port =
                    ParsePort(*value, kServerPortEnv);
            }

            if (const std::string *value =
                    FindEnvironment(
                        environment,
                        kMySqlHostEnv))
            {
                config->mysql.host = *value;
            }

            if (const std::string *value =
                    FindEnvironment(
                        environment,
                        kMySqlPortEnv))
            {
                config->mysql.port =
                    ParsePort(*value, kMySqlPortEnv);
            }

            if (const std::string *value =
                    FindEnvironment(
                        environment,
                        kMySqlDatabaseEnv))
            {
                config->mysql.database = *value;
            }

            if (const std::string *value =
                    FindEnvironment(
                        environment,
                        kMySqlUserEnv))
            {
                config->mysql.user = *value;
            }

            if (const std::string *value =
                    FindEnvironment(
                        environment,
                        kMySqlPasswordEnv))
            {
                config->mysql.password = *value;
            }

            if (const std::string *value =
                    FindEnvironment(
                        environment,
                        kMySqlMaxConnEnv))
            {
                config->mysql.maxConn =
                    ParsePositiveSize(
                        *value,
                        kMySqlMaxConnEnv);
            }

            if (const std::string *value =
                    FindEnvironment(
                        environment,
                        kRedisHostEnv))
            {
                config->redis.host = *value;
            }

            if (const std::string *value =
                    FindEnvironment(
                        environment,
                        kRedisPortEnv))
            {
                config->redis.port =
                    ParsePort(*value, kRedisPortEnv);
            }

            if (const std::string *value =
                    FindEnvironment(
                        environment,
                        kRedisMaxConnEnv))
            {
                config->redis.maxConn =
                    ParsePositiveSize(
                        *value,
                        kRedisMaxConnEnv);
            }

            if (const std::string *value =
                    FindEnvironment(
                        environment,
                        kRedisTimeoutEnv))
            {
                config->redis.timeoutMs =
                    ParsePositiveInt(
                        *value,
                        kRedisTimeoutEnv);
            }

            if (const std::string *value =
                    FindEnvironment(
                        environment,
                        kLogLevelEnv))
            {
                config->log.level = *value;
            }

            if (const std::string *value =
                    FindEnvironment(
                        environment,
                        kStorageRootEnv))
            {
                config->storage.root = *value;
            }

            if (const std::string *value =
                    FindEnvironment(
                        environment,
                        kStorageMaxFileBytesEnv))
            {
                config->storage.maxFileBytes =
                    ParsePositiveSize(
                        *value,
                        kStorageMaxFileBytesEnv);
            }

            if (const std::string *value =
                    FindEnvironment(
                        environment,
                        kInternalApiTokenEnv))
            {
                config->internalApi.token = *value;
            }

            if (const std::string *value =
                    FindEnvironment(
                        environment,
                        kAiEnhancementEnabledEnv))
            {
                config->ai.enhancementEnabled =
                    ParseBool(
                        *value,
                        kAiEnhancementEnabledEnv);
            }

            if (const std::string *value =
                    FindEnvironment(
                        environment,
                        kAiLeaseSecondsEnv))
            {
                config->ai.leaseSeconds =
                    static_cast<std::int32_t>(
                        ParsePositiveInt(
                            *value,
                            kAiLeaseSecondsEnv));
            }

            if (const std::string *value =
                    FindEnvironment(
                        environment,
                        kAiMaxAttemptsEnv))
            {
                config->ai.maxAttempts =
                    static_cast<std::int32_t>(
                        ParsePositiveInt(
                            *value,
                            kAiMaxAttemptsEnv));
            }
        }

        void Validate(const Config &config)
        {
            if (config.server.host.empty())
            {
                throw ConfigError(
                    "server.host must not be empty");
            }

            if (config.mysql.host.empty())
            {
                throw ConfigError(
                    "mysql.host must not be empty");
            }

            if (config.mysql.database.empty())
            {
                throw ConfigError(
                    "mysql.database must not be empty");
            }

            if (config.mysql.user.empty())
            {
                throw ConfigError(
                    "mysql.user must not be empty");
            }

            if (config.mysql.password.empty())
            {
                throw ConfigError(
                    "MYSQL_PASSWORD is required");
            }

            if (config.redis.host.empty())
            {
                throw ConfigError(
                    "redis.host must not be empty");
            }

            if (config.log.level.empty())
            {
                throw ConfigError(
                    "log.level must not be empty");
            }

            if (config.storage.root.empty())
            {
                throw ConfigError(
                    "storage.root must not be empty");
            }
        }
    } // namespace

    Config Config::LoadFromFile(
        const std::string &path,
        const Environment &environment)
    {
        std::ifstream input(path);

        if (!input)
        {
            throw ConfigError(
                "failed to open configuration file: " + path);
        }

        std::ostringstream buffer;
        buffer << input.rdbuf();

        return LoadFromString(
            buffer.str(),
            environment);
    }

    Config Config::LoadFromString(
        const std::string &yaml,
        const Environment &environment)
    {
        Config config;

        try
        {
            const YAML::Node root =
                YAML::Load(yaml);

            ApplyYaml(&config, root);
        }
        catch (const ConfigError &)
        {
            throw;
        }
        catch (const YAML::Exception &error)
        {
            throw ConfigError(
                std::string("failed to parse configuration: ") +
                error.what());
        }

        ApplyEnvironment(&config, environment);
        Validate(config);

        return config;
    }

    Environment Config::CurrentEnvironment()
    {
        constexpr const char *keys[] = {
            kServerHostEnv,
            kServerPortEnv,
            kMySqlHostEnv,
            kMySqlPortEnv,
            kMySqlDatabaseEnv,
            kMySqlUserEnv,
            kMySqlPasswordEnv,
            kMySqlMaxConnEnv,
            kRedisHostEnv,
            kRedisPortEnv,
            kRedisMaxConnEnv,
            kRedisTimeoutEnv,
            kLogLevelEnv,
            kStorageRootEnv,
            kStorageMaxFileBytesEnv,
            kInternalApiTokenEnv,
            kAiEnhancementEnabledEnv,
            kAiLeaseSecondsEnv,
            kAiMaxAttemptsEnv};

        Environment environment;

        for (const char *key : keys)
        {
            const char *value = std::getenv(key);

            if (value != nullptr)
            {
                environment.emplace(key, value);
            }
        }

        return environment;
    }

} // namespace jobpilot::core
