# vectra-core: tree-sitter chunking, language registry, content hashing.
#
# This is the foundational module. Every other Vectra module builds on
# top of vectra::core, either by consuming Chunk values directly or by
# storing them somewhere.

add_library(vectra_core STATIC
    chunk.cpp
    chunker.cpp
    grammars.cpp
    hash.cpp
    language.cpp
    parser_pool.cpp
)
add_library(vectra::core ALIAS vectra_core)

target_include_directories(vectra_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(vectra_core
    PUBLIC
        vectra::compile_options
        vectra::sanitizers
    PRIVATE
        vectra::tree_sitter
        # Built-in grammars. Mirror this list when adding a new
        # language to grammars.cpp and the VECTRA_GRAMMARS variable
        # in the top-level CMakeLists.txt.
        vectra::grammar::c
        vectra::grammar::cpp
        vectra::grammar::python
        vectra::grammar::javascript
        vectra::grammar::typescript
        vectra::grammar::tsx
        vectra::grammar::rust
        vectra::grammar::go
        # Third-party libraries.
        BLAKE3::blake3
        tomlplusplus::tomlplusplus
        fmt::fmt
        spdlog::spdlog
)

set_target_properties(vectra_core PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    FOLDER                    "vectra")
