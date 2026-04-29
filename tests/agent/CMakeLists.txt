# Unit tests for vectra-agent.
#
# Real backend round-trips need an API key and network, both of
# which we keep out of the unit suite. The tests here cover the
# config parser and the factory's missing-env-var error path so a
# config regression fails CI before the user ever calls a backend.

add_executable(vectra_agent_tests
    config_test.cpp
)

target_link_libraries(vectra_agent_tests
    PRIVATE
        vectra::agent
        vectra::compile_options
        Catch2::Catch2WithMain
)

set_target_properties(vectra_agent_tests PROPERTIES
    FOLDER "tests")

catch_discover_tests(vectra_agent_tests
    DISCOVERY_MODE PRE_TEST)
