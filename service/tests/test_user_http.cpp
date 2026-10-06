#include <arpa/inet.h>

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <exception>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "jobpilot/cache/session_manager.h"
#include "jobpilot/http/http_server.h"
#include "jobpilot/network/event_loop.h"
#include "jobpilot/service/router.h"
#include "jobpilot/service/user/user_controller.h"
#include "jobpilot/service/user/user_dao.h"
#include "jobpilot/service/user/user_service.h"
#include "test_user_support.h"

namespace jobpilot::service
{
    namespace
    {
        constexpr std::uint16_t kTestPort = 19092;

        struct ServerState
        {
            std::mutex mutex;
            std::condition_variable condition;
            jobpilot::core::EventLoop *loop = nullptr;
            bool ready = false;
            std::exception_ptr exception;
        };

        struct HttpResponseData
        {
            int statusCode = 0;
            std::string body;
            nlohmann::json jsonBody;
            bool hasJson = false;
        };

        bool SendAll(
            int socketFd,
            const std::string &data)
        {
            std::size_t sent = 0;

            while (sent < data.size())
            {
                const ssize_t result =
                    ::send(
                        socketFd,
                        data.data() + sent,
                        data.size() - sent,
                        0);

                if (result <= 0)
                {
                    return false;
                }

                sent +=
                    static_cast<std::size_t>(result);
            }

            return true;
        }

        int CreateClientSocket()
        {
            const int socketFd =
                ::socket(
                    AF_INET,
                    SOCK_STREAM,
                    0);

            if (socketFd < 0)
            {
                return -1;
            }

            timeval timeout{};
            timeout.tv_sec = 2;

            if (::setsockopt(
                    socketFd,
                    SOL_SOCKET,
                    SO_RCVTIMEO,
                    &timeout,
                    sizeof(timeout)) != 0)
            {
                ::close(socketFd);
                return -1;
            }

            return socketFd;
        }

        bool ConnectToServer(int socketFd)
        {
            sockaddr_in serverAddress{};
            serverAddress.sin_family = AF_INET;
            serverAddress.sin_port = htons(kTestPort);

            return ::inet_pton(
                       AF_INET,
                       "127.0.0.1",
                       &serverAddress.sin_addr) == 1 &&
                   ::connect(
                       socketFd,
                       reinterpret_cast<sockaddr *>(
                           &serverAddress),
                       sizeof(serverAddress)) == 0;
        }

        std::string MakeRequest(
            const std::string &method,
            const std::string &path,
            const std::string &body = "",
            const std::string &authorization = "")
        {
            std::string request =
                method + " " + path +
                " HTTP/1.1\r\n"
                "Host: 127.0.0.1\r\n"
                "Connection: close\r\n";

            if (!authorization.empty())
            {
                request +=
                    "Authorization: " +
                    authorization +
                    "\r\n";
            }

            if (!body.empty())
            {
                request +=
                    "Content-Type: application/json\r\n"
                    "Content-Length: " +
                    std::to_string(body.size()) +
                    "\r\n";
            }

            request += "\r\n";
            request += body;

            return request;
        }

        std::optional<HttpResponseData> SendRequest(
            const std::string &method,
            const std::string &path,
            const std::string &body = "",
            const std::string &authorization = "")
        {
            const int socketFd =
                CreateClientSocket();

            if (socketFd < 0 ||
                !ConnectToServer(socketFd))
            {
                if (socketFd >= 0)
                {
                    ::close(socketFd);
                }

                return std::nullopt;
            }

            if (!SendAll(
                    socketFd,
                    MakeRequest(
                        method,
                        path,
                        body,
                        authorization)))
            {
                ::close(socketFd);
                return std::nullopt;
            }

            std::string rawResponse;
            char buffer[4096];

            while (true)
            {
                const ssize_t received =
                    ::recv(
                        socketFd,
                        buffer,
                        sizeof(buffer),
                        0);

                if (received <= 0)
                {
                    break;
                }

                rawResponse.append(
                    buffer,
                    static_cast<std::size_t>(
                        received));
            }

            ::close(socketFd);

            const std::size_t headerEnd =
                rawResponse.find("\r\n\r\n");

            if (headerEnd == std::string::npos)
            {
                return std::nullopt;
            }

            const std::size_t statusEnd =
                rawResponse.find("\r\n");

            if (statusEnd == std::string::npos ||
                statusEnd < 9)
            {
                return std::nullopt;
            }

            HttpResponseData response;

            response.statusCode =
                std::stoi(
                    rawResponse.substr(
                        9,
                        3));

            response.body =
                rawResponse.substr(
                    headerEnd + 4);

            try
            {
                response.jsonBody =
                    nlohmann::json::parse(
                        response.body);
                response.hasJson = true;
            }
            catch (const nlohmann::json::parse_error &)
            {
                response.hasJson = false;
            }

            return response;
        }
    } // namespace

