// Copyright 2026 Vectra Contributors. Apache-2.0.
//
// Per-project Vectra config loaded from <repo>/.vectra/config.toml.
//
// Lets a user write `vectra fix "..."` without typing --model and
// --reranker every time. CLI flags still take precedence; the
// config supplies defaults for the fields the user did not set.
//
// Example .vectra/config.toml:
//
//   [retrieve]
//   model     = "qwen3-embed-8b"
//   reranker  = "qwen3-rerank-0.6b"
//   top_k     = 12
//
//   [claude]
//   binary     = "claude"
//   extra_args = ["--model", "claude-opus-4-5"]
//
// All sections and keys are optional. A missing file is fine —
// it is the same as an empty config.

#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace vectra::cli {

struct ProjectConfig {
    // [retrieve]
    std::string model;
    std::string reranker;
    std::size_t top_k = 0;  // 0 means "not set"; caller picks a built-in default

    // [claude]
    std::string claude_binary;
    std::string claude_model;
    std::string claude_effort;
    std::string claude_permission_mode;
    std::vector<std::string> claude_extra_args;
};

// Read <repo>/.vectra/config.toml. Returns an empty config when the
// file is absent. Throws std::runtime_error on a parse error or a
// type mismatch (e.g. top_k stored as a string).
[[nodiscard]] ProjectConfig load_project_config(const std::filesystem::path& repo_root);

}  // namespace vectra::cli
