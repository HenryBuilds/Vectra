// Copyright 2026 Vectra Contributors. Apache-2.0.

#include "fix_command.hpp"

#include <fmt/format.h>

#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <system_error>

#include "vectra/core/chunk.hpp"
#include "vectra/retrieve/retriever.hpp"
#include "vectra/store/store.hpp"

#if VECTRA_HAS_EMBED
#include "vectra/embed/embedder.hpp"
#include "vectra/embed/model_registry.hpp"
#include "vectra/embed/reranker.hpp"
#endif

#include "claude_subprocess.hpp"
#include "cli_paths.hpp"

namespace vectra::cli {

namespace {

namespace fs = std::filesystem;

// Resolve the index DB path. When the caller passed --db we use it
// verbatim; otherwise we land on <repo>/.vectra/index.db.
[[nodiscard]] fs::path resolve_db(const fs::path& repo_root, const fs::path& explicit_db) {
    if (!explicit_db.empty()) {
        return explicit_db;
    }
    return repo_root / ".vectra" / "index.db";
}

// Convert a retrieve::Hit into the ContextChunk shape the prompt
// composer accepts. Line numbers are converted from the
// 0-indexed-half-open convention the retriever uses to the
// 1-indexed-inclusive humans-and-LLMs prefer.
[[nodiscard]] ContextChunk to_chunk(const retrieve::Hit& hit) {
    ContextChunk c;
    c.file_path = hit.file_path;
    c.start_line = static_cast<int>(hit.start_row) + 1;
    c.end_line = static_cast<int>(hit.end_row) + 1;
    c.symbol = hit.symbol;
    c.kind = std::string{core::chunk_kind_name(hit.kind)};
    c.text = hit.text;
    return c;
}

}  // namespace

int run_fix(const FixOptions& opts) {
    if (opts.task.empty()) {
        fmt::print(stderr, "error: task is required\n");
        return 2;
    }

    // ---- repo root ---------------------------------------------------
    fs::path repo_root = opts.repo_root;
    if (repo_root.empty()) {
        if (auto found = find_project_root(fs::current_path()); found) {
            repo_root = std::move(*found);
        } else {
            fmt::print(stderr,
                       "error: no project root detected (looked for .vectra or .git).\n"
                       "       run from inside a project, or pass --root <path>.\n");
            return 1;
        }
    } else {
        std::error_code ec;
        if (!fs::is_directory(repo_root, ec)) {
            fmt::print(stderr, "error: --root is not a directory: {}\n", repo_root.string());
            return 1;
        }
    }

    // ---- index -------------------------------------------------------
    const auto db_path = resolve_db(repo_root, opts.db);
    {
        std::error_code ec;
        if (!fs::exists(db_path, ec)) {
            fmt::print(stderr,
                       "error: index not found at {}.\n"
                       "       run `vectra index <root>` first to create one.\n",
                       db_path.string());
            return 1;
        }
    }

    auto store = store::Store::open(db_path);
    fmt::print(stderr, "project: {}\n", repo_root.string());
    fmt::print(stderr, "index:   {}\n", db_path.string());

    retrieve::Retriever retriever(store);

#if VECTRA_HAS_EMBED
    std::unique_ptr<embed::Embedder> embedder;
    if (!opts.model.empty()) {
        const auto* entry = embed::ModelRegistry::by_name(opts.model);
        if (entry == nullptr) {
            fmt::print(stderr, "error: unknown model '{}'. Try `vectra model list`.\n", opts.model);
            return 2;
        }
        const auto model_path = embed::ModelRegistry::local_path(*entry);
        if (!fs::exists(model_path)) {
            fmt::print(
                stderr, "error: model not cached. Run `vectra model pull {}` first.\n", opts.model);
            return 2;
        }
        embed::EmbedderConfig cfg;
        cfg.model_path = model_path;
        cfg.model_id = entry->name;
        embedder = std::make_unique<embed::Embedder>(embed::Embedder::open(cfg));
        retriever.set_embedder(embedder.get());
        fmt::print(stderr, "model:   {} (dim {})\n", entry->name, embedder->dim());
    } else {
        fmt::print(stderr, "model:   (none — symbol-only retrieval)\n");
    }

    std::unique_ptr<embed::Reranker> reranker;
    if (!opts.reranker.empty()) {
        const auto* entry = embed::ModelRegistry::by_name(opts.reranker);
        if (entry == nullptr) {
            fmt::print(
                stderr, "error: unknown reranker '{}'. Try `vectra model list`.\n", opts.reranker);
            return 2;
        }
        const auto model_path = embed::ModelRegistry::local_path(*entry);
        if (!fs::exists(model_path)) {
            fmt::print(stderr,
                       "error: reranker not cached. Run `vectra model pull {}` first.\n",
                       opts.reranker);
            return 2;
        }
        embed::RerankerConfig cfg;
        cfg.model_path = model_path;
        cfg.model_id = entry->name;
        reranker = std::make_unique<embed::Reranker>(embed::Reranker::open(cfg));
        retriever.set_reranker(reranker.get());
        fmt::print(stderr, "reranker:{}\n", entry->name);
    }
#else
    if (!opts.model.empty() || !opts.reranker.empty()) {
        fmt::print(stderr,
                   "error: this build was produced with VECTRA_BUILD_EMBED=OFF; "
                   "the --model and --reranker flags are unavailable.\n");
        return 2;
    }
    fmt::print(stderr, "model:   (build without embed support — symbol-only)\n");
#endif

    // ---- retrieve ----------------------------------------------------
    retrieve::RetrieveOptions r_opts;
    r_opts.k = opts.k;
    const auto hits = retriever.retrieve(opts.task, r_opts);

    PromptComposition comp;
    comp.task = opts.task;
    comp.context.reserve(hits.size());
    for (const auto& h : hits) {
        comp.context.push_back(to_chunk(h));
    }

    fmt::print(stderr,
               "context: {} chunk{} retrieved\n",
               comp.context.size(),
               comp.context.size() == 1 ? "" : "s");

    const auto prompt = compose_prompt(comp);

    if (opts.print_prompt) {
        std::cout << prompt;
        return 0;
    }

    // ---- dispatch to claude ------------------------------------------
    fmt::print(stderr, "\n--- claude ---\n");

    TempFile tmp("vectra-fix");
    tmp.write(prompt);

    ClaudeInvocation inv;
    inv.prompt_file = tmp.path();
    if (!opts.claude_binary.empty()) {
        inv.claude_binary = opts.claude_binary;
    }
    inv.extra_args = opts.claude_extra_args;

    const int rc = run_claude(inv, std::cout);
    if (rc < 0) {
        fmt::print(stderr,
                   "error: could not spawn claude. Is the Claude Code CLI installed "
                   "and on your PATH? (npm i -g @anthropic-ai/claude-code)\n");
        return 1;
    }
    return rc;
}

}  // namespace vectra::cli
