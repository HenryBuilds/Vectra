// Copyright 2026 Vectra Contributors. Apache-2.0.
//
// `vectra serve` — long-running retrieval daemon.
//
// Holds an open Store, an Embedder (when --model is set), and an
// optional Reranker in RAM, and answers `POST /retrieve` requests
// against them. The whole point is to amortise the cold-start cost
// of `vectra ask --model …`: every invocation today re-loads the
// GGUF embedder from disk (~1s) and embeds the query (~200ms) on
// top of the actual retrieval work, which dominates wall-clock for
// short queries.
//
// Scope of this MVP:
//   - localhost-only TCP socket (`127.0.0.1:<port>`)
//   - one workspace per daemon (set at startup)
//   - one endpoint: `POST /retrieve` with `{"task":"…","k":8}` →
//     `{"chunks":[…],"took_ms":42}`
//   - no auth (localhost binding is the gate); single user per host
//   - no auto-shutdown; user runs Ctrl-C to stop
//
// Out of scope (deferred):
//   - PID-file auto-discovery from `vectra ask`
//   - Bearer-token auth + multi-user
//   - Windows named pipes
//   - Multi-workspace routing
//   - Reranker hot-swap

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

namespace vectra::cli {

struct ServeOptions {
    std::filesystem::path repo_root;       // empty → walk up from CWD
    std::filesystem::path db;              // empty → <root>/.vectra/index.db
    std::string model;                     // embedding model (empty = symbol-only)
    std::string reranker;                  // optional cross-encoder
    std::string bind_host{"127.0.0.1"};    // localhost-only by default
    std::uint16_t port{7777};
    std::size_t default_k{8};
    bool quiet{false};                     // suppress per-request log
};

// Run the daemon until killed. Returns 0 on clean shutdown, non-
// zero on a setup error (missing index, invalid model, port in
// use, …). Blocks the current thread; httplib's listener does the
// blocking, so SIGINT / Ctrl-C is the supported stop signal.
int run_serve(const ServeOptions& opts);

}  // namespace vectra::cli
