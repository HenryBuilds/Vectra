# Unit tests for vectra CLI components.

add_executable(vectra_cli_tests
    walker_test.cpp
)

target_link_libraries(vectra_cli_tests
    PRIVATE
        vectra::core
        vectra::compile_options
        Catch2::Catch2WithMain
)

# Reach the internal CLI sources without exporting them publicly.
target_include_directories(vectra_cli_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src/cli)

target_sources(vectra_cli_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src/cli/walker.cpp)

set_target_properties(vectra_cli_tests PROPERTIES
    FOLDER "tests")

catch_discover_tests(vectra_cli_tests
    DISCOVERY_MODE PRE_TEST)
