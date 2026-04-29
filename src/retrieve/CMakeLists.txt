# vectra-retrieve: hybrid query path.
#
# Combines vector ANN (vectra::store) and BM25 symbol search
# (vectra::store again) into a fused result via Reciprocal Rank
# Fusion. Optionally consumes vectra::embed for the query-side
# embedding step; the dependency is forward-declared in the header
# so consumers that only need symbol-only retrieval do not pay the
# llama.cpp link cost transitively.

add_library(vectra_retrieve STATIC
    retriever.cpp
)
add_library(vectra::retrieve ALIAS vectra_retrieve)

target_include_directories(vectra_retrieve
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(vectra_retrieve
    PUBLIC
        vectra::compile_options
        vectra::sanitizers
        vectra::core
        vectra::store
)

# Embedder is only needed when actually used; we link it
# transitively here so callers do not have to remember to add it
# themselves. Gated on VECTRA_BUILD_EMBED so a build without the
# embedding module still compiles vectra-retrieve (symbol-only
# retrieval still works without an Embedder).
if(VECTRA_BUILD_EMBED)
    target_link_libraries(vectra_retrieve PUBLIC vectra::embed)
endif()

set_target_properties(vectra_retrieve PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    FOLDER                    "vectra")
