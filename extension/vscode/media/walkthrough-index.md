# Indexing

Vectra builds a `.vectra/index.db` SQLite file in your workspace
root. It contains tree-sitter chunks per top-level symbol, an FTS5
index, and (optionally) embeddings.

## Two flavours

- **Symbol-only** — fast (~1–2s on a small repo), no GPU, no model
  download. FTS5 over symbol names + chunk text. The default.
- **Hybrid (with embeddings)** — symbol search + vector search,
  fused via RRF. Adds 10–50 ms to retrieval but strongly improves
  paraphrase recall ("where do we deserialize the auth token" →
  finds `Token::from_str`). Requires an embedding model (Qwen3
  variants, pulled on first use).

## Recommended starting point

Run `Vectra: Re-index with model…` and pick **qwen3-embed-0.6b**.
It's CPU-friendly (~600 MB), ~1024-dim, and gives a real recall
boost over symbol-only without needing a GPU. Switch to a 4B / 8B
later if you have one.

## Auto-indexing

Once `.vectra/` exists, the extension watches your workspace and
re-indexes affected files when you save. BLAKE3 hashes skip
unchanged files, so re-runs are cheap.
