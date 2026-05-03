# Vectra

**Local-first code RAG and coding assistant.** Vectra indexes your codebase
with tree-sitter, embeds it on-device via llama.cpp, and answers code
questions with hybrid vector + symbol retrieval, cross-encoder reranking,
and a self-healing patch loop. No cloud, no telemetry, single binary.

> **Status:** Pre-alpha. Repository scaffolding only — no working features yet.
> See [`architecture.md`](./architecture.md) for the design.

## Why Vectra

Existing code assistants either ship your code to a remote API or run a
generic embedding model that doesn't understand code structure. Vectra
treats code as a structured object: AST-aware chunking, hierarchical
context (function → class → call-graph → file), hybrid retrieval that
combines semantic similarity with exact identifier matches, and a
language-aware execution loop that can compile and re-test patches it
generates.

Read the full architecture in [`architecture.md`](./architecture.md).

## Supported Platforms

| Tier | Platforms |
|------|-----------|
| **Tier 1** (CI + prebuilt binaries) | Linux x86_64 · Linux ARM64 · macOS ARM64 · macOS x86_64 · Windows x86_64 |
| **Tier 2** (build-tested)           | Windows ARM64 · FreeBSD |

GPU acceleration via llama.cpp: CUDA, ROCm/HIP, Metal, Vulkan.
CPU fallback with AVX2 / NEON.

## Building from source

### Prerequisites

- **CMake** 3.25 or newer
- **Ninja** (recommended) or Visual Studio 2022
- A C++20 compiler: Clang 16+, GCC 13+, or MSVC 19.36+
- **vcpkg** (set `VCPKG_ROOT` to its install path)
- **Git** with submodule support

### Quick build

```bash
git clone --recurse-submodules https://github.com/HenryBuilds/Vectra.git
cd Vectra

# Configure + build (pick the preset matching your platform)
cmake --preset release
cmake --build --preset release

# Run tests
ctest --preset release
```

### Available presets

| Preset | Description |
|--------|-------------|
| `debug` / `release` / `relwithdebinfo` | Generic single-config builds |
| `linux-clang-release` / `linux-gcc-release` | Linux with explicit compiler |
| `macos-clang-release` | macOS (Apple Silicon or Intel) |
| `windows-msvc-release` / `windows-clang-release` | Windows with MSVC or clang-cl |
| `asan` / `tsan` | Sanitizer builds (Unix only) |
| `linux-clang-cuda-release` / `linux-gcc-cuda-release` | Linux + CUDA, multi-arch redistributable (Turing → Blackwell) |
| `windows-msvc-cuda-release` | Windows + CUDA, same multi-arch list |
| `macos-clang-metal-release` | macOS + Metal (explicit; equivalent to the auto-detect default on macOS) |
| `linux-clang-rocm-release` | Linux + HIP/ROCm (AMD) |
| `linux-clang-vulkan-release` | Linux + Vulkan (cross-vendor fallback) |

### GPU acceleration

Vectra builds the embedding model through llama.cpp / ggml, which
ships backends for CUDA, Metal, HIP/ROCm, and Vulkan. **Vectra
auto-detects the right one at configure time** — you usually do
not need a flag.

The probe runs once per fresh configure (`build/<preset>/CMakeCache.txt`
remembers the choice afterwards) and follows this order:

| Platform | First match wins |
|----------|------------------|
| **macOS** (Apple Silicon + Intel) | Metal |
| **Linux / Windows** | CUDA Toolkit → ROCm/HIP → Vulkan SDK → CPU |

Watch the configure output for the resolved choice:

```
-- vectra: auto-detected CUDA Toolkit 12.8 — enabling CUDA backend
-- ...
-- Vectra 0.0.1
--   GPU backend  : CUDA
```

**Overrides** (any time you want something other than auto):

