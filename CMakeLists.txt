cmake_minimum_required(VERSION 3.25)

# ---------------------------------------------------------------------------
# Project
# ---------------------------------------------------------------------------
project(vectra
    VERSION     0.0.1
    DESCRIPTION "Local-first code RAG and coding assistant"
    LANGUAGES   C CXX
)

# ---------------------------------------------------------------------------
# Global settings
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD          20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS        OFF)

set(CMAKE_C_STANDARD          11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
        Debug Release RelWithDebInfo MinSizeRel)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(VECTRA_BUILD_TESTS         "Build unit tests"                 ON)
option(VECTRA_BUILD_BENCHMARKS    "Build benchmarks"                 OFF)
option(VECTRA_BUILD_EMBED         "Build the embedding module (compiles llama.cpp; slow first build)" ON)
option(VECTRA_ENABLE_ASAN         "Enable AddressSanitizer"          OFF)
option(VECTRA_ENABLE_TSAN         "Enable ThreadSanitizer"           OFF)
option(VECTRA_ENABLE_UBSAN        "Enable UndefinedBehaviorSanitizer" OFF)
option(VECTRA_WARNINGS_AS_ERRORS  "Treat warnings as errors"         ON)

option(VECTRA_GPU_CUDA            "Enable CUDA backend in llama.cpp"  OFF)
option(VECTRA_GPU_METAL           "Enable Metal backend in llama.cpp" OFF)
option(VECTRA_GPU_VULKAN          "Enable Vulkan backend in llama.cpp" OFF)
option(VECTRA_GPU_HIP             "Enable HIP/ROCm backend in llama.cpp" OFF)

# ---------------------------------------------------------------------------
# Compiler flags
# ---------------------------------------------------------------------------
add_library(vectra_compile_options INTERFACE)
add_library(vectra::compile_options ALIAS vectra_compile_options)

if(MSVC)
    target_compile_options(vectra_compile_options INTERFACE
        /W4
        /permissive-
        /Zc:__cplusplus
        /Zc:preprocessor
        /utf-8
        /MP
    )
    if(VECTRA_WARNINGS_AS_ERRORS)
        target_compile_options(vectra_compile_options INTERFACE /WX)
    endif()
    target_compile_definitions(vectra_compile_options INTERFACE
        _CRT_SECURE_NO_WARNINGS
        NOMINMAX
        WIN32_LEAN_AND_MEAN
    )
else()
    target_compile_options(vectra_compile_options INTERFACE
        -Wall -Wextra -Wpedantic
        -Wshadow -Wnon-virtual-dtor -Wold-style-cast
        -Wcast-align -Wunused -Woverloaded-virtual
        -Wconversion -Wsign-conversion -Wnull-dereference
        -Wdouble-promotion -Wformat=2
    )
    if(VECTRA_WARNINGS_AS_ERRORS)
        target_compile_options(vectra_compile_options INTERFACE -Werror)
    endif()
endif()

# ---------------------------------------------------------------------------
# Sanitizers
# ---------------------------------------------------------------------------
add_library(vectra_sanitizers INTERFACE)
add_library(vectra::sanitizers ALIAS vectra_sanitizers)

if(NOT MSVC)
    if(VECTRA_ENABLE_ASAN)
        target_compile_options(vectra_sanitizers INTERFACE -fsanitize=address -fno-omit-frame-pointer)
        target_link_options(vectra_sanitizers INTERFACE -fsanitize=address)
    endif()
    if(VECTRA_ENABLE_TSAN)
        target_compile_options(vectra_sanitizers INTERFACE -fsanitize=thread)
        target_link_options(vectra_sanitizers INTERFACE -fsanitize=thread)
    endif()
    if(VECTRA_ENABLE_UBSAN)
        target_compile_options(vectra_sanitizers INTERFACE -fsanitize=undefined)
        target_link_options(vectra_sanitizers INTERFACE -fsanitize=undefined)
    endif()
endif()

