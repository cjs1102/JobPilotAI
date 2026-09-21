#include "jobpilot/network/event_loop.h"

#include <chrono>
#include <iostream>
#include <thread>

int main()
{
    using namespace jobpilot::core;

    EventLoop loop;

    int callbackCount = 0;

    std::thread worker([&loop, &callbackCount]()
                       {
        // 故意稍微等待一下。
        //
        // 这样主线程大概率已经进入：
        //
        //     epoll_wait()
        //
        // 我们就能够真正验证：
        //
        //     跨线程 RunInLoop
        //          ↓
        //     pendingFunctors_
        //          ↓
        //        Wakeup()
        //          ↓
        //       eventfd
        //          ↓
        //     epoll_wait 返回
        //
        std::this_thread::sleep_for(
            std::chrono::milliseconds(100));

        loop.RunInLoop([&loop, &callbackCount]() {
            ++callbackCount;

            std::cout
                << "EventLoop callback executed"
                << std::endl;

            loop.Quit();
        }); });

    // EventLoop 属于创建它的主线程，
    // 所以 Loop() 也在主线程运行。
    loop.Loop();

    worker.join();

    if (callbackCount != 1)
    {
        std::cerr
            << "test_event_loop failed: callbackCount = "
            << callbackCount
            << std::endl;

        return 1;
    }

    std::cout
        << "test_event_loop passed"
        << std::endl;

    return 0;
}