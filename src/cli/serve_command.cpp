// Copyright 2026 Vectra Contributors. Apache-2.0.

#include "serve_command.hpp"

// httplib ships as a single 10k-line header that pulls in winsock /
// posix socket headers. We isolate the include here and turn on the
// "header-only" mode (default) so no separate library has to be
// linked. The Windows socket warnings that leak through `/WX` are
// silenced via the same wd-list the rest of the binary uses.
#include <fmt/format.h>
#include <httplib.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <process.h>
#include <windows.h>
#define VECTRA_GETPID() static_cast<long long>(GetCurrentProcessId())
#else
#include <unistd.h>
#define VECTRA_GETPID() static_cast<long long>(getpid())
#endif

#include "vectra/core/chunk.hpp"
#include "vectra/retrieve/retriever.hpp"
#include "vectra/store/store.hpp"

#if VECTRA_HAS_EMBED
#include "vectra/embed/embedder.hpp"
#include "vectra/embed/model_registry.hpp"
#include "vectra/embed/reranker.hpp"
#endif

#include "claude_subprocess.hpp"  // for json_escape (we already hand-emit JSON elsewhere)
#include "cli_paths.hpp"
#include "project_config.hpp"

namespace vectra::cli {

namespace {

namespace fs = std::filesystem;

// Hand-written JSON request parser. The request shape is fixed —
// `{"task": "<string>", "k": <number, optional>}` — so we do not
// need a full JSON dependency for the MVP. Returns false when the
// body cannot be parsed; the caller then 400s.
bool parse_retrieve_request(const std::string& body, std::string& task_out, std::size_t& k_out) {
    // Find "task" key.
    const auto task_key = body.find("\"task\"");
    if (task_key == std::string::npos)
        return false;
    auto colon = body.find(':', task_key);
    if (colon == std::string::npos)
        return false;
    auto quote = body.find('"', colon);
    if (quote == std::string::npos)
        return false;
    std::string s;
    bool escape = false;
    for (std::size_t i = quote + 1; i < body.size(); ++i) {
        const char c = body[i];
        if (escape) {
            switch (c) {
                case 'n':
                    s += '\n';
                    break;
                case 't':
                    s += '\t';
                    break;
                case 'r':
                    s += '\r';
                    break;
                case '"':
                    s += '"';
                    break;
                case '\\':
                    s += '\\';
                    break;
                default:
                    s += c;
                    break;
            }
            escape = false;
            continue;
        }
        if (c == '\\') {
            escape = true;
            continue;
        }
        if (c == '"') {
            task_out = std::move(s);
            // Optionally pull `k`.
            const auto k_key = body.find("\"k\"");
            if (k_key != std::string::npos) {
                auto k_colon = body.find(':', k_key);
                if (k_colon != std::string::npos) {
                    std::size_t pos = k_colon + 1;
                    while (pos < body.size() && (body[pos] == ' ' || body[pos] == '\t'))
                        ++pos;
                    std::size_t v = 0;
                    bool any = false;
                    while (pos < body.size() && body[pos] >= '0' && body[pos] <= '9') {
                        v = v * 10 + static_cast<std::size_t>(body[pos] - '0');
                        ++pos;
                        any = true;
                    }
                    if (any)
                        k_out = v;
                }
            }
            return true;
        }
        s += c;
    }
    return false;
}

std::string serialize_chunks(const std::vector<retrieve::Hit>& hits, std::int64_t took_ms) {
    std::string out;
    out += R"({"chunks":[)";
    for (std::size_t i = 0; i < hits.size(); ++i) {
        if (i > 0)
            out += ',';
        const auto& h = hits[i];
        out += fmt::format(
            R"({{"file":"{}","start_line":{},"end_line":{},"symbol":"{}","kind":"{}","text":"{}","score":{:.4f}}})",
            json_escape(h.file_path),
            static_cast<int>(h.start_row) + 1,
            static_cast<int>(h.end_row) + 1,
            json_escape(h.symbol),
            json_escape(std::string{core::chunk_kind_name(h.kind)}),
            json_escape(h.text),
            h.score);
    }
    out += R"(],"took_ms":)";
    out += std::to_string(took_ms);
    out += '}';
    return out;
}

}  // namespace

int run_serve(const ServeOptions& opts) {
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
    }

    // Project config supplies fallbacks for --model / --reranker so
    // the daemon defaults match `vectra ask` defaults from the same
    // workspace.
    ProjectConfig project_cfg;
    try {
        project_cfg = load_project_config(repo_root);
    } catch (const std::exception& e) {
        fmt::print(stderr, "error: {}\n", e.what());
        return 1;
    }
    ServeOptions resolved = opts;
    if (resolved.model.empty())
        resolved.model = project_cfg.model;
    if (resolved.reranker.empty())
        resolved.reranker = project_cfg.reranker;
    if (resolved.default_k == 0) {
        resolved.default_k = project_cfg.top_k != 0 ? project_cfg.top_k : std::size_t{8};
    }

    fs::path db_path = resolved.db.empty() ? repo_root / ".vectra" / "index.db" : resolved.db;
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

