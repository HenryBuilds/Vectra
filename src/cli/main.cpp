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
#include "search_command.hpp"

#if VECTRA_HAS_EMBED
#include "model_command.hpp"
#endif

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

    // ---- search ----------------------------------------------------------
    auto* search_cmd = app.add_subcommand("search", "Hybrid search across the local index");

    vectra::cli::SearchOptions search_opts;
    search_cmd->add_option("query", search_opts.query, "Search query")->required();
    search_cmd->add_option(
        "--db", search_opts.db, "Database file (default: walks up to find .vectra/index.db)");
    search_cmd->add_option("-k,--top-k", search_opts.k, "Number of hits to return")
        ->default_val(10);
    search_cmd->add_option(
        "--model", search_opts.model, "Embedding model name (skip for symbol-only retrieval)");
    search_cmd->add_flag(
        "--show-text", search_opts.show_text, "Print full chunk text under each hit");
    search_cmd->callback([&] {
        try {
            exit_code = vectra::cli::run_search(search_opts);
        } catch (const std::exception& e) {
            fmt::print(stderr, "error: {}\n", e.what());
            exit_code = 1;
        }
    });

#if VECTRA_HAS_EMBED
    // ---- model -----------------------------------------------------------
    auto* model_cmd = app.add_subcommand("model", "Manage embedding models");
    model_cmd->require_subcommand(1);

    auto* model_list = model_cmd->add_subcommand("list", "List built-in embedding models");
    model_list->callback([&] {
        try {
            exit_code = vectra::cli::run_model_list();
        } catch (const std::exception& e) {
            fmt::print(stderr, "error: {}\n", e.what());
            exit_code = 1;
        }
    });

    vectra::cli::ModelWhereOptions where_opts;
    auto* model_where = model_cmd->add_subcommand("where", "Print the local cache path of a model");
    model_where->add_option("name", where_opts.name, "Registry name")->required();
    model_where->callback([&] {
        try {
            exit_code = vectra::cli::run_model_where(where_opts);
        } catch (const std::exception& e) {
            fmt::print(stderr, "error: {}\n", e.what());
            exit_code = 1;
        }
    });

    vectra::cli::ModelPullOptions pull_opts;
    auto* model_pull = model_cmd->add_subcommand("pull", "Download a model into the local cache");
    model_pull->add_option("name", pull_opts.name, "Registry name")->required();
    model_pull->add_flag(
        "--force", pull_opts.force, "Re-download even if the model is already cached");
    model_pull->callback([&] {
        try {
            exit_code = vectra::cli::run_model_pull(pull_opts);
        } catch (const std::exception& e) {
            fmt::print(stderr, "error: {}\n", e.what());
            exit_code = 1;
        }
    });
#endif

    CLI11_PARSE(app, argc, argv);
    return exit_code;
}