```bash
# Force a specific backend (skips probe):
cmake --preset release -DVECTRA_GPU_CUDA=ON
cmake --preset release -DVECTRA_GPU_METAL=ON
cmake --preset release -DVECTRA_GPU_VULKAN=ON
cmake --preset release -DVECTRA_GPU_HIP=ON

# Force CPU-only even on a GPU machine:
cmake --preset release -DVECTRA_AUTO_GPU=OFF

# Pick CUDA on a machine that also has Vulkan / ROCm installed
# (CUDA already wins by probe order; this just makes it explicit):
cmake --preset release -DVECTRA_GPU_CUDA=ON
```

**CUDA notes.** CUDA architecture selection is delegated to llama.cpp's
ggml-cuda CMakeLists, which auto-targets the host GPU when
`GGML_NATIVE=ON` (the default for native builds). For redistributable
binaries, override with a multi-arch list, e.g.:

```bash
cmake --preset release -DVECTRA_GPU_CUDA=ON \
    -DCMAKE_CUDA_ARCHITECTURES="75-virtual;86-real;89-real;120a-real"
```

This covers Turing → Blackwell with one binary. CUDA Toolkit 12.8+ is
required for Blackwell (RTX 50-series) FP4 tensor cores; older
toolkits skip the `120a-real` arch automatically.

**Skipping the embedder.** If you only work on the CLI / store / core
modules and don't want a 10–15-minute first build, pass
`-DVECTRA_BUILD_EMBED=OFF`. Vectra falls back to symbol-only retrieval
(FTS5 + tree-sitter), which works without any GPU or embedding model.

## Repository layout

```
vectra/
├── architecture.md          Design document (start here)
├── languages.toml           Language registry (data-driven, no C++ changes)
├── CMakeLists.txt           Top-level build
├── CMakePresets.json        Cross-platform build presets
├── vcpkg.json               Manifest dependencies
├── cmake/                   Reusable CMake helpers (e.g. TreeSitterGrammar)
├── queries/<lang>/          Tree-sitter queries: chunks, symbols, imports
├── adapters/                Build-tool manifests: cargo, cmake, npm, ...
├── third_party/             Pinned submodules
│   ├── llama.cpp/           Inference runtime (embeddings + generation)
│   ├── tree-sitter/         Parser runtime
│   ├── usearch/             HNSW vector index (header-only)
│   └── grammars/            Per-language tree-sitter grammars
├── src/
│   ├── core/                Tree-sitter chunking, Merkle index
│   ├── store/               SQLite + usearch persistence
│   ├── embed/               llama.cpp embedding wrapper
│   ├── retrieve/            Hybrid query path + reranker
│   ├── exec/                Diff, language adapters, patch loop
│   └── cli/                 Command-line entry point
├── include/vectra/          Public headers
├── tests/                   Catch2 unit tests
└── benchmarks/              google/benchmark perf tests
```

### Cloning with submodules

Vectra vendors `llama.cpp`, `tree-sitter`, `usearch`, and seven
tree-sitter grammars as git submodules under `third_party/`. Always
clone with submodules:

```bash
git clone --recurse-submodules https://github.com/HenryBuilds/Vectra.git
```

If you cloned without `--recurse-submodules`, run:

```bash
git submodule update --init --recursive
```

## Roadmap

The work breaks down into discrete bootstrap steps:

1. **Repository scaffolding** — CMake, vcpkg, presets, license, CI _(this commit)_
2. **CI matrix** — Linux/macOS/Windows × Clang/GCC/MSVC, all green
3. **Submodule wiring** — llama.cpp, tree-sitter, language grammars
4. **`vectra-core`** — AST chunker + Merkle index
5. **`vectra-store`** — SQLite schema + usearch sync
6. **`vectra index <path>`** — first end-to-end CLI command
7. **`vectra-embed`** — llama.cpp embedding pipeline
8. **`vectra-retrieve`** — hybrid query + reranker
9. **`vectra-exec`** — patch loop + language adapters

## License

Apache-2.0. See [`LICENSE`](./LICENSE).

## Contributing

See [`CONTRIBUTING.md`](./CONTRIBUTING.md).
