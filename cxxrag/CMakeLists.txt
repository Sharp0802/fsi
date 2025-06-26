project(cxxrag)

if (CMAKE_BUILD_TYPE STREQUAL "Release")
    set(CARGO_BUILD_ARGS "--release")
    set(CARGO_BUILD_PATH "release")
else ()
    set(CARGO_BUILD_ARGS "")
    set(CARGO_BUILD_PATH "debug")
endif ()

add_custom_target(cxxrag ALL
        COMMAND cargo build ${CARGO_BUILD_ARGS}
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/target/${CARGO_BUILD_PATH}/cxxrag"
        "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)
