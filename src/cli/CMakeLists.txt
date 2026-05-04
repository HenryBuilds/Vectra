# vectra CLI — verb-based command-line entry point.

add_executable(vectra
    ask_command.cpp
    claude_subprocess.cpp
    cli_paths.cpp
    index_command.cpp
    main.cpp
    project_config.cpp
    search_command.cpp
    serve_command.cpp
    walker.cpp
)

target_include_directories(vectra
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR})

# Vendored single-header deps go in via SYSTEM so the project-level
# strictness (-Werror -Wold-style-cast -Wsign-conversion -Wshadow,
# all of which httplib.h trips somewhere) does NOT apply to their
# code. Without SYSTEM, the GCC/Clang Linux builds fail at
# httplib.h:367 on `(unsigned char)(char)c`. Our own code keeps its
# warnings active because the regular -I above stays in front.
target_include_directories(vectra
    SYSTEM PRIVATE
        # cpp-httplib (HTTP server + client for `vectra serve` and
        # the daemon-client path in `vectra ask`).
        ${CMAKE_SOURCE_DIR}/third_party
        # nlohmann::json comes for free via llama.cpp's vendor tree;
        # we re-use it here for the daemon-client JSON parse instead
        # of taking another single-header dep.
        ${CMAKE_SOURCE_DIR}/third_party/llama.cpp/vendor)

# httplib includes <winsock2.h> on Windows. Without WIN32_LEAN_AND_MEAN
# (already set at the project level) winsock2.h conflicts with
# windows.h's earlier-include of winsock1, and we'd lose to the
# include-order lottery in main.cpp.
if(WIN32)
    target_link_libraries(vectra PRIVATE ws2_32)
endif()

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
        tomlplusplus::tomlplusplus
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

# ---------------------------------------------------------------------------
# Install rules
# ---------------------------------------------------------------------------
# `cmake --install <build> --prefix <dir>` puts the binary plus the
# vcpkg-resolved DLLs it links against into <dir>/bin. Pointing the
# prefix at e.g. `~/.local` (already a convention on most user PATHs)
# makes `vectra` runnable from any shell.
include(GNUInstallDirs)

install(TARGETS vectra
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

# `vectra index` chunks via tree-sitter and needs both languages.toml
# and the per-language queries/*.scm files at run time. Ship them
# under the standard share/<project>/ layout. resolve_resources()
# probes the exe-relative fallback so the installed binary picks
# them up without needing --resources or VECTRA_RESOURCES.
install(FILES ${CMAKE_SOURCE_DIR}/languages.toml
    DESTINATION ${CMAKE_INSTALL_DATADIR}/vectra)
install(DIRECTORY ${CMAKE_SOURCE_DIR}/queries/
    DESTINATION ${CMAKE_INSTALL_DATADIR}/vectra/queries)

if(WIN32)
    # On Windows the vcpkg-installed dependencies (sqlite3, fmt,
    # spdlog, blake3, llama.cpp, openssl via crypt32, ...) are
    # dynamic by default. TARGET_RUNTIME_DLLS expands to the set the
    # linker reports through the import table, but it misses some
    # libs vcpkg drops in the runtime output dir transitively
    # (OpenSSL, tomlplusplus, brotli). Sweep *.dll alongside it as
    # a fallback so the installed binary doesn't trip on
    # STATUS_DLL_NOT_FOUND.
    install(FILES $<TARGET_RUNTIME_DLLS:vectra>
        DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/
        DESTINATION ${CMAKE_INSTALL_BINDIR}
        FILES_MATCHING PATTERN "*.dll")
endif()
