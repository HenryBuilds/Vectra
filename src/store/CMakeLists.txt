# vectra-store: SQLite + usearch persistence layer.
#
# Persists chunk metadata, embeddings, and file records, and serves
# symbol search via FTS5 trigram and ANN search via an in-memory
# usearch HNSW index that is rebuilt from SQLite at open() time.

add_library(vectra_store STATIC
    schema.cpp
    sqlite_helpers.cpp
    store.cpp
    vector_index.cpp
)
add_library(vectra::store ALIAS vectra_store)

target_include_directories(vectra_store
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(vectra_store
    PUBLIC
        vectra::compile_options
        vectra::sanitizers
        vectra::core
    PRIVATE
        vectra::usearch
        unofficial::sqlite3::sqlite3
        fmt::fmt
        spdlog::spdlog
)

set_target_properties(vectra_store PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    FOLDER                    "vectra")
