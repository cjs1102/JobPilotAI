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
