# Retrieval benchmark · Vectra vs Claude alone on kubernetes/kubernetes

This directory holds a reproducible head-to-head benchmark comparing
two ways of asking code questions about a large unfamiliar
repository:

1. **Vectra → Claude** — `vectra ask "$task"`. Vectra retrieves with
   FTS5 + tree-sitter symbol search over a local SQLite index and
   hands the top chunks to `claude -p` as labeled context.
2. **Claude alone** — `claude -p "$task"`. Claude Code does its own
   retrieval through its built-in `Glob` / `Grep` / `Read` (and
   `Bash` on plan-mode-allowed reads).

The repo under test is [kubernetes/kubernetes](https://github.com/kubernetes/kubernetes)
— shallow clone, ~17 000 Go files, ~245 000 chunks after
`vectra index`. Both pipelines run in `--permission-mode plan` so
neither side modifies code; the comparison is purely *can the
system find the right code and reason about it*.

> **Status.** n = 10 research questions. Not large enough to be a
> formal benchmark — variance per task is real and one or two tasks
> can flip the aggregate signs. Treat the headline numbers as a
> directional read, not a contract.

## What this measures

Per task we capture, from each pipeline's final `result` event:

| field | meaning |
|---|---|
| **answer correct** | did the assistant's text answer contain every `verify.must_contain` anchor? (case-insensitive substring match) |
| context tokens processed | `input_tokens + cache_creation + cache_read` — the actual prompt volume Claude saw |
| output tokens | what Claude generated — proxy for "how much did it write?" |
| total cost (USD) | as Anthropic reports for the run |
| wall-clock | measured by the harness |
| turns | tool-use round-trips Claude did |

The "answer correct" column is the closest we can get to grading
quality without a human in the loop. Each task in `tasks.json`
declares a small set of anchors (function names, distinctive file
basenames) that any correct answer must mention. A task is graded
as a hit only when all anchors appear; that keeps a vague answer
from accidentally passing.

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

The harness is idempotent: cached tasks (those with both
`vectra.ndjson` and `claude.ndjson` already on disk) are skipped.
Delete a task's `runs/<id>/` directory to re-run that one task.

## Files

| path | purpose |
|---|---|
| `tasks.json` | the questions and their verifier anchors |
| `run-poc.sh` | harness — runs both pipelines per task, captures NDJSON streams + final-result metadata |
| `format-report.js` | reads `runs/` and `tasks.json`, produces `REPORT.md` |
| `REPORT.md` | committed evidence trail with current numbers |
| `runs/<task-id>/` | per-task: NDJSON streams, extracted answers, `meta.json` (tokens / cost / duration), stderr captures, elapsed-cache files |

## Caveats up front

- **n = 10.** Not a formal benchmark; variance per task is real.
- **Symbol-only Vectra.** No embeddings. Hybrid retrieval (with
  the qwen3-embed-0.6b GGUF) would shift the input mix further but
  adds a per-query model-load cost we did not want in the headline.
- **Plan mode.** Both runs are read-only; results don't reflect
  Vectra's value on edit-style tasks where Claude's tool budget
  grows fastest.
- **Kubernetes is in Claude's training data.** That makes Claude's
  built-in `Grep` / `Glob` an unusually strong baseline. On a
  brand-new repo or a private fork the gap should be wider.
- **Verifier is a *did-it-find-the-code* gate, not a quality gate.**
  An answer that mentions the right function name but explains its
  semantics wrong still passes. Use the answer text in
  `runs/<id>/{vectra,claude}.txt` for human inspection.

## Adding a task

1. Append a new entry to `tasks.json`:
   ```json
   {
     "id": "kebab-case-id",
     "category": "research",
     "question": "Concrete question with a verifiable answer.",
     "verify": {
       "must_contain": ["FunctionName", "distinctive_file.go"]
     }
   }
   ```
2. Re-run `run-poc.sh` (only the new id will fire).
3. Re-render `REPORT.md`.
4. Commit `tasks.json`, `runs/<new-id>/`, and the updated `REPORT.md`.

Pick anchors that **only the right answer** would mention. Generic
words ("Pod", "controller", "manager") let bad answers pass; type
names, function names, and full file basenames are the right
shape.
