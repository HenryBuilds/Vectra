# Unit tests for vectra-exec.

add_executable(vectra_exec_tests
    apply_test.cpp
    diff_test.cpp
    patch_loop_test.cpp
    test_runner_test.cpp
)

target_link_libraries(vectra_exec_tests
    PRIVATE
        vectra::exec
        vectra::compile_options
        Catch2::Catch2WithMain
)

set_target_properties(vectra_exec_tests PROPERTIES
    FOLDER "tests")

catch_discover_tests(vectra_exec_tests
    DISCOVERY_MODE PRE_TEST)
