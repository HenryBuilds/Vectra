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
#include <cstring>
#include <exception>
#include <iostream>
#include <string_view>
#include <vector>

#include "ask_command.hpp"
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
    // Bare-task shortcut. `vectra "<task>"` (or `vectra fix the bug`)
    // is rewritten to `vectra ask <task>` so users do not have to
    // type the verb for the common case. The rewrite happens before
    // CLI11 parses anything, so all of `ask`'s flags still work via
    // the canonical form. Anything that looks like a subcommand
    // (`index`, `search`, `ask`, `model`, `--help`, `-h`,
    // `--version`, `-V`) is left alone.
    static constexpr std::string_view kSubcommands[] = {"index", "search", "ask", "model"};
    std::vector<char*> rewritten_argv;
    if (argc >= 2 && argv[1] != nullptr && argv[1][0] != '\0' && argv[1][0] != '-') {
        const std::string_view first{argv[1]};
        bool is_known = false;
        for (const auto& s : kSubcommands) {
            if (first == s) {
                is_known = true;
                break;
            }
        }
        if (!is_known) {
            rewritten_argv.reserve(static_cast<std::size_t>(argc) + 1);
            rewritten_argv.push_back(argv[0]);
            rewritten_argv.push_back(const_cast<char*>("ask"));
            for (int i = 1; i < argc; ++i) {
                rewritten_argv.push_back(argv[i]);
            }
            argc = static_cast<int>(rewritten_argv.size());
            argv = rewritten_argv.data();
        }
    }

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
    search_cmd->add_option("--reranker",
                           search_opts.reranker,
                           "Cross-encoder reranker model name (e.g. qwen3-rerank-0.6b)");
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

    // ---- ask -------------------------------------------------------------
    // RAG dispatch to Claude Code: retrieve top-K chunks for the task,
    // compose a prompt that wraps them as <context> blocks, and shell
    // out to `claude -p`. Vectra does not edit files itself — Claude
    // Code's tools own that. Bare `vectra "<task>"` (no subcommand)
    // is rewritten to `vectra ask <task>` in the argv preprocessor
    // below, so the verb is optional in the common case.
    auto* ask_cmd = app.add_subcommand("ask", "Hand a task to Claude Code with retrieved context");

    vectra::cli::AskOptions ask_opts;
    ask_cmd
        ->add_option(
            "task", ask_opts.task_words, "Task description (multiple words are joined with spaces)")
        ->required()
        ->expected(1, -1);
    ask_cmd->add_option("--root", ask_opts.repo_root, "Project root (default: walk up from CWD)")
        ->check(CLI::ExistingDirectory);
    ask_cmd->add_option("--db", ask_opts.db, "Index DB (default: <root>/.vectra/index.db)");
    ask_cmd->add_option(
        "-k,--top-k",
        ask_opts.k,
        "Number of context chunks to surface (default: 8, override in .vectra/config.toml)");
    ask_cmd->add_option(
        "--model", ask_opts.model, "Embedding model name (skip for symbol-only retrieval)");
    ask_cmd->add_option("--reranker",
                        ask_opts.reranker,
                        "Cross-encoder reranker model name (e.g. qwen3-rerank-0.6b)");
    ask_cmd->add_option("--claude-bin",
                        ask_opts.claude_binary,
                        "Override the claude binary (default: PATH lookup)");
    ask_cmd->add_option("--claude-arg",
                        ask_opts.claude_extra_args,
                        "Extra flag passed through to `claude -p` (repeatable)");
    ask_cmd->add_flag("--print-prompt",
                      ask_opts.print_prompt,
                      "Print the composed prompt and exit (no claude spawn)");
    ask_cmd->callback([&] {
        try {
            exit_code = vectra::cli::run_ask(ask_opts);
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
