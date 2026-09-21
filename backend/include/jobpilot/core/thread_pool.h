#pragma once

#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace jobpilot::core
{

    class ThreadPool
    {
    public:
        explicit ThreadPool(
            std::size_t threadCount);

        ~ThreadPool();

        ThreadPool(const ThreadPool &) = delete;
        ThreadPool &operator=(const ThreadPool &) = delete;

        void Submit(
            std::function<void()> task);

    private:
        void WorkerLoop();

    private:
        std::mutex mutex_;
        std::condition_variable condition_;
        std::queue<std::function<void()>> tasks_;
        std::vector<std::thread> workers_;
        bool stopping_;
    };

} // namespace jobpilot::core