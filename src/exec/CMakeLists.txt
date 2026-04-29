# vectra-exec: patch loop and diff machinery.
#
# This module starts as a pure data-transformation library: parse
# unified diffs, apply them with backup, roll back on failure. The
# LLM-driven self-healing loop (which orchestrates retrieve +
# agent + apply + test) lands in subsequent commits.
#
# No third-party dependencies beyond fmt — diff and apply are pure
# stdlib.

add_library(vectra_exec STATIC
    apply.cpp
    diff.cpp
)
add_library(vectra::exec ALIAS vectra_exec)

target_include_directories(vectra_exec
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(vectra_exec
    PUBLIC
        vectra::compile_options
        vectra::sanitizers
    PRIVATE
        fmt::fmt
        spdlog::spdlog
)

set_target_properties(vectra_exec PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    FOLDER                    "vectra")
