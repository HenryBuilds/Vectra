# vectra CLI — verb-based command-line entry point.

add_executable(vectra
    index_command.cpp
    main.cpp
    walker.cpp
)

target_include_directories(vectra
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(vectra
    PRIVATE
        vectra::core
        vectra::store
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

set_target_properties(vectra PROPERTIES
    FOLDER  "vectra"
    OUTPUT_NAME "vectra")
