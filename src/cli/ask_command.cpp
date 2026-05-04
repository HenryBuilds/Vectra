// Copyright 2026 Vectra Contributors. Apache-2.0.

#include "ask_command.hpp"

#include <fmt/format.h>

// httplib + nlohmann::json power the daemon client path. They are
// only included when --daemon-url is set, so the include cost shows
// up here but the runtime cost is gated on the flag.
#include <httplib.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
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

// Split a `--daemon-url` value like `http://127.0.0.1:7777` (with
// optional trailing slash) into the (scheme+host:port) base that
// httplib::Client accepts. We do not support https for now — the
// daemon binds to 127.0.0.1 only, so plaintext is fine.
struct DaemonEndpoint {
    std::string base;  // e.g. "http://127.0.0.1:7777"
};

[[nodiscard]] DaemonEndpoint parse_daemon_url(std::string_view url) {
    DaemonEndpoint ep;
    std::string s{url};
    while (!s.empty() && s.back() == '/') s.pop_back();
    if (s.empty() || (s.find("http://") != 0 && s.find("https://") != 0)) {
        throw std::runtime_error(
            fmt::format("--daemon-url must be a full http://host:port (got '{}')", url));
    }
    ep.base = std::move(s);
    return ep;
}

// Look for a `.vectra/daemon.json` PID file in the workspace and,
// if a daemon is reachable on the port it advertises, return its
// endpoint. Returns an empty optional on any of:
//   - file missing / unreadable / malformed
//   - daemon's /health does not return 200 within ~1 s
// The caller then falls back to in-process retrieval.
[[nodiscard]] std::optional<DaemonEndpoint> try_discover_daemon(const fs::path& repo_root) {
    const auto pid_path = repo_root / ".vectra" / "daemon.json";
    std::error_code ec;
    if (!fs::exists(pid_path, ec)) return std::nullopt;
    std::ifstream in(pid_path, std::ios::binary);
    if (!in) return std::nullopt;
    std::string body((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    nlohmann::json meta;
    try {
        meta = nlohmann::json::parse(body);
    } catch (const std::exception&) {
        return std::nullopt;
    }
    const int port = meta.value("port", 0);
    const std::string bind = meta.value("bind", "127.0.0.1");
    if (port <= 0) return std::nullopt;

    DaemonEndpoint ep;
    ep.base = fmt::format("http://{}:{}", bind, port);

    httplib::Client cli(ep.base);
    cli.set_connection_timeout(0, 500'000);  // 500 ms
    cli.set_read_timeout(1, 0);              // 1 s
    auto res = cli.Get("/health");
    if (!res || res->status != 200) return std::nullopt;
    return ep;
}

// POST /retrieve at the daemon, parse the response into ContextChunks.
// Throws on transport failure or schema mismatch — caller turns the
// exception into a friendly error.
[[nodiscard]] std::vector<ContextChunk> fetch_from_daemon(const DaemonEndpoint& ep,
                                                          const std::string& task,
                                                          std::size_t k,
                                                          std::int64_t& took_ms_out) {
    httplib::Client cli(ep.base);
    cli.set_connection_timeout(2, 0);  // 2s — daemon is local; longer = it's wedged
    cli.set_read_timeout(60, 0);       // generous for slow embedders on big repos

    nlohmann::json req;
    req["task"] = task;
    if (k > 0) req["k"] = k;

    auto res = cli.Post("/retrieve", req.dump(), "application/json");
    if (!res) {
        throw std::runtime_error(
            fmt::format("could not reach daemon at {} ({}) — is `vectra serve` running?",
                        ep.base,
                        httplib::to_string(res.error())));
    }
    if (res->status != 200) {
        throw std::runtime_error(
            fmt::format("daemon returned {}: {}", res->status, res->body));
    }

    nlohmann::json body;
    try {
        body = nlohmann::json::parse(res->body);
    } catch (const std::exception& e) {
        throw std::runtime_error(
            fmt::format("daemon response was not valid JSON: {}", e.what()));
    }

    if (!body.contains("chunks") || !body["chunks"].is_array()) {
        throw std::runtime_error("daemon response missing 'chunks' array");
    }
    took_ms_out = body.value("took_ms", static_cast<std::int64_t>(0));

    std::vector<ContextChunk> out;
    out.reserve(body["chunks"].size());
    for (const auto& c : body["chunks"]) {
        ContextChunk cc;
        cc.file_path = c.value("file", "");
        cc.start_line = c.value("start_line", 0);
        cc.end_line = c.value("end_line", 0);
        cc.symbol = c.value("symbol", "");
        cc.kind = c.value("kind", "");
        cc.text = c.value("text", "");
        out.push_back(std::move(cc));
    }
    return out;
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

    fmt::print(stderr, "project: {}\n", repo_root.string());

    // ---- daemon shortcut ---------------------------------------------
    // When --daemon-url is set we skip the store/embedder/reranker
    // setup entirely and POST the retrieval to a long-running
    // `vectra serve`. The daemon owns the index file; we just
    // forward the task and consume the chunks. Everything below the
    // retrieval phase (prompt composition, claude spawn) is shared.
    PromptComposition comp;
    comp.task = task;
    // Plan-mode is read-only by definition — claude can't edit, so
    // the SCOPE / TOOL-ORDER invariants would just burn ~250 tokens
    // per call for nothing. Skip them in that mode; keep them on
    // for the edit modes where they actually steer claude.
    comp.include_edit_invariants = (resolved.claude_permission_mode != "plan");

    // Resolve the daemon endpoint:
    //   1. explicit --daemon-url wins
    //   2. otherwise try `<repo_root>/.vectra/daemon.json` and health-check
    //   3. otherwise fall back to in-process retrieval
    std::optional<DaemonEndpoint> daemon_ep;
    if (!resolved.daemon_url.empty()) {
        daemon_ep = parse_daemon_url(resolved.daemon_url);
    } else {
        daemon_ep = try_discover_daemon(repo_root);
    }

    if (daemon_ep.has_value()) {
        fmt::print(stderr, "daemon:  {} (no in-process index/embedder)\n", daemon_ep->base);
        std::int64_t took_ms = 0;
        auto chunks = fetch_from_daemon(*daemon_ep, task, resolved.k, took_ms);
        fmt::print(stderr,
                   "context: {} chunk{} retrieved [daemon {} ms]\n",
                   chunks.size(),
                   chunks.size() == 1 ? "" : "s",
                   took_ms);
        comp.context = std::move(chunks);
    } else {
        // ---- index ----
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

        // Vector search is only used when an embedding model is set;
        // skipping the in-memory HNSW rebuild on a hybrid-indexed DB
        // saves multiple minutes of startup on every symbol-only ask.
        store::Store::OpenOptions store_opts;
        store_opts.skip_vector_index = resolved.model.empty();
        auto store = store::Store::open(db_path, store_opts);
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
    // Trim the chunk count when the score gradient is steep
    // (rank-0 dominates → 1-2 chunks usually). On slam-dunk
    // queries this cuts claude's input tokens dramatically;
    // on broad queries the gradient is gentle and we still
    // return the full opts.k.
    r_opts.adaptive_k = true;
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

        comp.context.reserve(hits.size());
        for (const auto& h : hits) {
            comp.context.push_back(to_chunk(h));
        }

        fmt::print(stderr,
                   "context: {} chunk{} retrieved [total {} ms]\n",
                   comp.context.size(),
                   comp.context.size() == 1 ? "" : "s",
                   retrieve_total.count());
    }  // end in-process retrieval branch

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
