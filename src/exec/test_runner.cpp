// Copyright 2026 Vectra Contributors. Apache-2.0.

#include "vectra/exec/test_runner.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <cstdio>
#include <regex>
#include <stdexcept>
#include <toml++/toml.hpp>
#include <utility>

#ifdef _WIN32
#include <io.h>
#define VECTRA_POPEN _popen
#define VECTRA_PCLOSE _pclose
#else
#include <sys/wait.h>
#define VECTRA_POPEN popen
#define VECTRA_PCLOSE pclose
#endif

namespace vectra::exec {

namespace {

[[nodiscard]] std::string replace_all(std::string s, std::string_view from, std::string_view to) {
    if (from.empty()) {
        return s;
    }
    std::size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
    return s;
}

[[nodiscard]] std::string substitute(std::string_view token,
                                     const std::filesystem::path& project_root,
                                     const std::filesystem::path& build_dir) {
    std::string out{token};
    out = replace_all(std::move(out), "{project_root}", project_root.string());
    out = replace_all(std::move(out), "{src_dir}", project_root.string());
    out = replace_all(std::move(out), "{build_dir}", build_dir.string());
    return out;
}

// Quote a token for cmd.exe / /bin/sh. We surround with double-
// quotes when the token contains whitespace; otherwise leave it
// bare. Sufficient for our inputs (filesystem paths and adapter
// flag strings without embedded quotes or shell metacharacters).
[[nodiscard]] std::string shell_quote(std::string_view tok) {
    if (tok.find_first_of(" \t") == std::string_view::npos) {
        return std::string{tok};
    }
    return fmt::format("\"{}\"", tok);
}

[[nodiscard]] std::string build_shell_command(std::span<const std::string> argv,
                                              const std::filesystem::path& cwd) {
    std::string cmd;
#ifdef _WIN32
    cmd += fmt::format("cd /d \"{}\" && ", cwd.string());
#else
    cmd += fmt::format("cd \"{}\" && ", cwd.string());
#endif
    for (std::size_t i = 0; i < argv.size(); ++i) {
        if (i > 0) {
            cmd += ' ';
        }
        cmd += shell_quote(argv[i]);
    }
    cmd += " 2>&1";
    return cmd;
}

[[nodiscard]] int normalize_status(int popen_status) noexcept {
#ifdef _WIN32
    return popen_status;
#else
    if (WIFEXITED(popen_status)) {
        return WEXITSTATUS(popen_status);
    }
    return -1;  // crashed or signaled
#endif
}

struct RawResult {
    int exit_code = 0;
    std::string output;
};

[[nodiscard]] RawResult run_shell(const std::string& cmd) {
    FILE* pipe = VECTRA_POPEN(cmd.c_str(), "r");
    if (pipe == nullptr) {
        throw std::runtime_error(fmt::format("popen failed for command: {}", cmd));
    }
    std::string output;
    char buffer[4096];
    while (true) {
        const auto n = std::fread(buffer, 1, sizeof(buffer), pipe);
        if (n == 0) {
            break;
        }
        output.append(buffer, n);
    }
    const int status = VECTRA_PCLOSE(pipe);
    return {normalize_status(status), std::move(output)};
}

[[nodiscard]] std::vector<std::string> read_string_array(const toml::table& tbl,
                                                         std::string_view key) {
    std::vector<std::string> out;
    if (const auto* arr = tbl.get_as<toml::array>(key)) {
        out.reserve(arr->size());
        for (const auto& v : *arr) {
            if (const auto* s = v.as_string()) {
                out.emplace_back(s->get());
            }
        }
    }
    return out;
}

[[nodiscard]] TestAdapter parse_adapter(const std::filesystem::path& path) {
    toml::table tbl;
    try {
        tbl = toml::parse_file(path.string());
    } catch (const toml::parse_error& e) {
        throw std::runtime_error(
            fmt::format("failed to parse adapter manifest {}: {}", path.string(), e.what()));
    }

    TestAdapter a;
    a.name = tbl["name"].value_or<std::string>("");
    a.description = tbl["description"].value_or<std::string>("");
    a.detect_files = read_string_array(tbl, "detect_files");
    a.priority = tbl["priority"].value_or<int>(0);
    a.languages = read_string_array(tbl, "languages");
    a.configure_command = read_string_array(tbl, "configure_command");
    a.build_command = read_string_array(tbl, "build_command");
    a.test_command = read_string_array(tbl, "test_command");
    a.error_format = tbl["error_format"].value_or<std::string>("auto");
    a.error_pattern = tbl["error_pattern"].value_or<std::string>("");

    if (a.name.empty()) {
        throw std::runtime_error(
            fmt::format("adapter at {} is missing required 'name' field", path.string()));
    }
    return a;
}

// Extract the last max_bytes of raw output, snapped forward to the
// next line boundary so the LLM never sees a half line.
[[nodiscard]] std::string tail_excerpt(std::string_view raw, std::size_t max_bytes) {
    if (raw.size() <= max_bytes) {
        return std::string{raw};
    }
    auto start = raw.size() - max_bytes;
    if (const auto nl = raw.find('\n', start); nl != std::string_view::npos) {
        start = nl + 1;
    }
    return std::string{raw.substr(start)};
}

[[nodiscard]] std::vector<TestFailure> extract_failures(const TestAdapter& a,
                                                        std::string_view raw) {
    std::vector<TestFailure> out;
    if (a.error_format == "regex" && !a.error_pattern.empty()) {
        try {
            const std::regex re(a.error_pattern, std::regex::ECMAScript | std::regex::multiline);
            for (std::cregex_iterator it(raw.data(), raw.data() + raw.size(), re), end; it != end;
                 ++it) {
                const auto& m = *it;
                std::string msg;
                if (m.size() > 1 && m[1].matched) {
                    msg = m[1].str();
                } else {
                    msg = m[0].str();
                }
                out.push_back({std::move(msg)});
            }
            if (!out.empty()) {
                return out;
            }
        } catch (const std::regex_error&) {
            // Fall through to the tail excerpt.
        }
    }
    constexpr std::size_t kTailBytes = 4096;
    out.push_back({tail_excerpt(raw, kTailBytes)});
    return out;
}

}  // namespace

std::vector<TestAdapter> load_adapters(const std::filesystem::path& adapters_dir) {
    if (!std::filesystem::is_directory(adapters_dir)) {
        throw std::runtime_error(
            fmt::format("adapters directory not found: {}", adapters_dir.string()));
    }
    std::vector<TestAdapter> out;
    for (const auto& entry : std::filesystem::directory_iterator(adapters_dir)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        if (entry.path().extension() != ".toml") {
            continue;
        }
        out.push_back(parse_adapter(entry.path()));
    }
    std::sort(out.begin(), out.end(), [](const TestAdapter& a, const TestAdapter& b) {
        return a.name < b.name;
    });
    return out;
}

std::optional<TestAdapter> select_adapter(std::span<const TestAdapter> adapters,
                                          const std::filesystem::path& project_root) {
    const TestAdapter* best = nullptr;
    for (const auto& a : adapters) {
        bool matches = false;
        for (const auto& f : a.detect_files) {
            std::error_code ec;
            if (std::filesystem::exists(project_root / f, ec)) {
                matches = true;
                break;
            }
        }
        if (!matches) {
            continue;
        }
        if (best == nullptr || a.priority > best->priority) {
            best = &a;
        }
    }
    if (best == nullptr) {
        return std::nullopt;
    }
    return *best;
}

TestReport build_report(const TestAdapter& adapter,
                        int exit_code,
                        std::string raw_output,
                        std::chrono::milliseconds duration) {
    TestReport rep;
    rep.exit_code = exit_code;
    rep.passed = (exit_code == 0);
    rep.duration = duration;
    rep.raw_output = std::move(raw_output);
    if (!rep.passed) {
        rep.failures = extract_failures(adapter, rep.raw_output);
    }
    return rep;
}

TestReport run_tests(const TestAdapter& adapter, const std::filesystem::path& project_root) {
    if (adapter.test_command.empty()) {
        throw std::runtime_error(fmt::format("adapter '{}' has no test_command", adapter.name));
    }

    const auto build_dir = project_root / "build";
    std::vector<std::string> argv;
    argv.reserve(adapter.test_command.size());
    for (const auto& tok : adapter.test_command) {
        argv.push_back(substitute(tok, project_root, build_dir));
    }

    const auto cmd = build_shell_command(argv, project_root);
    const auto t0 = std::chrono::steady_clock::now();
    auto raw = run_shell(cmd);
    const auto t1 = std::chrono::steady_clock::now();

    return build_report(adapter,
                        raw.exit_code,
                        std::move(raw.output),
                        std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0));
}

}  // namespace vectra::exec
