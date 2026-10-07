#include "jobpilot/service/resume/resume_service.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>
#include <unistd.h>

#include "jobpilot/cache/session_manager.h"
#include "jobpilot/database/mysql_pool.h"
#include "jobpilot/service/auth/session_authenticator.h"
#include "jobpilot/service/resume/resume_dao.h"
#include "jobpilot/service/user/user_dao.h"
#include "jobpilot/storage/file_storage.h"
#include "test_user_support.h"

namespace jobpilot::service
{
    namespace
    {
        class FailingStorage final
            : public jobpilot::core::FileStorage
        {
        public:
            jobpilot::core::TempFile WriteTemp(
                std::string_view) override
            {
                if (failWrite)
                {
                    throw std::runtime_error(
                        "forced write failure");
                }

                return jobpilot::core::TempFile{"temp"};
            }

            std::string Promote(
                const jobpilot::core::TempFile &,
                std::string_view) override
            {
                if (failPromote)
                {
                    throw std::runtime_error(
                        "forced promote failure");
                }

                return "promoted";
            }

            bool Exists(
                std::string_view) const override
            {
                return false;
            }

            std::string Read(
                std::string_view) const override
            {
                return {};
            }

            void Remove(
                std::string_view) override
            {
                ++removed;
            }

            void RemoveTemp(
                const jobpilot::core::TempFile &) noexcept override
            {
                ++removedTemp;
            }

            bool failWrite = false;
            bool failPromote = false;
            int removed = 0;
            int removedTemp = 0;
        };

        std::string MakeStorageRoot()
        {
            static int counter = 0;

            return "/tmp/jobpilot-resume-service-" +
                   std::to_string(getpid()) +
                   "-" +
                   std::to_string(counter++);
        }
    } // namespace

    class ResumeServiceTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            pool = jobpilot::test::MakeConnectionPool();
            redis = jobpilot::test::MakeRedisClient();
            sessions =
                std::make_unique<
                    jobpilot::core::SessionManager>(
                    *redis);
            authenticator =
                std::make_unique<SessionAuthenticator>(
                    *sessions);
            userDao =
                std::make_unique<UserDao>(*pool);
            resumeDao =
                std::make_unique<ResumeDao>(*pool);

            storageRoot = MakeStorageRoot();
            storage =
                jobpilot::core::MakeLocalFileStorage(
                    storageRoot);

            service =
                std::make_unique<ResumeService>(
                    *resumeDao,
                    *pool,
                    *storage,
                    *authenticator,
                    10U * 1024U * 1024U);

            email =
                jobpilot::test::UniqueEmail(
                    "resume-service");

            const std::optional<std::uint64_t> created =
                userDao->CreateUser(
                    "resume-service-user",
                    email,
                    "hash");

