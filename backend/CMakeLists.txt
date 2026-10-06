cmake_minimum_required(VERSION 3.22)

project(jobpilot_core C CXX)

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
    src/http/http_response.cpp
    src/http/http_router.cpp
    src/http/http_server.cpp
    src/database/mysql_pool.cpp
    src/database/sql_result.cpp
    src/cache/redis_client.cpp
    src/cache/session_manager.cpp
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
)

add_test(
    NAME bcrypt_test
    COMMAND test_bcrypt
)

include(GoogleTest)

gtest_discover_tests(test_config)
gtest_discover_tests(test_logger)
gtest_discover_tests(test_schema)
gtest_discover_tests(test_sql_result)
