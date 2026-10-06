#include "jobpilot/http/http_request_parser.h"
#include "jobpilot/http/http_response.h"
#include "jobpilot/http/http_router.h"
#include "jobpilot/network/buffer.h"
#include "jobpilot/network/event_loop.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace
{
    using BenchmarkFunction =
        std::function<std::uint64_t(std::size_t)>;

    struct BenchmarkOptions
    {
        std::size_t iterations = 0;
        std::size_t repeats = 5;
        bool smoke = false;
    };

    struct Measurement
    {
        std::string name;
        std::size_t iterations;
        std::size_t repeats;
        double nanosecondsPerOperation;
        double operationsPerSecond;
    };

    volatile std::uint64_t benchmarkSink = 0;

    std::uint64_t BenchmarkBuffer(
        std::size_t iterations)
    {
        jobpilot::core::Buffer buffer(256);
        const std::string payload(128, 'x');
        std::uint64_t checksum = 0;

        for (std::size_t i = 0; i < iterations; ++i)
        {
            buffer.Append(payload);
            checksum += buffer.ReadableBytes();
            buffer.RetrieveAll();
        }

        return checksum;
    }

    std::uint64_t BenchmarkHttpParser(
        std::size_t iterations)
    {
        const std::string request =
            "POST /api/v1/jobs/123 HTTP/1.1\r\n"
            "Host: localhost\r\n"
            "Content-Type: application/json\r\n"
            "Content-Length: 17\r\n"
            "\r\n"
            "{\"job\":\"backend\"}";

        std::uint64_t checksum = 0;

        for (std::size_t i = 0; i < iterations; ++i)
        {
            jobpilot::core::Buffer buffer;
            buffer.Append(request);

            jobpilot::core::HttpRequestParser parser;

            const jobpilot::core::ParseResult result =
                parser.Parse(&buffer);

            if (result !=
                jobpilot::core::ParseResult::kComplete)
            {
                throw std::runtime_error(
                    "HTTP parser benchmark input did not complete");
            }

            checksum += parser.Request().Body().size();
        }

        return checksum;
    }

    std::uint64_t BenchmarkRouter(
        std::size_t iterations)
    {
        using namespace jobpilot::core;

        HttpRouter router;

        router.RegisterRoute(
            HttpMethod::kGet,
            "/health",
            [](const HttpRequest &, HttpResponse &response)
            {
                response.SetStatusCode(200);
                response.SetStatusMessage("OK");
                response.SetBody("{\"status\":\"ok\"}");
            });

        router.RegisterRoute(
            HttpMethod::kGet,
            "/api/v1/jobs/{id}",
            [](const HttpRequest &, HttpResponse &response)
            {
                response.SetStatusCode(200);
                response.SetStatusMessage("OK");
                response.SetBody("{\"job\":\"ok\"}");
            });

        HttpRequest request;
        request.SetMethod(HttpMethod::kGet);
        request.SetPath("/api/v1/jobs/123");
        request.SetVersion(HttpVersion::kHttp11);

        std::uint64_t checksum = 0;

        for (std::size_t i = 0; i < iterations; ++i)
        {
            HttpResponse response;

            if (!router.Route(request, &response))
            {
                throw std::runtime_error(
                    "router benchmark request did not match");
            }

            checksum +=
                static_cast<std::uint64_t>(
                    response.Body().size());
        }

        return checksum;
    }

    std::uint64_t BenchmarkEventLoopDispatch(
        std::size_t iterations)
    {
        jobpilot::core::EventLoop loop;
        std::atomic<std::size_t> executed{0};

        std::thread loopThread(
            [&loop]()
            {
                loop.Loop();
            });

        for (std::size_t i = 0; i < iterations; ++i)
        {
            loop.QueueInLoop(
                [&executed]()
                {
                    executed.fetch_add(
                        1,
                        std::memory_order_relaxed);
                });
        }

        loop.QueueInLoop(
            [&loop]()
            {
                loop.Quit();
            });

        loopThread.join();

        const std::size_t completed =
            executed.load(
                std::memory_order_relaxed);

        if (completed != iterations)
        {
            throw std::runtime_error(
                "event-loop benchmark lost callbacks");
        }

        return static_cast<std::uint64_t>(
            completed);
    }

    double Median(
        std::vector<double> values)
    {
        std::sort(
            values.begin(),
            values.end());

        const std::size_t middle =
            values.size() / 2;

        if (values.size() % 2 == 0)
        {
            return (
                values[middle - 1] +
                values[middle]) /
                2.0;
        }

        return values[middle];
    }

    Measurement Measure(
        const std::string &name,
        std::size_t iterations,
        std::size_t repeats,
        const BenchmarkFunction &benchmark)
    {
        const std::size_t warmupIterations =
            std::max<std::size_t>(
                1,
                iterations / 10);

        for (std::size_t i = 0; i < 3; ++i)
        {
            benchmarkSink ^=
                benchmark(warmupIterations);
        }

        std::vector<double> samples;
        samples.reserve(repeats);

        for (std::size_t repeat = 0;
             repeat < repeats;
             ++repeat)
        {
            const auto start =
                std::chrono::steady_clock::now();

            const std::uint64_t checksum =
                benchmark(iterations);

            const auto end =
                std::chrono::steady_clock::now();

            benchmarkSink ^= checksum;

            const auto elapsed =
                std::chrono::duration_cast<
                    std::chrono::nanoseconds>(
                    end - start);

            samples.push_back(
                static_cast<double>(
                    elapsed.count()) /
                static_cast<double>(iterations));
        }

        const double nanosecondsPerOperation =
            Median(std::move(samples));

        return Measurement{
            name,
            iterations,
            repeats,
            nanosecondsPerOperation,
            1'000'000'000.0 /
                nanosecondsPerOperation};
    }

    std::size_t ParsePositiveSize(
        const std::string &value,
        const std::string &option)
    {
        std::size_t parsed = 0;

        const unsigned long long result =
            std::stoull(value, &parsed);

        if (parsed != value.size() ||
            result == 0)
        {
            throw std::invalid_argument(
                option + " requires a positive integer");
        }

        return static_cast<std::size_t>(result);
    }

    BenchmarkOptions ParseOptions(
        int argc,
        char *argv[])
    {
        BenchmarkOptions options;

        for (int i = 1; i < argc; ++i)
        {
            const std::string argument = argv[i];

            if (argument == "--smoke")
            {
                options.smoke = true;
                continue;
            }

            constexpr const char *kIterationsPrefix =
                "--iterations=";

            if (argument.rfind(
                    kIterationsPrefix,
                    0) == 0)
            {
                options.iterations =
                    ParsePositiveSize(
                        argument.substr(
                            std::string(
                                kIterationsPrefix).size()),
                        "--iterations");

                continue;
            }

            constexpr const char *kRepeatsPrefix =
                "--repeats=";

            if (argument.rfind(
                    kRepeatsPrefix,
                    0) == 0)
            {
                options.repeats =
                    ParsePositiveSize(
                        argument.substr(
                            std::string(
                                kRepeatsPrefix).size()),
                        "--repeats");

                continue;
            }

            throw std::invalid_argument(
                "unknown argument: " + argument);
        }

        if (options.smoke)
        {
            options.iterations =
                options.iterations == 0
                    ? 2'000
                    : std::min<std::size_t>(
                          options.iterations,
                          2'000);

            options.repeats = 1;
        }

        return options;
    }

    void PrintResult(
        const Measurement &measurement)
    {
        std::cout
            << measurement.name << ','
            << measurement.iterations << ','
            << measurement.repeats << ','
            << std::fixed
            << std::setprecision(3)
            << measurement.nanosecondsPerOperation << ','
            << std::setprecision(0)
            << measurement.operationsPerSecond
            << '\n';
    }
} // namespace

int main(int argc, char *argv[])
{
    try
    {
        const BenchmarkOptions options =
            ParseOptions(argc, argv);

        const std::size_t bufferIterations =
            options.iterations == 0
                ? 1'000'000
                : options.iterations;

        const std::size_t parserIterations =
            options.iterations == 0
                ? 200'000
                : options.iterations;

        const std::size_t routerIterations =
            options.iterations == 0
                ? 500'000
                : options.iterations;

        const std::size_t eventLoopIterations =
            options.iterations == 0
                ? 10'000
                : options.iterations;

        std::cout
            << "name,iterations,repeats,"
               "median_ns_per_op,operations_per_second\n";

        PrintResult(
            Measure(
                "buffer_append_retrieve",
                bufferIterations,
                options.repeats,
                BenchmarkBuffer));

        PrintResult(
            Measure(
                "http_parser_post",
                parserIterations,
                options.repeats,
                BenchmarkHttpParser));

        PrintResult(
            Measure(
                "router_parameter_route",
                routerIterations,
                options.repeats,
                BenchmarkRouter));

        PrintResult(
            Measure(
                "event_loop_dispatch",
                eventLoopIterations,
                options.repeats,
                BenchmarkEventLoopDispatch));

        return benchmarkSink == 0 ? 1 : 0;
    }
    catch (const std::exception &error)
    {
        std::cerr
            << "microbenchmark failed: "
            << error.what()
            << '\n';

        return 1;
    }
}
