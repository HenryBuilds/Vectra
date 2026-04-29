# vectra-embed: llama.cpp-backed embedding inference.
#
# Single-binary wrapper around llama.cpp that enforces the pooling
# strategy and instruct-prefix asymmetry that architecture.md
# prescribes. The heavy lifting (kernels, KV cache, quantization) is
# all llama.cpp; we only own the wiring.

add_library(vectra_embed STATIC
    embedder.cpp
)
add_library(vectra::embed ALIAS vectra_embed)

target_include_directories(vectra_embed
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(vectra_embed
    PUBLIC
        vectra::compile_options
        vectra::sanitizers
    PRIVATE
        llama
        fmt::fmt
        spdlog::spdlog
)

set_target_properties(vectra_embed PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    FOLDER                    "vectra")
