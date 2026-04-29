# vectra-agent: LLM-backend abstraction.
#
# Owns the LlmBackend interface and its concrete implementations
# (Anthropic direct API today; OpenAI-compatible, Claude Code
# subprocess, and llama-cpp local in subsequent commits). The
# patch loop in vectra-exec talks only to the abstract interface.

add_library(vectra_agent STATIC
    anthropic_backend.cpp
    llm_backend.cpp
    sse_parser.cpp
)
add_library(vectra::agent ALIAS vectra_agent)

target_include_directories(vectra_agent
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(vectra_agent
    PUBLIC
        vectra::compile_options
        vectra::sanitizers
    PRIVATE
        httplib::httplib
        nlohmann_json::nlohmann_json
        tomlplusplus::tomlplusplus
        fmt::fmt
        spdlog::spdlog
)

set_target_properties(vectra_agent PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    FOLDER                    "vectra")
