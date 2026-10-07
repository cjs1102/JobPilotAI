#include <cassert>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

#include <unistd.h>

#include "jobpilot/storage/file_storage.h"

using namespace jobpilot::core;

namespace
{
    namespace fs = std::filesystem;

    std::string MakeRoot()
    {
        static int counter = 0;

        return "/tmp/jobpilot-file-storage-" +
               std::to_string(getpid()) +
               "-" +
               std::to_string(counter++);
    }

    void TestWritePromoteReadRemove()
    {
        const std::string root =
            MakeRoot();

        std::unique_ptr<FileStorage> storage =
            MakeLocalFileStorage(root);

        std::string bytes = "PDF";
        bytes.push_back('\0');
        bytes.append("payload");

        const TempFile temp =
            storage->WriteTemp(bytes);

        const std::string relative =
            storage->Promote(
                temp,
                "1/resumes/2/v1.pdf");

        assert(relative == "1/resumes/2/v1.pdf");
        assert(storage->Exists(relative));
        assert(storage->Read(relative) == bytes);

        storage->Remove(relative);
        assert(!storage->Exists(relative));

        fs::remove_all(root);
    }

    void TestTraversalIsRejected()
    {
        const std::string root =
            MakeRoot();

        std::unique_ptr<FileStorage> storage =
            MakeLocalFileStorage(root);

        const TempFile temp =
            storage->WriteTemp("x");

        bool rejected = false;

        try
        {
            storage->Promote(
                temp,
                "../escape.txt");
        }
        catch (const std::invalid_argument &)
        {
            rejected = true;
        }

        assert(rejected);
        storage->RemoveTemp(temp);

        fs::remove_all(root);
    }

    void TestRemoveIsIdempotent()
    {
        const std::string root =
            MakeRoot();

        std::unique_ptr<FileStorage> storage =
            MakeLocalFileStorage(root);

        storage->Remove("missing/file.txt");

        fs::remove_all(root);
    }
} // namespace

int main()
{
    TestWritePromoteReadRemove();
    TestTraversalIsRejected();
    TestRemoveIsIdempotent();

    std::cout << "file storage tests passed\n";
    return 0;
}
