#!/usr/bin/env bash
# Copyright 2026 Vectra Contributors. Apache-2.0.
#
# Proof-of-concept harness: run each task from tasks.json through
#   (a) `vectra ask`        (Vectra retrieval → claude -p)
#   (b) `claude -p` plain   (Claude Code's own Glob/Grep/Read)
# inside the kubernetes/kubernetes clone, capturing per-task:
#   - the answer text
#   - the result-event JSON (token totals, cost, duration)
#   - wall-clock time
#
# Outputs land in benchmarks/proof-of-concept/runs/<task-id>/
# as: vectra.ndjson, vectra.txt, claude.ndjson, claude.txt, meta.json
#
# Usage:
#   KUBE_DIR=/path/to/kubernetes ./run-poc.sh
#
# The script is intentionally dumb shell — no Python, no jq dance.
# A future commit can add a markdown formatter that reads the runs/.

set -euo pipefail

POC_DIR="$(cd "$(dirname "$0")" && pwd)"
RUNS_DIR="$POC_DIR/runs"
TASKS_FILE="$POC_DIR/tasks.json"
KUBE_DIR="${KUBE_DIR:-$HOME/Desktop/kubernetes}"

if [ ! -d "$KUBE_DIR/.git" ]; then
    echo "error: KUBE_DIR=$KUBE_DIR does not look like a kubernetes clone" >&2
    exit 1
fi
if [ ! -f "$KUBE_DIR/.vectra/index.db" ]; then
    echo "error: $KUBE_DIR is not vectra-indexed yet — run 'vectra index .' first" >&2
    exit 1
fi

mkdir -p "$RUNS_DIR"

# Strip the leading "_comment" key, then iterate the tasks array.
# We avoid jq as a dependency — node is already on every dev box
# we expect to run this on.
read_tasks() {
    node -e '
        const t = JSON.parse(require("fs").readFileSync(process.argv[1], "utf8"));
        for (const x of t.tasks) console.log(`${x.id}\t${x.question}`);
    ' "$TASKS_FILE"
}

run_one() {
    local id="$1" question="$2"
    local out_dir="$RUNS_DIR/$id"
    mkdir -p "$out_dir"
    echo "$question" > "$out_dir/question.txt"

    echo
    echo "=== [$id] $question"

    local v_elapsed c_elapsed

    # ---- Vectra path (skip if NDJSON already there — idempotent rerun) ----
    if [ -s "$out_dir/vectra.ndjson" ] && [ -f "$out_dir/vectra.elapsed" ]; then
        v_elapsed=$(cat "$out_dir/vectra.elapsed")
        echo "  · vectra ask …  (cached, ${v_elapsed}s)"
    else
        echo "  · vectra ask …"
        local v_start=$EPOCHREALTIME
        (
            cd "$KUBE_DIR"
            # --stream-json so we can pull the final result event for
            # token / cost numbers; we capture both the raw NDJSON for
            # post-hoc inspection and the assistant text for human eyes.
            vectra ask "$question" --stream-json --permission-mode plan \
                </dev/null \
                > "$out_dir/vectra.ndjson" 2> "$out_dir/vectra.stderr"
        ) || true
        local v_end=$EPOCHREALTIME
        v_elapsed=$(awk -v s="$v_start" -v e="$v_end" 'BEGIN { printf "%.2f", e - s }')
        echo "$v_elapsed" > "$out_dir/vectra.elapsed"
    fi

    # ---- Claude-only path (same idempotency rule) ----
    if [ -s "$out_dir/claude.ndjson" ] && [ -f "$out_dir/claude.elapsed" ]; then
        c_elapsed=$(cat "$out_dir/claude.elapsed")
        echo "  · claude -p …  (cached, ${c_elapsed}s)"
    else
        echo "  · claude -p …"
        local c_start=$EPOCHREALTIME
        (
            cd "$KUBE_DIR"
            claude -p "$question" \
                --output-format=stream-json \
                --include-partial-messages \
                --verbose \
                --permission-mode plan \
                </dev/null \
                > "$out_dir/claude.ndjson" 2> "$out_dir/claude.stderr"
        ) || true
        local c_end=$EPOCHREALTIME
        c_elapsed=$(awk -v s="$c_start" -v e="$c_end" 'BEGIN { printf "%.2f", e - s }')
        echo "$c_elapsed" > "$out_dir/claude.elapsed"
    fi

    # ---- Extract human-readable answer ----
    extract_text "$out_dir/vectra.ndjson" > "$out_dir/vectra.txt"
    extract_text "$out_dir/claude.ndjson" > "$out_dir/claude.txt"

    # ---- Extract result event (final usage / cost) ----
    local v_meta c_meta
    v_meta=$(extract_result "$out_dir/vectra.ndjson")
    c_meta=$(extract_result "$out_dir/claude.ndjson")

    node -e '
        const id = process.argv[1];
        const vMeta = process.argv[2] ? JSON.parse(process.argv[2]) : null;
        const cMeta = process.argv[3] ? JSON.parse(process.argv[3]) : null;
        const meta = {
            id,
            vectra:   { wall_seconds: parseFloat(process.argv[4]), result: vMeta },
            claude:   { wall_seconds: parseFloat(process.argv[5]), result: cMeta },
        };
        require("fs").writeFileSync(process.argv[6], JSON.stringify(meta, null, 2));
    ' "$id" "$v_meta" "$c_meta" "$v_elapsed" "$c_elapsed" "$out_dir/meta.json"

    echo "  · wrote $out_dir/  (vectra ${v_elapsed}s, claude ${c_elapsed}s)"
}

# Pull the assistant message out of an NDJSON stream by concatenating
# every text_delta in the canonical 'assistant' message events. We
# read the final assistant block which holds the full reply.
extract_text() {
    local f="$1"
    node -e '
        const fs = require("fs");
        const lines = fs.readFileSync(process.argv[1], "utf8").split("\n");
        const out = [];
        for (const ln of lines) {
            if (!ln.trim()) continue;
            let e;
            try { e = JSON.parse(ln); } catch { continue; }
            if (e.type === "assistant" && e.message?.content) {
                for (const b of e.message.content) {
                    if (b.type === "text" && typeof b.text === "string") out.push(b.text);
                }
            }
        }
        process.stdout.write(out.join("\n"));
    ' "$f"
}

extract_result() {
    local f="$1"
    node -e '
        const fs = require("fs");
        const lines = fs.readFileSync(process.argv[1], "utf8").split("\n");
        for (const ln of lines.reverse()) {
            if (!ln.trim()) continue;
            let e;
            try { e = JSON.parse(ln); } catch { continue; }
            if (e.type === "result") {
                process.stdout.write(JSON.stringify({
                    duration_ms: e.duration_ms,
                    total_cost_usd: e.total_cost_usd,
                    num_turns: e.num_turns,
                    usage: e.usage,
                }));
                process.exit(0);
            }
        }
    ' "$f"
}

# ---- main ----
echo "POC harness — kubernetes clone at $KUBE_DIR"
echo

# Read tasks line-by-line (id<TAB>question).
while IFS=$'\t' read -r id question; do
    [ -z "$id" ] && continue
    run_one "$id" "$question"
done < <(read_tasks)

echo
echo "All tasks complete. Outputs in $RUNS_DIR"
