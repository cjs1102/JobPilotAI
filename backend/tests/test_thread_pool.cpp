#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>

#include "jobpilot/core/thread_pool.h"

namespace jobpilot::core
{

namespace
{

void TestZeroThreadCount()
{
    bool thrown = false;

    try
    {
        ThreadPool pool(0);
    }
    catch (const std::invalid_argument &)
    {
        thrown = true;
    }

    assert(thrown);
}

void TestEmptyTask()
{
    ThreadPool pool(2);

    bool thrown = false;

    try
    {
        pool.Submit(std::function<void()>{});
    }
    catch (const std::invalid_argument &)
    {
        thrown = true;
    }

    assert(thrown);
}

void TestSubmitTasks()
{
    constexpr int kTaskCount = 100;

    ThreadPool pool(4);

    std::atomic<int> completedTasks{0};

    std::mutex mutex;
    std::condition_variable condition;

    for (int i = 0; i < kTaskCount; ++i)
    {
        pool.Submit(
            [&completedTasks, &mutex, &condition]()
            {
                const int completed =
                    completedTasks.fetch_add(
                        1,
                        std::memory_order_relaxed) +
                    1;

                if (completed == 100)
                {
                    condition.notify_one();
                }
            });
    }

    std::unique_lock<std::mutex> lock(mutex);

    const bool completed =
        condition.wait_for(
            lock,
            std::chrono::seconds(2),
            [&completedTasks]()
            {
                return completedTasks.load(
                           std::memory_order_relaxed) ==
                       kTaskCount;
            });

    assert(completed);
    assert(
        completedTasks.load(
            std::memory_order_relaxed) ==
        kTaskCount);
}

void TestWorkerExecutesTasks()
{
    ThreadPool pool(4);

    const std::thread::id mainThreadId =
        std::this_thread::get_id();

    std::atomic<int> workerTasks{0};

    std::mutex mutex;
    std::condition_variable condition;

    constexpr int kTaskCount = 20;
    std::atomic<int> completedTasks{0};

    for (int i = 0; i < kTaskCount; ++i)
    {
        pool.Submit(
            [&workerTasks,
             &completedTasks,
             &condition,
             mainThreadId]()
            {
                if (std::this_thread::get_id() !=
                    mainThreadId)
                {
                    workerTasks.fetch_add(
                        1,
                        std::memory_order_relaxed);
                }

                const int completed =
                    completedTasks.fetch_add(
                        1,
                        std::memory_order_relaxed) +
                    1;

                if (completed == kTaskCount)
                {
                    condition.notify_one();
                }
            });
    }

    std::unique_lock<std::mutex> lock(mutex);

    const bool completed =
        condition.wait_for(
            lock,
            std::chrono::seconds(2),
            [&completedTasks]()
            {
                return completedTasks.load(
                           std::memory_order_relaxed) ==
                       kTaskCount;
            });

    assert(completed);

    assert(
        workerTasks.load(
            std::memory_order_relaxed) ==
        kTaskCount);
}

void TestWorkerSurvivesTaskException()
{
    ThreadPool pool(1);

    std::atomic<bool> secondTaskCompleted{false};

    std::mutex mutex;
    std::condition_variable condition;

    pool.Submit(
        []()
        {
            throw std::runtime_error(
                "test exception");
        });

    pool.Submit(
        [&secondTaskCompleted, &condition]()
        {
            secondTaskCompleted.store(
                true,
                std::memory_order_relaxed);

            condition.notify_one();
        });

    std::unique_lock<std::mutex> lock(mutex);

    const bool completed =
        condition.wait_for(
            lock,
            std::chrono::seconds(2),
            [&secondTaskCompleted]()
            {
                return secondTaskCompleted.load(
                    std::memory_order_relaxed);
            });

    assert(completed);
}

void TestDestructorWaitsForTasks()
{
    std::atomic<int> completedTasks{0};

    {
        ThreadPool pool(2);

        constexpr int kTaskCount = 10;

        for (int i = 0; i < kTaskCount; ++i)
        {
            pool.Submit(
                [&completedTasks]()
                {
                    std::this_thread::sleep_for(
                        std::chrono::milliseconds(10));

                    completedTasks.fetch_add(
                        1,
                        std::memory_order_relaxed);
                });
        }
    }

    assert(
        completedTasks.load(
            std::memory_order_relaxed) ==
        10);
}

}  // namespace

}  // namespace jobpilot::core

int main()
{
    using namespace jobpilot::core;

    TestZeroThreadCount();
    TestEmptyTask();
    TestSubmitTasks();
    TestWorkerExecutesTasks();
    TestWorkerSurvivesTaskException();
    TestDestructorWaitsForTasks();

    std::cout << "thread pool tests passed\n";

    return 0;
}
