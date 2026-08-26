cmake_minimum_required(VERSION 3.22)

project(jobpilot_core CXX)

add_library(jobpilot_core STATIC
    src/core/core.cpp
)

target_include_directories(jobpilot_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)