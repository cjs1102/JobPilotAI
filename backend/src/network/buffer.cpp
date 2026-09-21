#include "jobpilot/network/buffer.h"

#include <cerrno>
#include <cstring>
#include <sys/uio.h>
#include <unistd.h>

namespace jobpilot::core
{
    Buffer::Buffer(std::size_t initialSize)
        : buffer_(initialSize),
          readIndex_(0),
          writeIndex_(0)
    {
    }

    std::size_t Buffer::ReadableBytes() const
    {
        return writeIndex_ - readIndex_;
    }

    std::size_t Buffer::WritableBytes() const
    {
        return buffer_.size() - writeIndex_;
    }

    std::size_t Buffer::PrependableBytes() const
    {
        return readIndex_;
    }

    const char *Buffer::Peek() const
    {
        return Begin() + readIndex_;
    }

    void Buffer::Retrieve(std::size_t len)
    {
        if (len < ReadableBytes())
        {
            readIndex_ += len;
        }
        else
        {
            RetrieveAll();
        }
    }

    void Buffer::RetrieveAll()
    {
        readIndex_ = 0;
        writeIndex_ = 0;
    }

    std::string Buffer::RetrieveAsString()
    {
        std::string result(
            Peek(), ReadableBytes());

        RetrieveAll();

        return result;
    }

    void Buffer::Append(
        const char *data,
        std::size_t len)
    {
        EnsureWritableBytes(len);

        std::memcpy(BeginWrite(), data, len);

        HasWritten(len);
    }

    void Buffer::Append(
        const std::string &data)
    {
        Append(
            data.data(),
            data.size());
    }

    void Buffer::EnsureWritableBytes(
        std::size_t len)
    {
        if (WritableBytes() < len)
        {
            MakeSpace(len);
        }
    }

    char *Buffer::BeginWrite()
    {
        return Begin() + writeIndex_;
    }

    void Buffer::HasWritten(
        std::size_t len)
    {
        writeIndex_ += len;
    }

    ssize_t Buffer::ReadFd(
        int fd,
        int *savedErrno)
    {
        char extraBuf[65536];

        struct iovec vec[2];

        const std::size_t writableBytes =
            WritableBytes();

        vec[0].iov_base = BeginWrite();
        vec[0].iov_len = writableBytes;

        vec[1].iov_base = extraBuf;
        vec[1].iov_len = sizeof(extraBuf);

        const int iovCount =
            writableBytes < sizeof(extraBuf)
                ? 2
                : 1;

        const ssize_t n =
            ::readv(
                fd,
                vec,
                iovCount);

        if (n < 0)
        {
            if (savedErrno != nullptr)
            {
                *savedErrno = errno;
            }
        }
        else if (
            static_cast<std::size_t>(n) <= writableBytes)
        {
            writeIndex_ +=
                static_cast<std::size_t>(n);
        }
        else
        {
            writeIndex_ =
                buffer_.size();

            Append(
                extraBuf,
                static_cast<std::size_t>(n) - writableBytes);
        }

        return n;
    }

    ssize_t Buffer::WriteFd(
        int fd,
        int *savedErrno)
    {
        const ssize_t n =
            ::write(
                fd,
                Peek(),
                ReadableBytes());

        if (n < 0)
        {
            if (savedErrno != nullptr)
            {
                *savedErrno = errno;
            }

            return n;
        }

        Retrieve(
            static_cast<std::size_t>(n));

        return n;
    }

    char *Buffer::Begin()
    {
        return buffer_.data();
    }

    const char *Buffer::Begin() const
    {
        return buffer_.data();
    }

    void Buffer::MakeSpace(
        std::size_t len)
    {
        if (
            WritableBytes() + PrependableBytes() < len)
        {
            buffer_.resize(
                writeIndex_ + len);
        }
        else
        {
            const std::size_t readableBytes =
                ReadableBytes();

            std::memmove(
                Begin(),
                Peek(),
                readableBytes);

            readIndex_ = 0;
            writeIndex_ =
                readableBytes;
        }
    }

} // namespace jobpilot::core
