cmake_minimum_required(VERSION 3.22)

project(jobpilot_service CXX)

add_executable(jobpilot_service
    src/main.cpp
    src/health_stub.cpp
)

target_include_directories(jobpilot_service
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(jobpilot_service
    PRIVATE
        jobpilot_core
)

enable_testing()

add_executable(jobpilot_test
    tests/test_basic.cpp
)

add_test(
    NAME basic_test
    COMMAND jobpilot_test
)
