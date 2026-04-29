# Unit tests for vectra-retrieve.

add_executable(vectra_retrieve_tests
    retriever_test.cpp
)

target_link_libraries(vectra_retrieve_tests
    PRIVATE
        vectra::retrieve
        vectra::core
        vectra::store
        vectra::compile_options
        Catch2::Catch2WithMain
)

set_target_properties(vectra_retrieve_tests PROPERTIES
    FOLDER "tests")

catch_discover_tests(vectra_retrieve_tests
    DISCOVERY_MODE PRE_TEST)