    // Store: only build the in-memory HNSW when we'll actually use it
    // (i.e. when --model is set). Same opt-in as ask_command.
    store::Store::OpenOptions store_opts;
    store_opts.skip_vector_index = resolved.model.empty();
    auto store = store::Store::open(db_path, store_opts);

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
    }
#else
    if (!resolved.model.empty() || !resolved.reranker.empty()) {
        fmt::print(stderr,
                   "error: this build was produced with VECTRA_BUILD_EMBED=OFF; "
                   "the --model and --reranker flags are unavailable.\n");
        return 2;
    }
#endif

    fmt::print(stderr, "vectra serve · workspace: {}\n", repo_root.string());
    fmt::print(stderr, "             · index:     {}\n", db_path.string());
    fmt::print(stderr,
               "             · model:     {}\n",
               resolved.model.empty() ? "(symbol-only)" : resolved.model);
    if (!resolved.reranker.empty()) {
        fmt::print(stderr, "             · reranker:  {}\n", resolved.reranker);
    }

    httplib::Server server;

    server.Get("/health", [&](const httplib::Request&, httplib::Response& res) {
        res.set_content(R"({"status":"ok","model":")" + resolved.model + R"("})",
                        "application/json");
    });

    server.Post("/retrieve", [&](const httplib::Request& req, httplib::Response& res) {
        const auto t0 = std::chrono::steady_clock::now();
        std::string task;
        std::size_t k = resolved.default_k;
        if (!parse_retrieve_request(req.body, task, k)) {
            res.status = 400;
            res.set_content(R"({"error":"malformed request body"})", "application/json");
            return;
        }
        if (task.empty()) {
            res.status = 400;
            res.set_content(R"({"error":"task is empty"})", "application/json");
            return;
        }
        if (k == 0)
            k = resolved.default_k;

        try {
            retrieve::RetrieveOptions r_opts;
            r_opts.k = k;
            r_opts.adaptive_k = true;  // same trim behaviour as `vectra ask`
            const auto hits = retriever.retrieve(task, r_opts);
            const auto took = std::chrono::duration_cast<std::chrono::milliseconds>(
                                  std::chrono::steady_clock::now() - t0)
                                  .count();
            const auto body = serialize_chunks(hits, static_cast<std::int64_t>(took));
            res.set_content(body, "application/json");
            if (!resolved.quiet) {
                fmt::print(stderr,
                           "  POST /retrieve  k={}  task={}…  hits={}  [{} ms]\n",
                           k,
                           task.substr(0, 60),
                           hits.size(),
                           took);
            }
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(
                fmt::format(R"({{"error":"retrieve failed: {}"}})", json_escape(e.what())),
                "application/json");
        }
    });

    // Write a PID file so `vectra ask` (and the VS Code extension)
    // can auto-discover the daemon without the user passing
    // --daemon-url every time. Best-effort: a crashed daemon leaves
    // the file behind, so consumers must health-check the URL
    // before trusting it. Cleaned up on graceful shutdown via the
    // SIGINT handler below.
    const fs::path daemon_meta_path = repo_root / ".vectra" / "daemon.json";
    {
        std::error_code ec;
        fs::create_directories(daemon_meta_path.parent_path(), ec);
        std::ofstream out(daemon_meta_path, std::ios::binary | std::ios::trunc);
        if (out) {
            out << fmt::format(R"({{"port":{},"pid":{},"bind":"{}","model":"{}","reranker":"{}"}})",
                               resolved.port,
                               VECTRA_GETPID(),
                               json_escape(resolved.bind_host),
                               json_escape(resolved.model),
                               json_escape(resolved.reranker));
        }
    }
    auto cleanup_pidfile = [&] {
        std::error_code ec;
        fs::remove(daemon_meta_path, ec);
    };

    // Catch Ctrl-C so the listen loop returns, server.listen()
    // returns false-or-true cleanly, and we can remove the PID
    // file before exiting. Without this the file would always be
    // left stale even on an intended shutdown. We stash the server
    // pointer in a static so the C-style signal handler can reach
    // it; a single daemon per process is the only mode supported.
    static httplib::Server* g_server_for_signal = nullptr;
    g_server_for_signal = &server;
    std::signal(SIGINT, [](int) {
        if (g_server_for_signal != nullptr) {
            g_server_for_signal->stop();
        }
    });
#ifdef SIGTERM
    std::signal(SIGTERM, [](int) {
        if (g_server_for_signal != nullptr) {
            g_server_for_signal->stop();
        }
    });
#endif

    fmt::print(stderr, "             · pidfile:   {}\n", daemon_meta_path.string());
    fmt::print(stderr,
               "             · listening on http://{}:{}/ (Ctrl-C to stop)\n",
               resolved.bind_host,
               resolved.port);

    const bool ok = server.listen(resolved.bind_host, static_cast<int>(resolved.port));
    cleanup_pidfile();
    if (!ok) {
        fmt::print(stderr, "error: could not bind {}:{}\n", resolved.bind_host, resolved.port);
        return 1;
    }
    return 0;
}

}  // namespace vectra::cli
