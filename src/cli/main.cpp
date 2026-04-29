// Copyright 2026 Vectra Contributors. Apache-2.0.
//
// Vectra command-line entry point.
//
// The CLI follows the verb-based shape that git, cargo, kubectl, and
// gh use: `vectra <subcommand> [args]`. Currently only `index` is
// implemented; `search`, `embed`, `model`, and `serve` will land
// alongside their backing modules.

#include <fmt/format.h>

#include <CLI/CLI.hpp>
#include <exception>
#include <iostream>

#include "index_command.hpp"

namespace {

// VECTRA_VERSION is injected by the top-level CMakeLists.txt so the
// version reported by --version stays in sync with the project file.
#ifndef VECTRA_VERSION
#define VECTRA_VERSION "0.0.0+unknown"
#endif

}  // namespace

int main(int argc, char** argv) {
    CLI::App app{"Vectra — local-first code RAG and coding assistant", "vectra"};
    app.set_version_flag("-V,--version", VECTRA_VERSION);
    app.require_subcommand(1);

    // ---- index ------------------------------------------------------------
    auto* index_cmd = app.add_subcommand("index", "Index a source tree into the local database");

    vectra::cli::IndexOptions index_opts;
    index_cmd->add_option("path", index_opts.root, "Directory to index")
        ->required()
        ->check(CLI::ExistingDirectory);
    index_cmd->add_option(
        "--db", index_opts.db, "Database file (default: <path>/.vectra/index.db)");
    index_cmd->add_option(
        "--resources", index_opts.resources, "Directory containing languages.toml and queries/");
    index_cmd->add_flag("-q,--quiet", index_opts.quiet, "Suppress per-file output");

    int exit_code = 0;
    index_cmd->callback([&] {
        try {
            exit_code = vectra::cli::run_index(index_opts);
        } catch (const std::exception& e) {
            fmt::print(stderr, "error: {}\n", e.what());
            exit_code = 1;
        }
    });

    CLI11_PARSE(app, argc, argv);
    return exit_code;
}
