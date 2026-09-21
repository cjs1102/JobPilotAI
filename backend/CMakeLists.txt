cmake_minimum_required(VERSION 3.22)

project(jobpilot_core CXX)

add_library(jobpilot_core STATIC
    src/core/core.cpp
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
)

target_include_directories(jobpilot_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
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
