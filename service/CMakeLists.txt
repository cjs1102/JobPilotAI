cmake_minimum_required(VERSION 3.22)

project(jobpilot_service C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

enable_testing()

if(PROJECT_IS_TOP_LEVEL)
    add_subdirectory(../backend backend)
endif()

add_executable(jobpilot_service
    src/main.cpp
    src/router.cpp
)

target_include_directories(jobpilot_service
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(jobpilot_service
    PRIVATE
        jobpilot_core
)

add_executable(jobpilot_test
    tests/test_basic.cpp
)

target_link_libraries(jobpilot_test
    PRIVATE
        jobpilot_core
)

add_test(
    NAME basic_test
    COMMAND jobpilot_test
)