    class UserHttpTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            pool = jobpilot::test::MakeConnectionPool();
            redis = jobpilot::test::MakeRedisClient();

            sessions =
                std::make_unique<
                    jobpilot::core::SessionManager>(
                    *redis);

            dao = std::make_unique<UserDao>(*pool);

            service =
                std::make_unique<UserService>(
                    *dao,
                    *sessions);

            controller =
                std::make_unique<UserController>(
                    *service);

            serverThread = std::thread(
                [this]()
                {
                    try
                    {
                        jobpilot::core::EventLoop loop;

                        jobpilot::core::HttpServer server(
                            &loop,
                            "127.0.0.1",
                            kTestPort);

                        RegisterRoutes(
                            &server,
                            controller.get());

                        server.Start();

                        {
                            std::lock_guard<std::mutex> lock(
                                state.mutex);

                            state.loop = &loop;
                            state.ready = true;
                        }

                        state.condition.notify_one();

                        loop.Loop();

                        server.Stop();
                    }
                    catch (...)
                    {
                        {
                            std::lock_guard<std::mutex> lock(
                                state.mutex);

                            state.exception =
                                std::current_exception();
                            state.ready = true;
                        }

                        state.condition.notify_one();
                    }
                });

            std::unique_lock<std::mutex> lock(
                state.mutex);

            state.condition.wait(
                lock,
                [this]()
                {
                    return state.ready;
                });

