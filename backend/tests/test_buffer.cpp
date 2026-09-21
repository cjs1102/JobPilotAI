#include "jobpilot/network/buffer.h"

#include <cassert>
#include <cstring>
#include <iostream>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

namespace
{
    void TestBasicAppendAndRetrieve()
    {
        jobpilot::core::Buffer buffer(8);

        buffer.Append("hello", 5);

        assert(buffer.ReadableBytes() == 5);
        assert(buffer.WritableBytes() == 3);
        assert(
            std::string(
                buffer.Peek(),
                buffer.ReadableBytes()) == "hello");

        buffer.Retrieve(2);

        assert(buffer.ReadableBytes() == 3);
        assert(
            std::string(
                buffer.Peek(),
                buffer.ReadableBytes()) == "llo");

        buffer.RetrieveAll();

        assert(buffer.ReadableBytes() == 0);

        std::cout << "TestBasicAppendAndRetrieve passed\n";
    }

    void TestExpansion()
    {
        jobpilot::core::Buffer buffer(4);

        buffer.Append("hello world", 11);

        assert(buffer.ReadableBytes() == 11);
        assert(
            std::string(
                buffer.Peek(),
                buffer.ReadableBytes()) == "hello world");

        assert(buffer.WritableBytes() >= 0);

        std::cout << "TestExpansion passed\n";
    }

    void TestSpaceReuse()
    {
        jobpilot::core::Buffer buffer(8);

        buffer.Append("123456", 6);
        buffer.Retrieve(4);

        assert(
            std::string(
                buffer.Peek(),
                buffer.ReadableBytes()) == "56");

        buffer.Append("ABCDEFG", 7);

        assert(buffer.ReadableBytes() == 9);
        assert(
            std::string(
                buffer.Peek(),
                buffer.ReadableBytes()) == "56ABCDEFG");

        std::cout << "TestSpaceReuse passed\n";
    }

    void TestReadFdWithSocketPair()
    {
        int sockets[2];

        const int result =
            ::socketpair(
                AF_UNIX,
                SOCK_STREAM,
                0,
                sockets);

        assert(result == 0);

        jobpilot::core::Buffer buffer(4);

        const std::string firstMessage = "hello ";
        const std::string secondMessage = "world";

        const ssize_t firstWrite =
            ::write(
                sockets[0],
                firstMessage.data(),
                firstMessage.size());

        assert(
            firstWrite == static_cast<ssize_t>(firstMessage.size()));

        int savedErrno = 0;

        const ssize_t firstRead =
            buffer.ReadFd(
                sockets[1],
                &savedErrno);

        assert(
            firstRead == static_cast<ssize_t>(firstMessage.size()));

        assert(
            std::string(
                buffer.Peek(),
                buffer.ReadableBytes()) == "hello ");

        const ssize_t secondWrite =
            ::write(
                sockets[0],
                secondMessage.data(),
                secondMessage.size());

        assert(
            secondWrite == static_cast<ssize_t>(secondMessage.size()));

        const ssize_t secondRead =
            buffer.ReadFd(
                sockets[1],
                &savedErrno);

        assert(
            secondRead == static_cast<ssize_t>(secondMessage.size()));

        assert(
            std::string(
                buffer.Peek(),
                buffer.ReadableBytes()) == "hello world");

        std::cout << "TestReadFdWithSocketPair passed\n";

        ::close(sockets[0]);
        ::close(sockets[1]);
    }

    void TestWriteFd()
    {
        int sockets[2];

        const int result =
            ::socketpair(
                AF_UNIX,
                SOCK_STREAM,
                0,
                sockets);

        assert(result == 0);

        jobpilot::core::Buffer buffer;

        buffer.Append("network data", 12);

        int savedErrno = 0;

        const ssize_t written =
            buffer.WriteFd(
                sockets[0],
                &savedErrno);

        assert(written == 12);
        assert(buffer.ReadableBytes() == 0);

        char received[32] = {};

        const ssize_t readBytes =
            ::read(
                sockets[1],
                received,
                sizeof(received));

        assert(readBytes == 12);
        assert(
            std::string(
                received,
                static_cast<std::size_t>(readBytes)) == "network data");

        std::cout << "TestWriteFd passed\n";

        ::close(sockets[0]);
        ::close(sockets[1]);
    }
}

int main()
{
    TestBasicAppendAndRetrieve();
    TestExpansion();
    TestSpaceReuse();
    TestReadFdWithSocketPair();
    TestWriteFd();

    std::cout << "test_buffer passed\n";

    return 0;
}