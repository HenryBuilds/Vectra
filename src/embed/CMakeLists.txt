# vectra-embed: llama.cpp-backed embedding inference plus the model
# registry and HTTPS downloader that feeds it.
#
# The download path needs cpp-httplib (HTTPS via OpenSSL) for fetching
# GGUF files from HuggingFace and the OpenSSL crypto library for the
# SHA256 verification step.

# OpenSSL is pulled in transitively by cpp-httplib's [openssl] feature,
# but we resolve it explicitly so the SHA256 verification code can
# include <openssl/evp.h> without depending on the order of includes.
find_package(OpenSSL CONFIG REQUIRED)

add_library(vectra_embed STATIC
    embedder.cpp
    model_downloader.cpp
    model_registry.cpp
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
        httplib::httplib
        OpenSSL::Crypto
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