# ---------------------------------------------------------------------------
# Dependencies (vcpkg)
# ---------------------------------------------------------------------------
# SQLite3 in vcpkg ships only the "unofficial-sqlite3" config, not a
# standard SQLite3Config.cmake. The exported target is
# unofficial::sqlite3::sqlite3 — used by vectra-store once it lands.
find_package(unofficial-sqlite3 CONFIG REQUIRED)
find_package(fmt                CONFIG REQUIRED)
find_package(spdlog             CONFIG REQUIRED)
find_package(absl               CONFIG REQUIRED)
find_package(BLAKE3             CONFIG REQUIRED)
find_package(tomlplusplus       CONFIG REQUIRED)
find_package(CLI11              CONFIG REQUIRED)
find_package(httplib            CONFIG REQUIRED)

if(VECTRA_BUILD_TESTS)
    find_package(Catch2 3 CONFIG REQUIRED)
    enable_testing()
    include(Catch)
endif()

if(VECTRA_BUILD_BENCHMARKS)
    find_package(benchmark CONFIG REQUIRED)
endif()

# ---------------------------------------------------------------------------
# Vendored dependencies (third_party submodules)
# ---------------------------------------------------------------------------

# Tree-sitter runtime. The whole library compiles from a single
# translation unit (lib.c includes everything else internally).
add_library(tree_sitter_runtime STATIC
    third_party/tree-sitter/lib/src/lib.c)
add_library(vectra::tree_sitter ALIAS tree_sitter_runtime)
target_include_directories(tree_sitter_runtime
    PUBLIC  $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/third_party/tree-sitter/lib/include>
    PRIVATE ${CMAKE_SOURCE_DIR}/third_party/tree-sitter/lib/src)
set_target_properties(tree_sitter_runtime PROPERTIES
    C_STANDARD                  11
    POSITION_INDEPENDENT_CODE   ON
    FOLDER                      "third_party")
if(MSVC)
    target_compile_options(tree_sitter_runtime PRIVATE /W0)
else()
    target_compile_options(tree_sitter_runtime PRIVATE -w)
endif()

# Tree-sitter grammars. Build each as a static library via the helper.
# The list mirrors the [[language]] entries in languages.toml; keep
# both in sync until we generate one from the other.
include(TreeSitterGrammar)

set(VECTRA_GRAMMARS
    "c"
    "cpp"
    "python"
    "javascript"
    "rust"
    "go"
)
foreach(_lang IN LISTS VECTRA_GRAMMARS)
    add_tree_sitter_grammar(
        NAME ${_lang}
        PATH ${CMAKE_SOURCE_DIR}/third_party/grammars/tree-sitter-${_lang})
endforeach()

# tree-sitter-typescript ships two parser variants in one repo.
add_tree_sitter_grammar(
    NAME typescript
    PATH ${CMAKE_SOURCE_DIR}/third_party/grammars/tree-sitter-typescript/typescript)
add_tree_sitter_grammar(
    NAME tsx
    PATH ${CMAKE_SOURCE_DIR}/third_party/grammars/tree-sitter-typescript/tsx)

# usearch is header-only. Expose its include directory as an INTERFACE
# library so any consumer can link vectra::usearch and pick up the
# headers without compiling anything. The C wrapper under
# third_party/usearch/c is intentionally not used — the C++ index
# template gives us better control over storage and metrics.
#
# Mark the include directory as SYSTEM so the compiler treats those
# headers as out-of-tree: warnings raised inside usearch's templates
# (it is heavy on old-style casts and signed/unsigned mixing) do not
# fail our strict -Werror policy. Per-include #pragma blocks were not
# enough because GCC has no -Weverything kill switch and would still
# see -Wshadow / -Wold-style-cast / -Wdouble-promotion from our top-
# level flag list.
add_library(vectra_usearch INTERFACE)
add_library(vectra::usearch ALIAS vectra_usearch)
target_include_directories(vectra_usearch SYSTEM INTERFACE
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/third_party/usearch/include>)

