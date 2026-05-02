// Copyright 2026 Vectra Contributors. Apache-2.0.

#include "ask_command.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <chrono>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string_view>
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
#include "project_config.hpp"

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

// Emit the stream-json `vectra_event/context` line on stdout
// before spawning claude, so a UI client parsing the stream can
// pre-render a Sources footer while claude is still composing.
// The pure formatter lives in claude_subprocess.cpp where it is
// reachable from the test harness.
void emit_context_event(const std::vector<ContextChunk>& chunks) {
    const auto line = format_context_event(chunks);
    std::fputs(line.c_str(), stdout);
    std::fflush(stdout);
}

}  // namespace

namespace {

[[nodiscard]] std::string join_task(const std::vector<std::string>& words) {
    std::string out;
    for (std::size_t i = 0; i < words.size(); ++i) {
        if (i > 0) {
            out += ' ';
        }
        out += words[i];
    }
    return out;
}

}  // namespace

int run_ask(const AskOptions& opts) {
    const std::string task = join_task(opts.task_words);
    if (task.empty()) {
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

    // ---- project defaults -------------------------------------------
    // .vectra/config.toml supplies fallbacks for any flag the user
    // did not pass. CLI flags always win.
    ProjectConfig project_cfg;
    try {
        project_cfg = load_project_config(repo_root);
    } catch (const std::exception& e) {
        fmt::print(stderr, "error: {}\n", e.what());
        return 1;
    }

    AskOptions resolved = opts;
    if (resolved.model.empty()) {
        resolved.model = project_cfg.model;
    }
    if (resolved.reranker.empty()) {
        resolved.reranker = project_cfg.reranker;
    }
    if (resolved.k == 0) {
        resolved.k = project_cfg.top_k != 0 ? project_cfg.top_k : std::size_t{8};
    }
    if (resolved.claude_binary.empty()) {
        resolved.claude_binary = project_cfg.claude_binary;
    }
    if (resolved.claude_model.empty()) {
        resolved.claude_model = project_cfg.claude_model;
    }
    if (resolved.claude_effort.empty()) {
        resolved.claude_effort = project_cfg.claude_effort;
    }
    if (resolved.claude_permission_mode.empty()) {
        resolved.claude_permission_mode = project_cfg.claude_permission_mode;
    }
    if (resolved.claude_extra_args.empty()) {
        resolved.claude_extra_args = project_cfg.claude_extra_args;
    }

    // ---- index -------------------------------------------------------
    const auto db_path = resolve_db(repo_root, resolved.db);
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
    if (!resolved.model.empty()) {
        const auto* entry = embed::ModelRegistry::by_name(resolved.model);
        if (entry == nullptr) {
            fmt::print(
                stderr, "error: unknown model '{}'. Try `vectra model list`.\n", resolved.model);
            return 2;
        }
        const auto model_path = embed::ModelRegistry::local_path(*entry);
        if (!fs::exists(model_path)) {
            fmt::print(stderr,
                       "error: model not cached. Run `vectra model pull {}` first.\n",
                       resolved.model);
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
    if (!resolved.reranker.empty()) {
        const auto* entry = embed::ModelRegistry::by_name(resolved.reranker);
        if (entry == nullptr) {
            fmt::print(stderr,
                       "error: unknown reranker '{}'. Try `vectra model list`.\n",
                       resolved.reranker);
            return 2;
        }
        const auto model_path = embed::ModelRegistry::local_path(*entry);
        if (!fs::exists(model_path)) {
            fmt::print(stderr,
                       "error: reranker not cached. Run `vectra model pull {}` first.\n",
                       resolved.reranker);
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
    if (!resolved.model.empty() || !resolved.reranker.empty()) {
        fmt::print(stderr,
                   "error: this build was produced with VECTRA_BUILD_EMBED=OFF; "
                   "the --model and --reranker flags are unavailable.\n");
        return 2;
    }
    fmt::print(stderr, "model:   (build without embed support — symbol-only)\n");
#endif

    // ---- retrieve ----------------------------------------------------
    retrieve::RetrieveOptions r_opts;
    r_opts.k = resolved.k;
    if (!resolved.quiet) {
        // Per-stage timing surfaces where wall-clock time goes
        // (model load happens before this, but everything inside
        // the retriever is covered).
        r_opts.on_stage =
            [](std::string_view name, std::size_t count, std::chrono::milliseconds dur) {
                fmt::print(stderr,
                           "  [{:>5} ms] {:<25} ({} {})\n",
                           dur.count(),
                           name,
                           count,
                           count == 1 ? "item" : "items");
            };
        fmt::print(stderr, "retrieval pipeline:\n");
    }
    const auto retrieve_start = std::chrono::steady_clock::now();
    const auto hits = retriever.retrieve(task, r_opts);
    const auto retrieve_total = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - retrieve_start);

    PromptComposition comp;
    comp.task = task;
    comp.context.reserve(hits.size());
    for (const auto& h : hits) {
        comp.context.push_back(to_chunk(h));
    }

    fmt::print(stderr,
               "context: {} chunk{} retrieved [total {} ms]\n",
               comp.context.size(),
               comp.context.size() == 1 ? "" : "s",
               retrieve_total.count());

    const auto prompt = compose_prompt(comp);

    if (resolved.print_prompt) {
        std::cout << prompt;
        return 0;
    }

    if (resolved.stream_json) {
        // Hand the Top-K chunks to the UI client before claude
        // starts streaming. The webview pre-renders the Sources
        // footer; the user can already click into a file while
        // claude is still composing the answer.
        emit_context_event(comp.context);
    }

    // ---- dispatch to claude ------------------------------------------
    fmt::print(stderr, "\n--- claude ---\n");

    TempFile tmp("vectra-ask");
    tmp.write(prompt);

    ClaudeInvocation inv;
    inv.prompt_file = tmp.path();
    if (!resolved.claude_binary.empty()) {
        inv.claude_binary = resolved.claude_binary;
    }
    // Order: model + effort first (so users can override via
    // claude_extra_args if they really want to), then session
    // continuity, then the rest.
    if (!resolved.claude_model.empty()) {
        inv.extra_args.push_back("--model");
        inv.extra_args.push_back(resolved.claude_model);
    }
    if (!resolved.claude_effort.empty()) {
        inv.extra_args.push_back("--effort");
        inv.extra_args.push_back(resolved.claude_effort);
    }
    // Mutual exclusion is enforced at the CLI parse layer; we just
    // forward whichever one the caller filled in.
    if (!resolved.session_id.empty()) {
        inv.extra_args.push_back("--session-id");
        inv.extra_args.push_back(resolved.session_id);
    } else if (!resolved.resume_session.empty()) {
        inv.extra_args.push_back("--resume");
        inv.extra_args.push_back(resolved.resume_session);
    }
    if (resolved.stream_json) {
        // claude requires all three together: --output-format=stream-json
        // sets the wire format, --include-partial-messages turns on
        // per-token deltas (otherwise we'd only get one event per full
        // content block), and --verbose is needed in --print mode for
        // claude to actually emit the stream events instead of
        // suppressing them as "internal".
        inv.extra_args.push_back("--output-format=stream-json");
        inv.extra_args.push_back("--include-partial-messages");
        inv.extra_args.push_back("--verbose");
    }

    // Decide what `--permission-mode` to forward. Precedence:
    //   1. explicit --permission-mode / --dangerously-skip-permissions
    //      passed through --claude-arg — caller knows what they want
    //   2. the resolved value (CLI flag / project config / extension
    //      setting) when non-empty
    //   3. fall back to "acceptEdits" so file modifications go through
    //      without hanging on a permission prompt that has no UI to
    //      answer in `claude -p`.
    // acceptEdits auto-accepts Edit / Write and common filesystem
    // commands (mkdir, touch, mv, cp, …) within the working tree;
    // Bash and MCP tools still require explicit approval.
    const bool user_set_permission_mode_via_extra = std::any_of(
        resolved.claude_extra_args.begin(), resolved.claude_extra_args.end(), [](const auto& a) {
            return a == "--permission-mode" || a == "--dangerously-skip-permissions" ||
                   a.starts_with("--permission-mode=");
        });
    if (!user_set_permission_mode_via_extra) {
        const std::string mode = resolved.claude_permission_mode.empty()
                                     ? "acceptEdits"
                                     : resolved.claude_permission_mode;
        inv.extra_args.push_back("--permission-mode");
        inv.extra_args.push_back(mode);
    }

    for (const auto& a : resolved.claude_extra_args) {
        inv.extra_args.push_back(a);
    }

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
