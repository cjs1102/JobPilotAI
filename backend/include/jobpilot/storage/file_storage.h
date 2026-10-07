#pragma once

#include <memory>
#include <string>
#include <string_view>

namespace jobpilot::core
{
    struct TempFile
    {
        std::string id;
    };

    class FileStorage
    {
    public:
        virtual ~FileStorage() = default;

        virtual TempFile WriteTemp(
            std::string_view bytes) = 0;

        virtual std::string Promote(
            const TempFile &temp,
            std::string_view relativePath) = 0;

        virtual bool Exists(
            std::string_view relativePath) const = 0;

        virtual std::string Read(
            std::string_view relativePath) const = 0;

        virtual void Remove(
            std::string_view relativePath) = 0;

        virtual void RemoveTemp(
            const TempFile &temp) noexcept = 0;
    };

    std::unique_ptr<FileStorage> MakeLocalFileStorage(
        const std::string &root);

} // namespace jobpilot::core
