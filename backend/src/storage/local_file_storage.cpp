#include "jobpilot/storage/file_storage.h"

#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <fcntl.h>
#include <unistd.h>

namespace jobpilot::core
{
    namespace
    {
        namespace fs = std::filesystem;

        fs::path NormalizeRelativePath(
            std::string_view relativePath)
        {
            if (relativePath.empty())
            {
                throw std::invalid_argument(
                    "storage path must not be empty");
            }

            const fs::path path{
                std::string(relativePath)};

            if (path.is_absolute() ||
                path.has_root_name() ||
                path.has_root_directory())
            {
                throw std::invalid_argument(
                    "storage path must be relative");
            }

            const fs::path normalized =
                path.lexically_normal();

            if (normalized.empty())
            {
                throw std::invalid_argument(
                    "storage path must not be empty");
            }

            for (const fs::path &component :
                 normalized)
            {
                if (component == "..")
                {
                    throw std::invalid_argument(
                        "storage path must not escape the root");
                }
            }

            return normalized;
        }

        fs::path NormalizeTempId(
            std::string_view id)
        {
            if (id.empty() ||
                id == "." ||
                id == ".." ||
                id.find('/') != std::string_view::npos ||
                id.find('\\') != std::string_view::npos)
            {
                throw std::invalid_argument(
                    "invalid temporary file id");
            }

            return fs::path{std::string(id)};
        }

        [[noreturn]] void ThrowSystemError(
            const std::string &message)
        {
            throw std::system_error(
                errno,
                std::generic_category(),
                message);
        }
    } // namespace

    class LocalFileStorage final : public FileStorage
    {
    public:
        explicit LocalFileStorage(
            const std::string &root)
            : root_(),
              tempRoot_()
        {
            if (root.empty())
            {
                throw std::invalid_argument(
                    "storage root must not be empty");
            }

            std::error_code error;

            root_ = fs::absolute(root, error);

            if (error)
            {
                throw std::system_error(error);
            }

            root_ = root_.lexically_normal();
            tempRoot_ = root_ / ".tmp";

            fs::create_directories(tempRoot_, error);

            if (error)
            {
                throw std::system_error(error);
            }

            std::error_code canonicalError;
            const fs::path canonical =
                fs::weakly_canonical(root_, canonicalError);

            if (canonicalError)
            {
                throw std::system_error(canonicalError);
            }

            root_ = canonical;
            tempRoot_ = root_ / ".tmp";
        }

        TempFile WriteTemp(
            std::string_view bytes) override
        {
            const fs::path pattern =
                tempRoot_ / "upload-XXXXXX";

            std::string path =
                pattern.string();

            std::vector<char> buffer(
                path.begin(),
                path.end());
            buffer.push_back('\0');

            const int fd =
                ::mkstemp(buffer.data());

            if (fd < 0)
            {
                ThrowSystemError(
                    "failed to create temporary file");
            }

            const std::string createdPath =
                buffer.data();

            std::size_t written = 0;

            try
            {
                while (written < bytes.size())
                {
                    const ssize_t result =
                        ::write(
                            fd,
                            bytes.data() + written,
                            bytes.size() - written);

                    if (result < 0)
                    {
                        if (errno == EINTR)
                        {
                            continue;
                        }

                        ThrowSystemError(
                            "failed to write temporary file");
                    }

                    if (result == 0)
                    {
                        throw std::runtime_error(
                            "temporary file write returned zero");
                    }

                    written +=
                        static_cast<std::size_t>(
                            result);
                }

                if (::fsync(fd) != 0)
                {
                    ThrowSystemError(
                        "failed to flush temporary file");
                }

                if (::close(fd) != 0)
                {
                    ThrowSystemError(
                        "failed to close temporary file");
                }
            }
            catch (...)
            {
                ::close(fd);
                ::unlink(createdPath.c_str());
                throw;
            }

            return TempFile{
                fs::path(createdPath)
                    .filename()
                    .string()};
        }

        std::string Promote(
            const TempFile &temp,
            std::string_view relativePath) override
        {
            const fs::path normalized =
                NormalizeRelativePath(relativePath);

            const fs::path finalPath =
                root_ / normalized;

            std::error_code error;

            fs::create_directories(
                finalPath.parent_path(),
                error);

            if (error)
            {
                throw std::system_error(error);
            }

            const fs::path tempPath =
                tempRoot_ /
                NormalizeTempId(temp.id);

            if (::rename(
                    tempPath.c_str(),
                    finalPath.c_str()) != 0)
            {
                ThrowSystemError(
                    "failed to promote temporary file");
            }

            return normalized.generic_string();
        }

        bool Exists(
            std::string_view relativePath) const override
        {
            const fs::path path =
                root_ /
                NormalizeRelativePath(relativePath);

            std::error_code error;
            const fs::file_status status =
                fs::symlink_status(path, error);

            if (error ==
                std::errc::no_such_file_or_directory)
            {
                return false;
            }

            if (error)
            {
                throw std::system_error(error);
            }

            return fs::is_regular_file(status);
        }

        std::string Read(
            std::string_view relativePath) const override
        {
            const fs::path path =
                root_ /
                NormalizeRelativePath(relativePath);

            std::ifstream input(
                path,
                std::ios::binary);

            if (!input)
            {
                throw std::runtime_error(
                    "failed to open stored file");
            }

            return std::string(
                std::istreambuf_iterator<char>(input),
                std::istreambuf_iterator<char>());
        }

        void Remove(
            std::string_view relativePath) override
        {
            const fs::path path =
                root_ /
                NormalizeRelativePath(relativePath);

            if (::unlink(path.c_str()) != 0 &&
                errno != ENOENT)
            {
                ThrowSystemError(
                    "failed to remove stored file");
            }
        }

        void RemoveTemp(
            const TempFile &temp) noexcept override
        {
            try
            {
                const fs::path path =
                    tempRoot_ /
                    NormalizeTempId(temp.id);

                if (::unlink(path.c_str()) != 0 &&
                    errno != ENOENT)
                {
                    return;
                }
            }
            catch (...)
            {
            }
        }

    private:
        fs::path root_;
        fs::path tempRoot_;
    };

    std::unique_ptr<FileStorage> MakeLocalFileStorage(
        const std::string &root)
    {
        return std::make_unique<LocalFileStorage>(root);
    }

} // namespace jobpilot::core
