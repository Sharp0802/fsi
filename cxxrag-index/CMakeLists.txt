project(cxxrag-index)

set(CMAKE_CXX_STANDARD 26)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake/")
include(UseClang)
include(UseLLVM)

file(GLOB_RECURSE HEADERS include/*)
file(GLOB_RECURSE SOURCES lib/*)

add_executable(cxxrag-index ${HEADERS} ${SOURCES})
target_include_directories(cxxrag-index PUBLIC include)
target_link_libraries(cxxrag-index PUBLIC
        clang-cpp
)
target_precompile_headers(cxxrag-index PRIVATE include/Include.h)
target_compile_options(cxxrag-index PUBLIC -fopenmp=libomp)
target_link_options(cxxrag-index PUBLIC -fopenmp=libomp)
