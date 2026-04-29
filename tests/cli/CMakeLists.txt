# Unit tests for vectra CLI components.

add_executable(vectra_cli_tests
    approval_test.cpp
    diff_render_test.cpp
    repl_command_test.cpp
    walker_test.cpp
)

target_link_libraries(vectra_cli_tests
    PRIVATE
        vectra::core
        vectra::agent
        vectra::exec
        vectra::compile_options
        Catch2::Catch2WithMain
        fmt::fmt
)

# Reach the internal CLI sources without exporting them publicly.
target_include_directories(vectra_cli_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src/cli)

target_sources(vectra_cli_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src/cli/approval.cpp
    ${CMAKE_SOURCE_DIR}/src/cli/diff_render.cpp
    ${CMAKE_SOURCE_DIR}/src/cli/repl_command.cpp
    ${CMAKE_SOURCE_DIR}/src/cli/walker.cpp)

set_target_properties(vectra_cli_tests PROPERTIES
    FOLDER "tests")

catch_discover_tests(vectra_cli_tests
    DISCOVERY_MODE PRE_TEST)