# llama.cpp — built from source. Heavy first build (~10-15 min);
# incremental builds are fast. Gated behind VECTRA_BUILD_EMBED so
# anyone who only wants to work on core / store / cli can skip it.
# We map our user-facing VECTRA_GPU_* options onto ggml's GGML_*
# toggles before adding the subdirectory; ggml reads its options
# at configure time.
if(VECTRA_BUILD_EMBED)
    set(LLAMA_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
    set(LLAMA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(LLAMA_BUILD_SERVER   OFF CACHE BOOL "" FORCE)
    set(LLAMA_BUILD_TOOLS    OFF CACHE BOOL "" FORCE)
    set(LLAMA_BUILD_COMMON   OFF CACHE BOOL "" FORCE)

    if(VECTRA_GPU_CUDA)
        set(GGML_CUDA   ON CACHE BOOL "" FORCE)
    endif()
    if(VECTRA_GPU_METAL)
        set(GGML_METAL  ON CACHE BOOL "" FORCE)
    endif()
    if(VECTRA_GPU_VULKAN)
        set(GGML_VULKAN ON CACHE BOOL "" FORCE)
    endif()
    if(VECTRA_GPU_HIP)
        set(GGML_HIPBLAS ON CACHE BOOL "" FORCE)
    endif()

    add_subdirectory(third_party/llama.cpp EXCLUDE_FROM_ALL)

    # llama.cpp uses u8"..." literals expecting them to be char[]
    # (C++17 behaviour). In C++20 those literals are char8_t[] and
    # do not implicitly convert to char*, breaking llama-chat.cpp on
    # every strict C++20 compiler. We restore the C++17 typing
    # per-target so Vectra's own code stays standards-conformant:
    #
    #   MSVC:        /Zc:char8_t-
    #   Clang / GCC: -fno-char8_t
    #
    # Setting CMAKE_CXX_FLAGS at the parent scope did not work
    # because llama.cpp's own CMakeLists clears certain flag
    # categories on its targets.
    foreach(_llama_target IN ITEMS
        llama
        ggml
        ggml-base
        ggml-cpu
        ggml-blas
        ggml-cuda
        ggml-hip
        ggml-metal
        ggml-vulkan
        ggml-rpc
        ggml-sycl
        ggml-kompute
        mtmd)
        if(TARGET ${_llama_target})
            if(MSVC)
                target_compile_options(${_llama_target} PRIVATE /Zc:char8_t-)
            else()
                target_compile_options(${_llama_target} PRIVATE -fno-char8_t)
            endif()
        endif()
    endforeach()
endif()

# ---------------------------------------------------------------------------
# Subdirectories (added incrementally as modules land)
# ---------------------------------------------------------------------------
add_subdirectory(src/core)
add_subdirectory(src/store)
if(VECTRA_BUILD_EMBED)
    add_subdirectory(src/embed)
endif()
add_subdirectory(src/retrieve)
# add_subdirectory(src/exec)
add_subdirectory(src/cli)
if(VECTRA_BUILD_TESTS)
    add_subdirectory(tests)
endif()
# if(VECTRA_BUILD_BENCHMARKS)
#     add_subdirectory(benchmarks)
# endif()

# ---------------------------------------------------------------------------
# Status banner
# ---------------------------------------------------------------------------
message(STATUS "")
message(STATUS "Vectra ${PROJECT_VERSION}")
message(STATUS "  Build type   : ${CMAKE_BUILD_TYPE}")
message(STATUS "  C++ standard : ${CMAKE_CXX_STANDARD}")
message(STATUS "  Compiler     : ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "  Tests        : ${VECTRA_BUILD_TESTS}")
message(STATUS "  Benchmarks   : ${VECTRA_BUILD_BENCHMARKS}")
message(STATUS "  Embed        : ${VECTRA_BUILD_EMBED}")
message(STATUS "  Sanitizers   : ASAN=${VECTRA_ENABLE_ASAN} TSAN=${VECTRA_ENABLE_TSAN} UBSAN=${VECTRA_ENABLE_UBSAN}")
message(STATUS "")
