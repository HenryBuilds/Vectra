// Copyright 2026 Vectra Contributors. Apache-2.0.

#include "project_config.hpp"

#include <fmt/format.h>

#include <stdexcept>
#include <system_error>
#include <toml++/toml.hpp>

namespace vectra::cli {

namespace {

namespace fs = std::filesystem;

[[nodiscard]] std::string read_string(const toml::table& tbl,
                                      std::string_view table_name,
                                      std::string_view key) {
    const auto* node = tbl.get(key);
    if (node == nullptr) {
        return {};
    }
    if (const auto* s = node->as_string()) {
        return s->get();
    }
    throw std::runtime_error(fmt::format("config: [{}].{} must be a string", table_name, key));
}

[[nodiscard]] std::vector<std::string> read_string_array(const toml::table& tbl,
                                                         std::string_view table_name,
                                                         std::string_view key) {
    std::vector<std::string> out;
    const auto* node = tbl.get(key);
    if (node == nullptr) {
        return out;
    }
    const auto* arr = node->as_array();
    if (arr == nullptr) {
        throw std::runtime_error(
            fmt::format("config: [{}].{} must be an array of strings", table_name, key));
    }
    out.reserve(arr->size());
    for (const auto& v : *arr) {
        const auto* s = v.as_string();
        if (s == nullptr) {
            throw std::runtime_error(
                fmt::format("config: [{}].{} contains a non-string entry", table_name, key));
        }
        out.emplace_back(s->get());
    }
    return out;
}

[[nodiscard]] std::size_t read_size(const toml::table& tbl,
                                    std::string_view table_name,
                                    std::string_view key) {
    const auto* node = tbl.get(key);
    if (node == nullptr) {
        return 0;
    }
    const auto* i = node->as_integer();
    if (i == nullptr) {
        throw std::runtime_error(
            fmt::format("config: [{}].{} must be a positive integer", table_name, key));
    }
    const auto v = i->get();
    if (v < 0) {
        throw std::runtime_error(
            fmt::format("config: [{}].{} must be >= 0 (got {})", table_name, key, v));
    }
    return static_cast<std::size_t>(v);
}

}  // namespace

ProjectConfig load_project_config(const fs::path& repo_root) {
    const auto path = repo_root / ".vectra" / "config.toml";
    {
        std::error_code ec;
        if (!fs::exists(path, ec)) {
            return {};
        }
    }

    toml::table doc;
    try {
        doc = toml::parse_file(path.string());
    } catch (const toml::parse_error& e) {
        throw std::runtime_error(
            fmt::format("config: parse error in {}: {}", path.string(), e.description()));
    }

    ProjectConfig out;

    if (const auto* retrieve = doc.get_as<toml::table>("retrieve")) {
        out.model = read_string(*retrieve, "retrieve", "model");
        out.reranker = read_string(*retrieve, "retrieve", "reranker");
        out.top_k = read_size(*retrieve, "retrieve", "top_k");
    }

    if (const auto* claude = doc.get_as<toml::table>("claude")) {
        out.claude_binary = read_string(*claude, "claude", "binary");
        out.claude_model = read_string(*claude, "claude", "model");
        out.claude_effort = read_string(*claude, "claude", "effort");
        out.claude_permission_mode = read_string(*claude, "claude", "permission_mode");
        out.claude_extra_args = read_string_array(*claude, "claude", "extra_args");
    }

    return out;
}

}  // namespace vectra::cli
