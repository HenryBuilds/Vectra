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

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include "ask_command.hpp"
#include "index_command.hpp"
#include "search_command.hpp"
#include "serve_command.hpp"

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
#ifdef _WIN32
    // Make Windows consoles render UTF-8 correctly. Claude Code (and
    // our own diagnostics) emit UTF-8; without this, em-dashes and
    // smart quotes show up as CP-850/CP-1252 mojibake (e.g. "ÔÇö"
    // for "—"). Lifetime is the console window, so subsequent
    // commands in the same cmd session also benefit.
    SetConsoleOutputCP(CP_UTF8);
#endif

    // Bare-task shortcut. `vectra "<task>"` (or `vectra fix the bug`)
    // is rewritten to `vectra ask <task>` so users do not have to
    // type the verb for the common case. The rewrite happens before
    // CLI11 parses anything, so all of `ask`'s flags still work via
    // the canonical form. Anything that looks like a subcommand
    // (`index`, `search`, `ask`, `model`, `--help`, `-h`,
    // `--version`, `-V`) is left alone.
    static constexpr std::string_view kSubcommands[] = {"index", "search", "ask", "model", "serve"};
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
    index_cmd->add_option("--model",
                          index_opts.model,
                          "Embedding model name (e.g. qwen3-embed-0.6b). Skip for a symbol-only "
                          "index. Run `vectra model list` for available models.");
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
    ask_cmd->add_option("--claude-model",
                        ask_opts.claude_model,
                        "Claude model passed as `--model` to `claude -p` "
                        "(e.g. sonnet, opus, claude-opus-4-5)");
    ask_cmd->add_option("--effort",
                        ask_opts.claude_effort,
                        "Thinking budget passed as `--effort` to `claude -p` "
                        "(low / medium / high)");
    ask_cmd
        ->add_option("--permission-mode",
                     ask_opts.claude_permission_mode,
                     "Permission mode passed to `claude -p`: default (ask before "
                     "each edit), acceptEdits (auto-accept file edits), plan "
                     "(explore only, no edits), bypassPermissions (skip every "
                     "approval). Empty = wrapper picks acceptEdits.")
        ->check(CLI::IsMember({"default", "acceptEdits", "plan", "bypassPermissions"}));
    ask_cmd->add_option("--claude-arg",
                        ask_opts.claude_extra_args,
                        "Extra flag passed through to `claude -p` (repeatable)");
    auto* session_id_opt =
        ask_cmd->add_option("--session-id",
                            ask_opts.session_id,
                            "UUID forwarded as `claude -p --session-id <uuid>`. "
                            "Use for the first turn of a multi-turn conversation.");
    auto* resume_opt =
        ask_cmd->add_option("--resume",
                            ask_opts.resume_session,
                            "UUID forwarded as `claude -p --resume <uuid>` to continue "
                            "an existing claude session. Use for follow-up turns.");
    session_id_opt->excludes(resume_opt);
    resume_opt->excludes(session_id_opt);
    ask_cmd->add_flag("--print-prompt",
                      ask_opts.print_prompt,
                      "Print the composed prompt and exit (no claude spawn)");
    ask_cmd->add_flag("--stream-json",
                      ask_opts.stream_json,
                      "Emit claude's response as newline-delimited JSON events "
                      "(partial messages, tool_use, tool_result, usage). For UI "
                      "clients; humans want the default text output.");
    ask_cmd->add_flag(
        "--quiet", ask_opts.quiet, "Suppress per-stage retrieval timing output on stderr");
    ask_cmd->add_option("--daemon-url",
                        ask_opts.daemon_url,
                        "POST retrieval to a running `vectra serve` instead of running it "
                        "in-process. Skips the embedder cold-start on every call. "
                        "Example: --daemon-url http://127.0.0.1:7777");
    ask_cmd->callback([&] {
        try {
            exit_code = vectra::cli::run_ask(ask_opts);
        } catch (const std::exception& e) {
            fmt::print(stderr, "error: {}\n", e.what());
            exit_code = 1;
        }
    });

    // ---- serve -----------------------------------------------------------
    // Long-running retrieval daemon — holds the index, embedder, and
    // optional reranker in RAM and answers `POST /retrieve` against
    // them. Pair with `vectra ask --daemon-url …` to skip the model-
    // load cost on every call.
    auto* serve_cmd =
        app.add_subcommand("serve", "Run the local retrieval daemon (warm embedder, fast queries)");

    vectra::cli::ServeOptions serve_opts;
    serve_cmd->add_option("--root", serve_opts.repo_root, "Project root (default: walk up from CWD)")
        ->check(CLI::ExistingDirectory);
    serve_cmd->add_option("--db", serve_opts.db, "Index DB (default: <root>/.vectra/index.db)");
    serve_cmd->add_option(
        "--model", serve_opts.model, "Embedding model name (skip for symbol-only retrieval)");
    serve_cmd->add_option("--reranker",
                          serve_opts.reranker,
                          "Cross-encoder reranker model name (e.g. qwen3-rerank-0.6b)");
    serve_cmd->add_option("--port", serve_opts.port, "Localhost port to bind")->default_val(7777);
    serve_cmd->add_option(
        "--bind", serve_opts.bind_host, "Bind address (default: 127.0.0.1, localhost-only)");
    serve_cmd->add_option(
        "-k,--top-k", serve_opts.default_k, "Default top-K when a request omits `k`");
    serve_cmd->add_flag("-q,--quiet", serve_opts.quiet, "Suppress per-request log lines");
    serve_cmd->callback([&] {
        try {
            exit_code = vectra::cli::run_serve(serve_opts);
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
