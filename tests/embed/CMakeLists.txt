# Unit tests for vectra-embed.
#
# Most of the meaningful tests need a real GGUF model file, which we
# do not vendor in the repo and do not download from CI by default.
# The tests here cover the cheap error paths so a regression in the
# wrapper (a missing-file check, an exception message) is caught
# without needing model assets.
#
# Once `vectra model pull` lands and CI can fetch a tiny test model,
# this target will gain end-to-end embedding round-trip tests.

add_executable(vectra_embed_tests
    embedder_test.cpp
    model_registry_test.cpp
)

target_link_libraries(vectra_embed_tests
    PRIVATE
        vectra::embed
        vectra::compile_options
        Catch2::Catch2WithMain
)

set_target_properties(vectra_embed_tests PROPERTIES
    FOLDER "tests")

catch_discover_tests(vectra_embed_tests
    DISCOVERY_MODE PRE_TEST)
