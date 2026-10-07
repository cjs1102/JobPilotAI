cmake_minimum_required(VERSION 3.22)

project(jobpilot_core C CXX)

option(
    JP_ENABLE_ASAN
    "Build JobPilotAI targets with AddressSanitizer"
    OFF
)

option(
    JP_ENABLE_UBSAN
    "Build JobPilotAI targets with UndefinedBehaviorSanitizer"
    OFF
)

option(
    JP_ENABLE_TSAN
    "Build JobPilotAI targets with ThreadSanitizer"
    OFF
)

option(
    JP_BUILD_BENCHMARKS
    "Build the isolated JobPilotAI microbenchmark runner"
    OFF
)

if(JP_ENABLE_TSAN AND (JP_ENABLE_ASAN OR JP_ENABLE_UBSAN))
    message(
        FATAL_ERROR
        "TSan cannot be combined with ASan or UBSan in the same build"
    )
endif()

add_library(jobpilot_sanitizers INTERFACE)

if(JP_ENABLE_ASAN)
    target_compile_options(
        jobpilot_sanitizers
        INTERFACE
            $<$<COMPILE_LANGUAGE:C,CXX>:-fsanitize=address>
            $<$<COMPILE_LANGUAGE:C,CXX>:-fno-omit-frame-pointer>
            $<$<COMPILE_LANGUAGE:C,CXX>:-fno-common>
    )

    target_link_options(
        jobpilot_sanitizers
        INTERFACE
            -fsanitize=address
    )
endif()

if(JP_ENABLE_UBSAN)
    target_compile_options(
        jobpilot_sanitizers
        INTERFACE
            $<$<COMPILE_LANGUAGE:C,CXX>:-fsanitize=undefined>
            $<$<COMPILE_LANGUAGE:C,CXX>:-fno-sanitize-recover=undefined>
            $<$<COMPILE_LANGUAGE:C,CXX>:-fno-omit-frame-pointer>
    )

    target_link_options(
        jobpilot_sanitizers
        INTERFACE
            -fsanitize=undefined
    )
endif()

