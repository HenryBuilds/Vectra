# Contributing to Vectra

Thanks for your interest in Vectra. This document covers the practical
mechanics of contributing — how to build, what we expect from a PR, and
where to look when things break.

## Before you start

- **Read [`architecture.md`](./architecture.md).** It documents the design
  decisions, especially around embedding (last-token pooling, asymmetric
  instruct prefix) and retrieval (hybrid + reranker). Changes that violate
  these decisions need a discussion before code.
- **Open an issue first for non-trivial work.** A 5-line PR doesn't need
  one. A new module, a new dependency, or a behavioral change does.
- **One concern per PR.** Easier to review, easier to revert.

## Development setup

### Toolchain

You need:

- **CMake** 3.25+
- **Ninja** (recommended; CMake will fall back to platform default)
- A **C++20** compiler: Clang 16+, GCC 13+, MSVC 19.36+ (VS 2022 17.6)
- **vcpkg** with `VCPKG_ROOT` exported
- **clang-format 18+** and **clang-tidy 18+** for the lint checks
- **Git** with submodule support

### Build

```bash
git clone --recurse-submodules https://github.com/HenryBuilds/Vectra.git
cd Vectra

cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

For sanitizer builds (Unix only):

```bash
cmake --preset asan && cmake --build --preset debug
cmake --preset tsan && cmake --build --preset debug
```

### Editing

- Honor [`.clang-format`](./.clang-format). Run `clang-format -i` on every
  file you touch — CI rejects unformatted diffs.
- Honor [`.clang-tidy`](./.clang-tidy). Fix new warnings; don't suppress
  unless the suppression is justified in a comment.
- C++20 features are encouraged. Modules are not (cross-platform support
  is still flaky).
- No `using namespace std;` in headers.
- No exceptions across module boundaries — return `std::expected` /
  `absl::Status` style results.

## Commit messages

Follow this shape:

```
<area>: <imperative one-line summary, ≤ 72 chars>

Optional body. Wrap at 80. Explain *why*, not *what* — the diff already
shows what. Reference issues with `Fixes #123` or `Refs #123` on a final
trailer line.
```

Examples:

- `core: incrementally rehash only changed AST nodes`
- `store: add embed_dim column to detect index/model mismatch`
- `embed: switch pooling from MEAN to LAST_TOKEN per Qwen3 spec`

`<area>` should match a directory under `src/` (e.g. `core`, `store`,
`embed`, `retrieve`, `exec`, `cli`) or one of: `build`, `ci`, `docs`,
`tests`, `bench`.

## Pull requests

A good PR has:

- A clear title in the same shape as a commit message.
- A description that says *why* this change exists, what it does at a
  high level, and any non-obvious tradeoffs.
- Tests for new behavior. Bug fixes should include a regression test.
- Green CI. PRs with red CI will not be reviewed.
- Reasonable scope — under ~400 lines diff if possible.

Sign your commits with `git commit -s` (Developer Certificate of Origin).

## Tests

We use **Catch2 v3**. Tests live under `tests/` and follow the layout of
`src/`. A test file's name matches the unit it tests:

```
src/core/chunker.cpp        →  tests/core/chunker_test.cpp
```

Run only one test target:

```bash
cd build/debug
ctest -R chunker_test --output-on-failure
```

## Adding a dependency

The bar is high. Before adding a vcpkg dependency or a third-party
submodule, open an issue and explain:

- Why we need it.
- What we'd have to write to avoid it.
- The license (must be permissive — Apache-2.0, MIT, BSD).
- Maintenance status.

## Reporting bugs

Open an issue with:

- Vectra version (`vectra --version`)
- Platform (`uname -a` on Unix, `ver` on Windows)
- Compiler & version
- Minimal reproduction
- Expected vs. actual behavior

For crashes, include a stack trace. For embedding/retrieval quality
issues, include the query, expected hits, and actual hits.

## Code of conduct

Be kind. Disagree on technical merits, not on people. Reviewers should
explain *why* they want a change, not just demand it. Authors should
push back when they disagree — silent compliance hurts the project.