            if (state.exception)
            {
                std::rethrow_exception(
                    state.exception);
            }
        }

        void TearDown() override
        {
            jobpilot::core::EventLoop *loop = nullptr;

            {
                std::lock_guard<std::mutex> lock(
                    state.mutex);

                loop = state.loop;
            }

            if (loop != nullptr)
            {
                loop->QueueInLoop(
                    [loop]()
                    {
                        loop->Quit();
                    });
            }

            if (serverThread.joinable())
            {
                serverThread.join();
            }

            for (const std::string &token : tokens)
            {
                redis->Del("session:" + token);
            }

            for (const std::string &email : emails)
            {
                jobpilot::test::CleanupUser(
                    pool.get(),
                    email);
            }
        }

        std::string TrackEmail(
            const std::string &prefix)
        {
            const std::string email =
                jobpilot::test::UniqueEmail(prefix);

            emails.push_back(email);
            return email;
        }

        ServerState state;
        std::thread serverThread;

        std::unique_ptr<
            jobpilot::core::ConnectionPool>
            pool;

        std::unique_ptr<jobpilot::core::RedisClient> redis;

        std::unique_ptr<
            jobpilot::core::SessionManager>
            sessions;

        std::unique_ptr<UserDao> dao;
        std::unique_ptr<UserService> service;
        std::unique_ptr<UserController> controller;

        std::vector<std::string> emails;
        std::vector<std::string> tokens;
    };

    TEST_F(UserHttpTest, RegistersLogsInAndReturnsCurrentUser)
    {
        const std::string email =
            TrackEmail("http-user");

        const nlohmann::json registerBody{
            {"username", "tom"},
            {"email", email},
            {"password", "password"}};

        const std::optional<HttpResponseData> registration =
            SendRequest(
                "POST",
                "/api/v1/users/register",
                registerBody.dump());

        ASSERT_TRUE(registration.has_value());
        ASSERT_TRUE(registration->hasJson);
        EXPECT_EQ(registration->statusCode, 200);
        EXPECT_EQ(registration->jsonBody["code"], 0);

        const std::uint64_t userId =
            registration->jsonBody["data"]["user_id"]
                .get<std::uint64_t>();

        const nlohmann::json loginBody{
            {"email", email},
            {"password", "password"}};

        const std::optional<HttpResponseData> login =
            SendRequest(
                "POST",
                "/api/v1/users/login",
                loginBody.dump());

        ASSERT_TRUE(login.has_value());
        ASSERT_TRUE(login->hasJson);
        EXPECT_EQ(login->statusCode, 200);
        EXPECT_EQ(login->jsonBody["code"], 0);
        EXPECT_EQ(
            login->jsonBody["data"]["user_id"],
            userId);

        const std::string token =
            login->jsonBody["data"]["token"]
                .get<std::string>();

        tokens.push_back(token);

        const std::optional<HttpResponseData> me =
            SendRequest(
                "GET",
                "/api/v1/users/me",
                "",
                "Bearer " + token);

        ASSERT_TRUE(me.has_value());
        ASSERT_TRUE(me->hasJson);
        EXPECT_EQ(me->statusCode, 200);
        EXPECT_EQ(me->jsonBody["code"], 0);
        EXPECT_EQ(
            me->jsonBody["data"]["id"],
            userId);
        EXPECT_EQ(
            me->jsonBody["data"]["email"],
            email);
    }

    TEST_F(UserHttpTest, ReturnsLockedBusinessErrors)
    {
        const std::string email =
            TrackEmail("http-errors");

        const nlohmann::json registerBody{
            {"username", "tom"},
            {"email", email},
            {"password", "password"}};

        ASSERT_TRUE(
            SendRequest(
                "POST",
                "/api/v1/users/register",
                registerBody.dump())
                .has_value());

        const std::optional<HttpResponseData> duplicate =
            SendRequest(
                "POST",
                "/api/v1/users/register",
                registerBody.dump());

        ASSERT_TRUE(duplicate.has_value());
        ASSERT_TRUE(duplicate->hasJson);
        EXPECT_EQ(duplicate->statusCode, 409);
        EXPECT_EQ(duplicate->jsonBody["code"], 30001);

        const nlohmann::json wrongPassword{
            {"email", email},
            {"password", "wrong"}};

        const std::optional<HttpResponseData> login =
            SendRequest(
                "POST",
                "/api/v1/users/login",
                wrongPassword.dump());

        ASSERT_TRUE(login.has_value());
        ASSERT_TRUE(login->hasJson);
        EXPECT_EQ(login->statusCode, 401);
        EXPECT_EQ(login->jsonBody["code"], 30003);

        const std::optional<HttpResponseData> unauthorized =
            SendRequest(
                "GET",
                "/api/v1/users/me");

        ASSERT_TRUE(unauthorized.has_value());
        ASSERT_TRUE(unauthorized->hasJson);
        EXPECT_EQ(unauthorized->statusCode, 401);
        EXPECT_EQ(
            unauthorized->jsonBody["code"],
            20001);

        const std::optional<HttpResponseData> malformed =
            SendRequest(
                "POST",
                "/api/v1/users/register",
                "{");

        ASSERT_TRUE(malformed.has_value());
        ASSERT_TRUE(malformed->hasJson);
        EXPECT_EQ(malformed->statusCode, 400);
        EXPECT_EQ(
            malformed->jsonBody["code"],
            10002);

        const std::optional<HttpResponseData> notFound =
            SendRequest(
                "GET",
                "/api/v1/users/missing");

        ASSERT_TRUE(notFound.has_value());
        EXPECT_EQ(notFound->statusCode, 404);
    }
} // namespace jobpilot::service
