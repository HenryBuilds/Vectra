# Proof-of-concept · Vectra-vs-Claude on kubernetes/kubernetes

This directory holds a small head-to-head run that compares two
ways of asking code questions about a large unfamiliar repository:

1. **Vectra → Claude** — `vectra ask "$task"`. Vectra retrieves with
   FTS5 + tree-sitter symbol search over a local SQLite index and
   hands the top-K chunks to `claude -p` as labeled context.
2. **Claude alone** — `claude -p "$task"`. Claude Code does its own
   retrieval through its built-in `Glob` / `Grep` / `Read` (and on
   default settings, `Bash`).

The repo under test is [kubernetes/kubernetes](https://github.com/kubernetes/kubernetes)
(shallow clone, ~17 000 Go files, ~245 000 chunks after `vectra index`).

## Reproducing

```bash
# 1. Clone the target repo somewhere outside the Vectra tree.
cd ~/Desktop
git clone --depth 1 --filter=blob:none https://github.com/kubernetes/kubernetes.git

# 2. Build the index. Symbol-only is enough for this run.
cd kubernetes
vectra index .

# 3. Run the harness from the Vectra repo.
cd ~/path/to/Vectra
KUBE_DIR=~/Desktop/kubernetes ./benchmarks/proof-of-concept/run-poc.sh

# 4. Render the markdown report from the captured runs.
node benchmarks/proof-of-concept/format-report.js \
    > benchmarks/proof-of-concept/REPORT.md
```

The `runs/<task-id>/` subdirectories carry the raw NDJSON streams
(`vectra.ndjson`, `claude.ndjson`) and the extracted answer text
(`vectra.txt`, `claude.txt`) so a future re-run produces a
diffable evidence trail.

## What "REPORT.md" actually shows

For each task we capture:

- **Input / output tokens** off the final `result` event.
- **Total cost (USD)** as Anthropic reports it for the run.
- **Wall-clock seconds** measured by the harness.
- **Number of turns** — how many tool-use round-trips Claude did.

The aggregate row at the top of the report is what the headline
should read: lower input tokens / cost / time on the Vectra path
is the value the wrapper actually delivers.

## Caveats up front

- **n is tiny.** Four tasks. This is an anecdote, not a benchmark.
  Both pipelines have non-determinism (Claude's temperature,
  Vectra's tie-breaking on equal symbol scores).
- **Symbol-only Vectra.** No embeddings. Hybrid retrieval would
  shift Vectra's input mix further but adds a model-load cost we
  did not want in the headline number.
- **Plan mode.** Both runs use `--permission-mode plan` so neither
  side can edit; the comparison is purely "find and reason about
  code." Edit-style tasks would tilt further toward Vectra because
  Claude's tool budget grows fastest there.
- **Kubernetes is in Claude's training data.** That makes Claude's
  built-in `Grep` / `Glob` an unusually strong baseline. On a
  brand-new repo or a private fork the gap should be wider.
