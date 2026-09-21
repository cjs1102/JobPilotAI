#pragma once
#include <cstddef>
#include <string>
#include <vector>
#include <sys/types.h>

namespace jobpilot::core
{
    class Buffer
    {
    public:
        static constexpr std::size_t kInitialSize = 1024;

        explicit Buffer(std::size_t initialSize = kInitialSize);

        // 可读字节数
        std::size_t ReadableBytes() const;

        // 可写的字节数
        std::size_t WritableBytes() const;

        // readIndex_前面已使用的空间
        std::size_t PrependableBytes() const;

        // 返回可读数据的起始位置
        const char *Peek() const;

        // 取出len字节
        void Retrieve(std::size_t len);

        // 取出全部可读数据
        void RetrieveAll();

        // 取出全部可读数据并转化成string
        std::string RetrieveAsString();

        // 向Buffer尾部追加数据
        void Append(const char *data, std::size_t len);

        // 追加string
        void Append(const std::string &data);

        // 确认Buffer至少拥有len字节的可写空间
        void EnsureWritableBytes(std::size_t len);

        // 返回可写区域起始位置
        char *BeginWrite();

        // 将writeIndex_向后移动len字节
        void HasWritten(std::size_t len);

        // 从fd读取数据
        ssize_t ReadFd(int fd, int *saveErrno);

        // 向fd写数据
        ssize_t WriteFd(int fd, int *savedErrno);

    private:
        char *Begin();

        const char *Begin() const;

        void MakeSpace(std::size_t len);

    private:
        std::vector<char> buffer_;

        std::size_t readIndex_;

        std::size_t writeIndex_;
    };
}