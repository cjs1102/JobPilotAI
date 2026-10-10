cmake_minimum_required(VERSION 3.22)

project(jobpilot_service C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

enable_testing()

if(PROJECT_IS_TOP_LEVEL)
    add_subdirectory(../backend backend)
endif()

find_package(nlohmann_json CONFIG REQUIRED)
find_package(GTest CONFIG REQUIRED)

add_library(jobpilot_service_core STATIC
    src/router.cpp
    src/auth/session_authenticator.cpp
    src/ai/ai_controller.cpp
    src/ai/ai_internal_controller.cpp
    src/ai/ai_task_dao.cpp
    src/ai/ai_task_service.cpp
    src/ai/match_result_json.cpp
    src/ai/match_rule.cpp
    src/application/application_controller.cpp
    src/application/application_dao.cpp
    src/application/application_service.cpp
    src/job/job_controller.cpp
    src/job/job_dao.cpp
    src/job/job_extra_fields.cpp
    src/job/job_ingestion_dao.cpp
    src/job/job_ingestion_service.cpp
    src/job/match_explanation.cpp
    src/job/job_quality.cpp
    src/job/job_ranking.cpp
    src/job/job_ranking_service.cpp
    src/job/job_review.cpp
    src/job/job_review_dao.cpp
    src/job/job_search_filter.cpp
    src/job/internal_job_import_controller.cpp
    src/job/internal_job_review_controller.cpp
    src/job/internal_source_sync_controller.cpp
    src/job/job_service.cpp
    src/job/source_observation.cpp
    src/job/source_sync_dao.cpp
    src/job/source_sync_service.cpp
    src/resume/resume_controller.cpp
    src/resume/resume_dao.cpp
    src/resume/resume_service.cpp
    src/resume/resume_text_status.cpp
    src/resume/resume_artifact_dao.cpp
    src/resume/resume_artifact_service.cpp
    src/resume/tailor_rule.cpp
    src/user/user_controller.cpp
    src/user/user_dao.cpp
    src/user/user_service.cpp
)

target_include_directories(jobpilot_service_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(jobpilot_service_core
    PUBLIC
        jobpilot_core
        jobpilot_bcrypt
        nlohmann_json::nlohmann_json
)

add_executable(jobpilot_service
    src/main.cpp
)

target_link_libraries(jobpilot_service
    PRIVATE
        jobpilot_service_core
)

add_executable(jobpilot_test
    tests/test_basic.cpp
)

target_link_libraries(jobpilot_test
    PRIVATE
        jobpilot_service_core
)

add_test(
    NAME basic_test
    COMMAND jobpilot_test
)

add_executable(test_user_dao
    tests/test_user_dao.cpp
)

target_link_libraries(test_user_dao
    PRIVATE
        jobpilot_service_core
        GTest::gtest_main
)

add_executable(test_user_service
    tests/test_user_service.cpp
)

target_link_libraries(test_user_service
    PRIVATE
        jobpilot_service_core
        GTest::gtest_main
)

add_executable(test_user_http
    tests/test_user_http.cpp
)

target_link_libraries(test_user_http
    PRIVATE
        jobpilot_service_core
        GTest::gtest_main
)

add_executable(
    test_session_authenticator
    tests/test_session_authenticator.cpp
)

target_link_libraries(
    test_session_authenticator
    PRIVATE
        jobpilot_service_core
        GTest::gtest_main
)

add_executable(
    test_resume_dao
    tests/test_resume_dao.cpp
)

target_link_libraries(
    test_resume_dao
    PRIVATE
        jobpilot_service_core
        GTest::gtest_main
)

add_executable(
    test_resume_service
    tests/test_resume_service.cpp
)

target_link_libraries(
    test_resume_service
    PRIVATE
        jobpilot_service_core
        GTest::gtest_main
)

add_executable(
    test_resume_http
    tests/test_resume_http.cpp
)

target_link_libraries(
    test_resume_http
    PRIVATE
        jobpilot_service_core
        GTest::gtest_main
)

add_executable(
    test_job_dao
    tests/test_job_dao.cpp
)

target_link_libraries(
    test_job_dao
    PRIVATE
        jobpilot_service_core
        GTest::gtest_main
)

add_executable(
    test_job_ingestion_dao
    tests/test_job_ingestion_dao.cpp
)

target_link_libraries(
    test_job_ingestion_dao
    PRIVATE
        jobpilot_service_core
        GTest::gtest_main
)

add_executable(
    test_job_ingestion_service
    tests/test_job_ingestion_service.cpp
)

target_link_libraries(
    test_job_ingestion_service
    PRIVATE
        jobpilot_service_core
        GTest::gtest_main
)

add_executable(
    test_job_ingestion_http
    tests/test_job_ingestion_http.cpp
)

target_link_libraries(
    test_job_ingestion_http
    PRIVATE
        jobpilot_service_core
        GTest::gtest_main
)

add_executable(
    test_job_service
    tests/test_job_service.cpp
)

target_link_libraries(
    test_job_service
    PRIVATE
        jobpilot_service_core
        GTest::gtest_main
)

add_executable(
    test_job_ranking_service
    tests/test_job_ranking_service.cpp
)

target_link_libraries(
    test_job_ranking_service
    PRIVATE
        jobpilot_service_core
        GTest::gtest_main
)

add_executable(
    test_source_observation
    tests/test_source_observation.cpp
)

target_link_libraries(
    test_source_observation
    PRIVATE
        jobpilot_service_core
        GTest::gtest_main
)

add_executable(
    test_job_quality
    tests/test_job_quality.cpp
)

target_link_libraries(
    test_job_quality
    PRIVATE
        jobpilot_service_core
        GTest::gtest_main
)

add_executable(
    test_job_review_http
    tests/test_job_review_http.cpp
)

target_link_libraries(
    test_job_review_http
    PRIVATE
        jobpilot_service_core
        GTest::gtest_main
)

add_executable(
    test_deadline_time
    tests/test_deadline_time.cpp
)

target_link_libraries(
    test_deadline_time
    PRIVATE
        jobpilot_service_core
        GTest::gtest_main
)

add_executable(
    test_source_sync_dao
    tests/test_source_sync_dao.cpp
)

target_link_libraries(
    test_source_sync_dao
    PRIVATE
        jobpilot_service_core
        GTest::gtest_main
)

add_executable(
    test_source_sync_service
    tests/test_source_sync_service.cpp
)

target_link_libraries(
    test_source_sync_service
    PRIVATE
        jobpilot_service_core
        GTest::gtest_main
)

add_executable(
    test_source_sync_http
    tests/test_source_sync_http.cpp
)

target_link_libraries(
    test_source_sync_http
    PRIVATE
        jobpilot_service_core
        GTest::gtest_main
)

add_executable(
    test_source_sync_e2e
    tests/test_source_sync_e2e.cpp
)

target_link_libraries(
    test_source_sync_e2e
    PRIVATE
        jobpilot_service_core
        GTest::gtest_main
)

add_executable(
    test_ai_task_dao
    tests/test_ai_task_dao.cpp
)

target_link_libraries(
    test_ai_task_dao
    PRIVATE
        jobpilot_service_core
        GTest::gtest_main
)

add_executable(
    test_match_rule
    tests/test_match_rule.cpp
)

target_link_libraries(
    test_match_rule
    PRIVATE
        jobpilot_service_core
        GTest::gtest_main
)

add_executable(
    test_ai_task_service
    tests/test_ai_task_service.cpp
)

target_link_libraries(
    test_ai_task_service
    PRIVATE
        jobpilot_service_core
        GTest::gtest_main
)

add_executable(
    test_match_result_json
    tests/test_match_result_json.cpp
)

target_link_libraries(
    test_match_result_json
    PRIVATE
        jobpilot_service_core
        GTest::gtest_main
)

add_executable(
    test_ai_http
    tests/test_ai_http.cpp
)

target_link_libraries(
    test_ai_http
    PRIVATE
        jobpilot_service_core
        GTest::gtest_main
)

add_executable(
    test_job_http
    tests/test_job_http.cpp
)

target_link_libraries(
    test_job_http
    PRIVATE
        jobpilot_service_core
        GTest::gtest_main
)

add_executable(
    test_application_dao
    tests/test_application_dao.cpp
)

target_link_libraries(
    test_application_dao
    PRIVATE
        jobpilot_service_core
        GTest::gtest_main
)

add_executable(
    test_application_service
    tests/test_application_service.cpp
)

target_link_libraries(
    test_application_service
    PRIVATE
        jobpilot_service_core
        GTest::gtest_main
)

add_executable(
    test_application_http
    tests/test_application_http.cpp
)

target_link_libraries(
    test_application_http
    PRIVATE
        jobpilot_service_core
        GTest::gtest_main
)

if(JP_ENABLE_TSAN AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(_jp_service_tsan_launcher
        setarch
        ${CMAKE_SYSTEM_PROCESSOR}
        -R
    )

    set_target_properties(
        test_user_dao
        test_user_service
        test_user_http
        test_session_authenticator
        test_resume_dao
        test_resume_service
        test_resume_http
        test_job_dao
        test_job_ingestion_dao
        test_job_ingestion_service
        test_job_ingestion_http
        test_job_service
        test_job_http
        test_application_dao
        test_application_service
        test_application_http
        PROPERTIES
            CROSSCOMPILING_EMULATOR
                "${_jp_service_tsan_launcher}"
    )

    unset(_jp_service_tsan_launcher)
endif()

include(GoogleTest)

gtest_discover_tests(
    test_user_dao
    PROPERTIES
        LABELS "user;external;integration"
)

gtest_discover_tests(
    test_user_service
    PROPERTIES
        LABELS "user;integration;tsan"
)

gtest_discover_tests(
    test_user_http
    PROPERTIES
        LABELS "user;integration;tsan"
)

gtest_discover_tests(
    test_session_authenticator
    PROPERTIES
        LABELS "auth;integration"
)

gtest_discover_tests(
    test_resume_dao
    PROPERTIES
        LABELS "resume;external;integration"
)

gtest_discover_tests(
    test_resume_service
    PROPERTIES
        LABELS "resume;integration;tsan"
)

gtest_discover_tests(
    test_resume_http
    PROPERTIES
        LABELS "resume;integration;tsan"
)

add_executable(
    test_tailor_rule
    tests/test_tailor_rule.cpp
)
target_link_libraries(
    test_tailor_rule
    PRIVATE
        jobpilot_service_core
        GTest::gtest_main
)

gtest_discover_tests(
    test_tailor_rule
    PROPERTIES
        LABELS "resume;tsan"
)

add_executable(
    test_resume_artifact_dao
    tests/test_resume_artifact_dao.cpp
)

target_link_libraries(
    test_resume_artifact_dao
    PRIVATE
        jobpilot_service_core
        GTest::gtest_main
)

gtest_discover_tests(
    test_resume_artifact_dao
    PROPERTIES
        LABELS "resume;external;integration"
)

# These two targets are defined after the main CROSSCOMPILING_EMULATOR block
# above, so they get the TSan launcher here instead: naming a target before
# add_executable makes every TSan configure fail. Keep the two blocks in sync.
if(JP_ENABLE_TSAN AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set_target_properties(
        test_tailor_rule
        test_resume_artifact_dao
        PROPERTIES
            CROSSCOMPILING_EMULATOR
                "setarch;${CMAKE_SYSTEM_PROCESSOR};-R"
    )
endif()

gtest_discover_tests(
    test_job_dao
    PROPERTIES
        LABELS "job;external;integration"
)

gtest_discover_tests(
    test_job_ingestion_dao
    PROPERTIES
        LABELS "job;external;integration"
)

gtest_discover_tests(
    test_job_ingestion_service
    PROPERTIES
        LABELS "job;external;integration"
)

gtest_discover_tests(
    test_job_ingestion_http
    PROPERTIES
        LABELS "job;integration;tsan"
)

gtest_discover_tests(
    test_job_service
    PROPERTIES
        LABELS "job;integration"
)

gtest_discover_tests(
    test_job_ranking_service
    PROPERTIES
        LABELS "job;integration"
)

gtest_discover_tests(
    test_source_observation
    PROPERTIES
        LABELS "job"
)

gtest_discover_tests(
    test_job_quality
    PROPERTIES
        LABELS "job"
)

gtest_discover_tests(
    test_job_review_http
    PROPERTIES
        LABELS "job;integration"
)

gtest_discover_tests(
    test_deadline_time
    PROPERTIES
        LABELS "job"
)

gtest_discover_tests(
    test_source_sync_dao
    PROPERTIES
        LABELS "job;external;integration"
)

gtest_discover_tests(
    test_source_sync_service
    PROPERTIES
        LABELS "job;external;integration"
)

gtest_discover_tests(
    test_source_sync_http
    PROPERTIES
        LABELS "job;integration"
)

gtest_discover_tests(
    test_source_sync_e2e
    PROPERTIES
        LABELS "job;integration"
)

gtest_discover_tests(
    test_ai_task_dao
    LABELS "ai;integration"
)

gtest_discover_tests(
    test_match_rule
    LABELS "ai"
)

gtest_discover_tests(
    test_ai_task_service
    PROPERTIES
        LABELS "ai"
)

gtest_discover_tests(
    test_match_result_json
    PROPERTIES
        LABELS "ai"
)

gtest_discover_tests(
    test_ai_http
    PROPERTIES
        LABELS "ai;integration"
)

gtest_discover_tests(
    test_job_http
    PROPERTIES
        LABELS "job;integration"
)

gtest_discover_tests(
    test_application_dao
    PROPERTIES
        LABELS "application;external;integration"
)

gtest_discover_tests(
    test_application_service
    PROPERTIES
        LABELS "application;integration"
)

gtest_discover_tests(
    test_application_http
    PROPERTIES
        LABELS "application;integration"
)
