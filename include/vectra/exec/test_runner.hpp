// Copyright 2026 Vectra Contributors. Apache-2.0.
//
// Test-runner adapters: parse adapters/*.toml manifests, pick the
// best match for a project tree, drive its test_command, and turn
// the captured output into a TestReport the patch loop can feed
// back to the LLM. Subprocess execution is shell-based today; the
// adapter manifest is the only knob users twist.

#pragma once

#include <chrono>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace vectra::exec {

// One adapter loaded from adapters/<name>.toml. Command tokens keep
// their {project_root}/{src_dir}/{build_dir} placeholders intact —
// substitution happens at run time when the project root is known.
struct TestAdapter {
    std::string name;
    std::string description;
    std::vector<std::string> detect_files;
    int priority = 0;
    std::vector<std::string> languages;
    std::vector<std::string> configure_command;
    std::vector<std::string> build_command;
    std::vector<std::string> test_command;

    // "json" | "compiler" | "regex" | "auto". Today only "regex" is
    // wired through to the failure extractor; the others fall back
    // to a tail excerpt of the raw output.
    std::string error_format;

    // ECMAScript regex applied per-line when error_format == "regex".
    // The first capture group, if present, is the failure message.
    std::string error_pattern;
};

// One failure extracted from a test run. The message is what the
// LLM-driven self-healing loop sees, so it should be self-contained.
struct TestFailure {
    std::string message;
};

struct TestReport {
    bool passed = false;
    int exit_code = 0;
    std::chrono::milliseconds duration{0};
    std::vector<TestFailure> failures;
    std::string raw_output;  // combined stdout+stderr
};

// Load every adapters/*.toml under adapters_dir into TestAdapters.
// Throws std::runtime_error on a missing directory or a manifest
// missing the required 'name' field.
[[nodiscard]] std::vector<TestAdapter> load_adapters(const std::filesystem::path& adapters_dir);

// Pick the highest-priority adapter whose detect_files exist under
// project_root. Returns nullopt when no adapter applies.
[[nodiscard]] std::optional<TestAdapter> select_adapter(std::span<const TestAdapter> adapters,
                                                        const std::filesystem::path& project_root);

// Run the adapter's test_command in project_root, capturing combined
// stdout+stderr. Throws if the adapter declares no test_command.
[[nodiscard]] TestReport run_tests(const TestAdapter& adapter,
                                   const std::filesystem::path& project_root);

// Public for testing: turn a captured exit code + raw output into a
// TestReport using the adapter's failure-extraction rules. Decoupled
// from the subprocess so callers can unit-test parsing without a
// real test framework on PATH.
[[nodiscard]] TestReport build_report(const TestAdapter& adapter,
                                      int exit_code,
                                      std::string raw_output,
                                      std::chrono::milliseconds duration = {});

}  // namespace vectra::exec
