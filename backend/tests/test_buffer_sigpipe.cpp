#include "jobpilot/network/buffer.h"

#include <cerrno>
#include <cstring>
#include <iostream>

#include <sys/socket.h>
#include <unistd.h>

int main()
{
    int sockets[2] = {-1, -1};

    if (::socketpair(
            AF_UNIX,
            SOCK_STREAM,
            0,
            sockets) != 0)
    {
        std::cerr
            << "socketpair failed: "
            << std::strerror(errno)
            << '\n';

        return 1;
    }

    ::close(sockets[1]);
    sockets[1] = -1;

    jobpilot::core::Buffer buffer;
    buffer.Append("write to closed peer");

    int savedErrno = 0;

    const ssize_t written =
        buffer.WriteFd(
            sockets[0],
            &savedErrno);

    ::close(sockets[0]);

    if (written != -1 || savedErrno != EPIPE)
    {
        std::cerr
            << "expected EPIPE without process termination, got "
            << written
            << " and errno "
            << savedErrno
            << '\n';

        return 1;
    }

    std::cout
        << "buffer SIGPIPE regression test passed\n";

    return 0;
}
