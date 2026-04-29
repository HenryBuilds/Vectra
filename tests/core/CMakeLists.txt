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

# Some tests need to construct internal types (e.g. ParserPool) that
# live in src/core/ but are not part of the public include/ tree.
# Allow tests to reach them by adding src/core/ to the test target's
# include path. Production consumers still see only the public API.
target_include_directories(vectra_core_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src/core)

set_target_properties(vectra_core_tests PROPERTIES
    FOLDER "tests")

# Discover and register every TEST_CASE with CTest.
catch_discover_tests(vectra_core_tests
    DISCOVERY_MODE PRE_TEST)
