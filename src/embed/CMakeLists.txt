# vectra-embed: llama.cpp-backed embedding inference plus the model
# registry and HTTPS downloader that feeds it.
#
# The downloader shells out to system `curl` (universal on Windows
# 10+, macOS, and every Linux distro) and uses an in-tree
# header-only SHA-256 for verification. Keeping cpp-httplib and
# OpenSSL out of the dependency graph avoids vcpkg's openssl
# arm64-osx flake without losing anything we actually need.

# llama.cpp's public headers contain patterns that trip our strict
# warning policy (struct/function name shadowing, old-style casts in
# C-compat shims, etc.). Mark llama's and its transitive ggml deps'
# interface include dirs as SYSTEM so GCC suppresses warnings from
# upstream headers — same trick we already use for usearch.
foreach(_llama_dep llama ggml ggml-base ggml-cpu)
    if(TARGET ${_llama_dep})
        get_target_property(_inc ${_llama_dep} INTERFACE_INCLUDE_DIRECTORIES)
        if(_inc)
            set_target_properties(${_llama_dep} PROPERTIES
                INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${_inc}")
        endif()
    endif()
endforeach()

add_library(vectra_embed STATIC
    embedder.cpp
    model_downloader.cpp
    model_registry.cpp
    reranker.cpp
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

# Project version baked in for the HTTP User-Agent string. Stays in
# sync with PROJECT_VERSION from the top-level CMakeLists.
target_compile_definitions(vectra_embed PRIVATE
    VECTRA_VERSION="${PROJECT_VERSION}"
)

set_target_properties(vectra_embed PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    FOLDER                    "vectra")