            ASSERT_TRUE(created.has_value());
            userId = created.value();
        }

        void TearDown() override
        {
            for (const std::string &token : tokens)
            {
                redis->Del("session:" + token);
            }

            if (pool && !email.empty())
            {
                jobpilot::test::CleanupUser(
                    pool.get(),
                    email);
            }

            if (!storageRoot.empty())
            {
                std::filesystem::remove_all(
                    storageRoot);
            }
        }

        std::unique_ptr<
            jobpilot::core::ConnectionPool>
            pool;
        std::unique_ptr<jobpilot::core::RedisClient> redis;
        std::unique_ptr<jobpilot::core::SessionManager>
            sessions;
        std::unique_ptr<SessionAuthenticator>
            authenticator;
        std::unique_ptr<UserDao> userDao;
        std::unique_ptr<ResumeDao> resumeDao;
        std::unique_ptr<jobpilot::core::FileStorage>
            storage;
        std::unique_ptr<ResumeService> service;
        std::string storageRoot;
        std::string email;
        std::uint64_t userId = 0;
        std::vector<std::string> tokens;
    };

    TEST_F(ResumeServiceTest, UploadsListsAndVersionsTextAndPdf)
    {
        ResumeUpload textUpload;
        textUpload.title = "C++ backend";
        textUpload.filename = "resume.txt";
        textUpload.bytes = "hello resume";

        const ServiceResult<ResumeUploadResult> text =
            service->Upload(
                userId,
                textUpload);

        ASSERT_EQ(text.code, ErrorCode::kSuccess);
        EXPECT_EQ(text.value.version, 1);

        const std::string textPath =
            std::to_string(userId) +
            "/resumes/" +
            std::to_string(text.value.resumeId) +
            "/v1.txt";

        EXPECT_TRUE(storage->Exists(textPath));

        const ServiceResult<ResumePage> list =
            service->ListResumes(
                userId,
                1,
                20);

        ASSERT_EQ(list.code, ErrorCode::kSuccess);
        ASSERT_EQ(list.value.list.size(), 1U);
        EXPECT_EQ(list.value.total, 1U);
        EXPECT_EQ(
            list.value.list.front().latestVersion,
            1);

        const ServiceResult<ResumeVersionResult> version =
            service->CreateVersion(
                userId,
                text.value.resumeId,
                "second version");

        ASSERT_EQ(version.code, ErrorCode::kSuccess);
        EXPECT_EQ(version.value.version, 2);

        const ServiceResult<
            std::vector<ResumeVersionRecord>> versions =
                service->ListVersions(
                    userId,
                    text.value.resumeId);

        ASSERT_EQ(versions.code, ErrorCode::kSuccess);
        ASSERT_EQ(versions.value.size(), 2U);
        EXPECT_EQ(versions.value[0].content.value(), "hello resume");
        EXPECT_EQ(
            versions.value[1].content.value(),
            "second version");

        ResumeUpload pdfUpload;
        pdfUpload.title = "PDF resume";
        pdfUpload.filename = "resume.pdf";
        pdfUpload.bytes = "%PDF-1.4\npdf";

        const ServiceResult<ResumeUploadResult> pdf =
            service->Upload(
                userId,
                pdfUpload);

        ASSERT_EQ(pdf.code, ErrorCode::kSuccess);
        EXPECT_EQ(pdf.value.version, 1);

        const ServiceResult<
            std::vector<ResumeVersionRecord>> pdfVersions =
                service->ListVersions(
                    userId,
                    pdf.value.resumeId);

        ASSERT_EQ(pdfVersions.code, ErrorCode::kSuccess);
        ASSERT_EQ(pdfVersions.value.size(), 1U);
        EXPECT_FALSE(
            pdfVersions.value.front().content.has_value());
        EXPECT_TRUE(
            pdfVersions.value.front().filePath.has_value());
    }

    TEST_F(ResumeServiceTest, RejectsInvalidFileInput)
    {
        const std::string large(
            10U * 1024U * 1024U + 1U,
            'x');

        const std::vector<ResumeUpload> invalid = {
            ResumeUpload{
                "title",
                "resume.docx",
                "text",
                std::nullopt},
            ResumeUpload{
                "title",
                "resume.pdf",
                "not a pdf",
                std::nullopt},
            ResumeUpload{
                "title",
                "resume.txt",
                large,
                std::nullopt}};

        for (const ResumeUpload &upload : invalid)
        {
            const ServiceResult<ResumeUploadResult> result =
                service->Upload(
                    userId,
                    upload);

            EXPECT_EQ(
                result.code,
                ErrorCode::kInvalidParam);
        }
    }

    TEST_F(ResumeServiceTest, CompensatesPromoteFailure)
    {
        FailingStorage failingStorage;
        failingStorage.failPromote = true;

        ResumeService failingService(
            *resumeDao,
            *pool,
            failingStorage,
            *authenticator,
            10U * 1024U * 1024U);

        const ServiceResult<ResumeUploadResult> result =
            failingService.Upload(
                userId,
                ResumeUpload{
                    "failing",
                    "resume.txt",
                    "content",
                    std::nullopt});

        EXPECT_EQ(
            result.code,
            ErrorCode::kInternalError);
        EXPECT_EQ(
            resumeDao->CountResumes(userId),
            0U);
        EXPECT_GE(failingStorage.removedTemp, 1);
    }

    TEST_F(ResumeServiceTest, WriteFailureDoesNotCreateMetadata)
    {
        FailingStorage failingStorage;
        failingStorage.failWrite = true;

        ResumeService failingService(
            *resumeDao,
            *pool,
            failingStorage,
            *authenticator,
            10U * 1024U * 1024U);

        const ServiceResult<ResumeUploadResult> result =
            failingService.Upload(
                userId,
                ResumeUpload{
                    "failing",
                    "resume.txt",
                    "content",
                    std::nullopt});

        EXPECT_EQ(
            result.code,
            ErrorCode::kInternalError);
        EXPECT_EQ(
            resumeDao->CountResumes(userId),
            0U);
    }

    TEST_F(ResumeServiceTest, ConcurrentVersionsAreUnique)
    {
        const ServiceResult<ResumeUploadResult> upload =
            service->Upload(
                userId,
                ResumeUpload{
                    "concurrent",
                    "resume.txt",
                    "first",
                    std::nullopt});

        ASSERT_EQ(upload.code, ErrorCode::kSuccess);

        std::mutex mutex;
        std::vector<ServiceResult<ResumeVersionResult>>
            results;

        const auto createVersion =
            [this, resumeId = upload.value.resumeId, &mutex, &results](
                const std::string &content)
            {
                const ServiceResult<ResumeVersionResult> result =
                    service->CreateVersion(
                        userId,
                        resumeId,
                        content);

                std::lock_guard<std::mutex> lock(
                    mutex);
                results.push_back(result);
            };

        std::thread first(
            createVersion,
            "second");
        std::thread second(
            createVersion,
            "third");
        first.join();
        second.join();

        ASSERT_EQ(results.size(), 2U);

        std::set<std::int32_t> versions;

        for (const ServiceResult<ResumeVersionResult> &result :
             results)
        {
            ASSERT_EQ(result.code, ErrorCode::kSuccess);
            versions.insert(result.value.version);
        }

        EXPECT_EQ(
            versions,
            (std::set<std::int32_t>{2, 3}));

        const ServiceResult<
            std::vector<ResumeVersionRecord>> stored =
                service->ListVersions(
                    userId,
                    upload.value.resumeId);

        ASSERT_EQ(stored.code, ErrorCode::kSuccess);
        EXPECT_EQ(stored.value.size(), 3U);
    }

} // namespace jobpilot::service
