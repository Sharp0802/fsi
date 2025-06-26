cmake_minimum_required(VERSION 4.0)
project(cxxrag)

set(CMAKE_VERBOSE_MAKEFILE ON)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake/")
include(UseClang)
include(UseLLVM)

file(GLOB_RECURSE HEADERS include/*)
file(GLOB_RECURSE SOURCES lib/*)

add_executable(cxxrag ${HEADERS} ${SOURCES})
target_include_directories(cxxrag PUBLIC include)
target_link_libraries(cxxrag PUBLIC
        clang-cpp
)
target_precompile_headers(cxxrag PRIVATE include/Include.h)
target_compile_options(cxxrag PUBLIC -fopenmp=libomp)
target_link_options(cxxrag PUBLIC -fopenmp=libomp)
