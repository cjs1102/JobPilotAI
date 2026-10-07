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
