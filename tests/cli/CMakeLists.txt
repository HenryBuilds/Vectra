# Unit tests for vectra CLI components.

add_executable(vectra_cli_tests
    cli_paths_test.cpp
    walker_test.cpp
)

target_link_libraries(vectra_cli_tests
    PRIVATE
        vectra::core
        vectra::compile_options
        Catch2::Catch2WithMain
        fmt::fmt
)

# Reach the internal CLI sources without exporting them publicly.
target_include_directories(vectra_cli_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src/cli)

target_sources(vectra_cli_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src/cli/cli_paths.cpp
    ${CMAKE_SOURCE_DIR}/src/cli/walker.cpp)

set_target_properties(vectra_cli_tests PROPERTIES
    FOLDER "tests")

catch_discover_tests(vectra_cli_tests
    DISCOVERY_MODE PRE_TEST)