if(JP_ENABLE_TSAN)
    target_compile_options(
        jobpilot_sanitizers
        INTERFACE
            $<$<COMPILE_LANGUAGE:C,CXX>:-fsanitize=thread>
            $<$<COMPILE_LANGUAGE:C,CXX>:-fno-omit-frame-pointer>
    )

    target_link_options(
        jobpilot_sanitizers
        INTERFACE
            -fsanitize=thread
    )
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(MYSQLCLIENT REQUIRED IMPORTED_TARGET mysqlclient)
find_package(GTest CONFIG REQUIRED)
find_package(hiredis CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(yaml-cpp CONFIG REQUIRED)

add_subdirectory(
    ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/libbcrypt
    libbcrypt
)

target_link_libraries(
    jobpilot_bcrypt
    PRIVATE
        jobpilot_sanitizers
)

add_library(jobpilot_core STATIC
    src/core/config.cpp
    src/core/core.cpp
    src/core/logger.cpp
    src/core/thread_pool.cpp
    src/network/channel.cpp
    src/network/epoll_poller.cpp
    src/network/event_loop.cpp
    src/network/buffer.cpp
    src/network/tcp_connection.cpp
    src/network/tcp_server.cpp
    src/http/http_request.cpp
    src/http/http_request_parser.cpp
    src/http/multipart_form_data_parser.cpp
    src/http/http_response.cpp
    src/http/http_router.cpp
    src/http/http_server.cpp
    src/database/mysql_pool.cpp
    src/database/sql_result.cpp
    src/cache/redis_client.cpp
    src/cache/session_manager.cpp
    src/storage/local_file_storage.cpp
)

target_include_directories(jobpilot_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(jobpilot_core
    PUBLIC
        PkgConfig::MYSQLCLIENT
        hiredis::hiredis
        spdlog::spdlog
        jobpilot_sanitizers
    PRIVATE
        yaml-cpp::yaml-cpp
)

# Tests use <cassert>. Keep their checks active in Release builds.
add_compile_options(
    $<$<COMPILE_LANGUAGE:CXX>:-UNDEBUG>
)

add_executable(test_config
    tests/test_config.cpp
)

target_link_libraries(test_config
    PRIVATE
        jobpilot_core
        GTest::gtest_main
)

add_executable(test_logger
    tests/test_logger.cpp
)

target_link_libraries(test_logger
    PRIVATE
        jobpilot_core
        GTest::gtest_main
)

add_executable(test_http_correctness
    tests/test_http_correctness.cpp
)

target_link_libraries(test_http_correctness
    PRIVATE
        jobpilot_core
        GTest::gtest_main
)

add_executable(test_schema
    tests/test_schema.cpp
)

target_link_libraries(test_schema
    PRIVATE
        jobpilot_core
        GTest::gtest_main
)

add_executable(test_event_loop
    tests/test_event_loop.cpp

)

target_link_libraries(test_event_loop
    PRIVATE
        jobpilot_core
)

add_test(
    NAME event_loop_test
    COMMAND test_event_loop
)
add_executable(test_buffer
    tests/test_buffer.cpp
)

target_link_libraries(test_buffer
    PRIVATE
        jobpilot_core
)

add_test(
    NAME buffer_test
    COMMAND test_buffer
)

add_executable(test_buffer_sigpipe
    tests/test_buffer_sigpipe.cpp
)

target_link_libraries(test_buffer_sigpipe
    PRIVATE
        jobpilot_core
)

add_test(
    NAME buffer_sigpipe_test
    COMMAND test_buffer_sigpipe
)

    add_executable(test_tcp_connection
    tests/test_tcp_connection.cpp
)

target_link_libraries(test_tcp_connection
    PRIVATE
        jobpilot_core
)

add_test(
    NAME tcp_connection_test
    COMMAND test_tcp_connection
)

add_executable(
    test_tcp_connection_hangup
    tests/test_tcp_connection_hangup.cpp
)

target_link_libraries(
    test_tcp_connection_hangup
    PRIVATE jobpilot_core
)

add_test(
    NAME tcp_connection_hangup_test
    COMMAND test_tcp_connection_hangup
)

add_executable(test_tcp_connection_send
    tests/test_tcp_connection_send.cpp
)

target_link_libraries(test_tcp_connection_send
    PRIVATE
        jobpilot_core
)

add_test(
    NAME tcp_connection_send_test
    COMMAND test_tcp_connection_send
)

add_executable(
    test_tcp_connection_peer_close
    tests/test_tcp_connection_peer_close.cpp
)

target_link_libraries(
    test_tcp_connection_peer_close
    PRIVATE jobpilot_core
)

add_test(
    NAME tcp_connection_peer_close_test
    COMMAND test_tcp_connection_peer_close
)

add_executable(
    test_tcp_connection_write_buffer
    tests/test_tcp_connection_write_buffer.cpp
)

target_link_libraries(
    test_tcp_connection_write_buffer
    PRIVATE jobpilot_core
)

add_test(
    NAME tcp_connection_write_buffer_test
    COMMAND test_tcp_connection_write_buffer
)

add_executable(test_tcp_server
    tests/test_tcp_server.cpp
)

target_link_libraries(test_tcp_server
    PRIVATE
    jobpilot_core
)

add_test(
    NAME tcp_server_test
    COMMAND test_tcp_server
)

add_executable(test_http_request
    tests/test_http_request.cpp
)

target_link_libraries(test_http_request
    PRIVATE
        jobpilot_core
)

add_test(
    NAME http_request_test
    COMMAND test_http_request
)

add_executable(test_http_response
    tests/test_http_response.cpp
)

target_link_libraries(test_http_response
    PRIVATE
        jobpilot_core
)

add_test(
    NAME http_response_test
    COMMAND test_http_response
)

add_executable(test_http_router
    tests/test_http_router.cpp
)

target_link_libraries(test_http_router
    PRIVATE
        jobpilot_core
)

add_test(
    NAME http_router_test
    COMMAND test_http_router
)

add_executable(test_http_server
    tests/test_http_server.cpp
)

target_link_libraries(test_http_server
    PRIVATE
        jobpilot_core
)

add_test(
    NAME http_server_test
    COMMAND test_http_server
)

add_executable(
    test_multipart_form_data_parser
    tests/test_multipart_form_data_parser.cpp
)

target_link_libraries(
    test_multipart_form_data_parser
    PRIVATE
        jobpilot_core
)

add_test(
    NAME multipart_form_data_parser_test
    COMMAND test_multipart_form_data_parser
)

add_executable(
    test_file_storage
    tests/test_file_storage.cpp
)

target_link_libraries(
    test_file_storage
    PRIVATE
        jobpilot_core
)

add_test(
    NAME file_storage_test
    COMMAND test_file_storage
)

add_executable(test_thread_pool
    tests/test_thread_pool.cpp
)

target_link_libraries(test_thread_pool
    PRIVATE
        jobpilot_core
)

add_test(
    NAME thread_pool_test
    COMMAND test_thread_pool
)

add_executable(test_mysql_pool
    tests/test_mysql_pool.cpp
)

target_link_libraries(test_mysql_pool
    PRIVATE
        jobpilot_core
)

add_test(
    NAME mysql_pool_test
    COMMAND test_mysql_pool
)

add_executable(test_sql_result
    tests/test_sql_result.cpp
)

target_link_libraries(test_sql_result
    PRIVATE
        jobpilot_core
        GTest::gtest_main
)

add_executable(test_redis_client
    tests/test_redis_client.cpp
)

target_link_libraries(test_redis_client
    PRIVATE
        jobpilot_core
)

add_test(
    NAME redis_client_test
    COMMAND test_redis_client
)

add_executable(test_bcrypt
    tests/test_bcrypt.cpp
)

target_link_libraries(test_bcrypt
    PRIVATE
        jobpilot_bcrypt
        jobpilot_sanitizers
)

add_test(
    NAME bcrypt_test
    COMMAND test_bcrypt
)

set_tests_properties(
    event_loop_test
    thread_pool_test
    tcp_connection_test
    tcp_connection_hangup_test
    tcp_connection_send_test
    tcp_connection_peer_close_test
    tcp_connection_write_buffer_test
    tcp_server_test
    http_server_test
    PROPERTIES
        LABELS "concurrency;tsan"
)

set_tests_properties(
    mysql_pool_test
    redis_client_test
    PROPERTIES
        LABELS "external;integration"
)

if(JP_BUILD_BENCHMARKS)
    add_executable(
        jobpilot_microbench
        benchmarks/microbenchmarks.cpp
    )

    target_link_libraries(
        jobpilot_microbench
        PRIVATE
            jobpilot_core
    )

    add_custom_target(
        run_microbench
        COMMAND jobpilot_microbench
        DEPENDS jobpilot_microbench
        USES_TERMINAL
    )
endif()

if(JP_ENABLE_TSAN AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(_jp_tsan_launcher
        setarch
        ${CMAKE_SYSTEM_PROCESSOR}
        -R
    )

    set_target_properties(
        test_config
        test_logger
        test_http_correctness
        test_schema
        test_event_loop
        test_buffer
        test_buffer_sigpipe
        test_tcp_connection
        test_tcp_connection_hangup
        test_tcp_connection_send
        test_tcp_connection_peer_close
        test_tcp_connection_write_buffer
        test_tcp_server
        test_http_request
        test_http_response
        test_http_router
        test_http_server
        test_multipart_form_data_parser
        test_file_storage
        test_thread_pool
        test_mysql_pool
        test_sql_result
        test_redis_client
        test_bcrypt
        PROPERTIES
            CROSSCOMPILING_EMULATOR
                "${_jp_tsan_launcher}"
    )

    unset(_jp_tsan_launcher)
endif()

include(GoogleTest)

gtest_discover_tests(test_config)
gtest_discover_tests(test_logger)
gtest_discover_tests(test_sql_result)
gtest_discover_tests(test_http_correctness)

gtest_discover_tests(
    test_schema
    PROPERTIES
        LABELS "external;integration"
)
