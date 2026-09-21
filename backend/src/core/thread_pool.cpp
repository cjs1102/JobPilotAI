#include "jobpilot/core/thread_pool.h"

#include <stdexcept>
#include <utility>

namespace jobpilot::core
{

ThreadPool::ThreadPool(
    std::size_t threadCount)
    : mutex_(),
      condition_(),
      tasks_(),
      workers_(),
      stopping_(false)
{
    if (threadCount == 0)
    {
        throw std::invalid_argument(
            "ThreadPool threadCount must be greater than zero");
    }

    workers_.reserve(threadCount);

    for (std::size_t i = 0; i < threadCount; ++i)
    {
        workers_.emplace_back(
            [this]()
            {
                WorkerLoop();
            });
    }
}

ThreadPool::~ThreadPool()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);

        stopping_ = true;
    }

    condition_.notify_all();

    for (std::thread &worker : workers_)
    {
        if (worker.joinable())
        {
            worker.join();
        }
    }
}

void ThreadPool::Submit(
    std::function<void()> task)
{
    if (!task)
    {
        throw std::invalid_argument(
            "ThreadPool task must not be empty");
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (stopping_)
        {
            throw std::runtime_error(
                "ThreadPool is stopping");
        }

        tasks_.push(std::move(task));
    }

    condition_.notify_one();
}

void ThreadPool::WorkerLoop()
{
    while (true)
    {
        std::function<void()> task;

        {
            std::unique_lock<std::mutex> lock(mutex_);

            condition_.wait(
                lock,
                [this]()
                {
                    return stopping_ ||
                           !tasks_.empty();
                });

            if (stopping_ &&
                tasks_.empty())
            {
                return;
            }

            task = std::move(tasks_.front());
            tasks_.pop();
        }

        try
        {
            task();
        }
        catch (...)
        {
            // 当前阶段线程池不负责异常上报。
            // 单个任务的异常不能让 Worker 线程直接退出。
        }
    }
}

}  // namespace jobpilot::core
