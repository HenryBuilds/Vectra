# Unit tests for vectra-core.

add_executable(vectra_core_tests
    chunker_test.cpp
    hash_test.cpp
    language_test.cpp
)

target_link_libraries(vectra_core_tests
    PRIVATE
        vectra::core
        vectra::compile_options
        Catch2::Catch2WithMain
)

set_target_properties(vectra_core_tests PROPERTIES
    FOLDER "tests")

# Discover and register every TEST_CASE with CTest.
catch_discover_tests(vectra_core_tests
    DISCOVERY_MODE PRE_TEST)
