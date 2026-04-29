# Top-level test driver.
#
# Each module under src/<name> grows a sibling tests/<name>/ directory.
# The root tests/CMakeLists adds whichever subdirectories exist;
# adding a new test directory means adding one add_subdirectory call.

# The repository root is exposed to tests as VECTRA_REPO_ROOT so a
# test can resolve languages.toml and queries/ without depending on
# the working directory at run time.
add_compile_definitions(
    VECTRA_REPO_ROOT="${CMAKE_SOURCE_DIR}"
)

add_subdirectory(core)
add_subdirectory(store)
add_subdirectory(cli)
if(VECTRA_BUILD_EMBED)
    add_subdirectory(embed)
endif()
