# Unit tests for vectra-store.

add_executable(vectra_store_tests
    store_test.cpp
    vector_index_test.cpp
)

target_link_libraries(vectra_store_tests
    PRIVATE
        vectra::store
        vectra::compile_options
        Catch2::Catch2WithMain
)

# vector_index is internal — let tests reach it via src/store/.
target_include_directories(vectra_store_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src/store)

set_target_properties(vectra_store_tests PROPERTIES
    FOLDER "tests")

catch_discover_tests(vectra_store_tests
    DISCOVERY_MODE PRE_TEST)
