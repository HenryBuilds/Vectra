// Copyright 2026 Vectra Contributors. Apache-2.0.
//
// `vectra model` subcommand family.
//
//   vectra model list              — show built-in registry
//   vectra model where <name>      — print local cache path
//   vectra model pull <name>       — download into the cache
//
// Internal to vectra-cli; only compiled when VECTRA_BUILD_EMBED is on.

#pragma once

#include <string>

namespace vectra::cli {

struct ModelPullOptions {
    std::string name;    // canonical registry name
    bool force = false;  // re-download even if local file exists
};

struct ModelWhereOptions {
    std::string name;
};

[[nodiscard]] int run_model_list();
[[nodiscard]] int run_model_where(const ModelWhereOptions& opts);
[[nodiscard]] int run_model_pull(const ModelPullOptions& opts);

}  // namespace vectra::cli
