# vectra CLI — verb-based command-line entry point.

add_executable(vectra
    claude_subprocess.cpp
    cli_paths.cpp
    fix_command.cpp
    index_command.cpp
    main.cpp
    search_command.cpp
    walker.cpp
)

target_include_directories(vectra
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(vectra
    PRIVATE
        vectra::core
        vectra::store
        vectra::retrieve
        vectra::compile_options
        vectra::sanitizers
        CLI11::CLI11
        fmt::fmt
        spdlog::spdlog
)

# Make the project version available as a compile-time string so
# --version reports the same number that CMake / packaging see.
target_compile_definitions(vectra PRIVATE
    VECTRA_VERSION="${PROJECT_VERSION}"
)

# When the embed module is built, the CLI gains the `vectra model`
# subcommand family. The model_command.cpp directly uses the internal
# model_downloader.hpp from src/embed, so we add that directory to
# the include path for this target only.
if(VECTRA_BUILD_EMBED)
    target_sources(vectra PRIVATE
        model_command.cpp)
    target_include_directories(vectra PRIVATE
        ${CMAKE_SOURCE_DIR}/src/embed)
    target_link_libraries(vectra PRIVATE
        vectra::embed)
    target_compile_definitions(vectra PRIVATE
        VECTRA_HAS_EMBED=1)
endif()

set_target_properties(vectra PROPERTIES
    FOLDER  "vectra"
    OUTPUT_NAME "vectra")
