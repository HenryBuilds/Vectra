#!/usr/bin/env bash
# Copyright 2026 Vectra Contributors. Apache-2.0.
#
# Edit-task harness — same shape as run-poc.sh but each task asks the
# assistant to make a small, well-scoped code change rather than answer
# a research question.
#
# Per task we:
#   1. git -C "$KUBE_DIR" reset --hard HEAD             (clean tree)
#   2. run `vectra ask`, capturing stream-json + final git diff
#   3. git reset --hard HEAD                            (clean for next side)
#   4. run `claude -p`, capturing the same
#   5. git reset --hard HEAD                            (leave tree clean)
#   6. write meta.json with {touched_path_match, anchor_match} per side
#
# Both pipelines run with `--permission-mode bypassPermissions` so
# Edit / Write / Bash all execute without interactive approval —
# otherwise `claude -p` would deadlock on the first tool call.
#
# Usage:
#   KUBE_DIR=/path/to/kubernetes ./run-edit-poc.sh
#   KUBE_DIR=… CONFIG=embed ./run-edit-poc.sh

set -euo pipefail

POC_DIR="$(cd "$(dirname "$0")" && pwd)"
CONFIG="${CONFIG:-symbol-only}"
TASKS_FILE="${TASKS_FILE:-$POC_DIR/tasks-edit.json}"
KUBE_DIR="${KUBE_DIR:-$HOME/Desktop/kubernetes}"

case "$CONFIG" in
    symbol-only)
        RUNS_DIR="${RUNS_DIR_OVERRIDE:-$POC_DIR/runs-edit}"
        VECTRA_RETRIEVAL_FLAGS=()
        BASELINE_RUNS_DIR="${BASELINE_RUNS_DIR_OVERRIDE:-$RUNS_DIR}"
        ;;
    embed)
        RUNS_DIR="${RUNS_DIR_OVERRIDE:-$POC_DIR/runs-edit-embed}"
        VECTRA_RETRIEVAL_FLAGS=(--model qwen3-embed-0.6b)
        BASELINE_RUNS_DIR="${BASELINE_RUNS_DIR_OVERRIDE:-$POC_DIR/runs-edit}"
        ;;
    embed-rerank)
        RUNS_DIR="${RUNS_DIR_OVERRIDE:-$POC_DIR/runs-edit-embed-rerank}"
        VECTRA_RETRIEVAL_FLAGS=(--model qwen3-embed-0.6b --reranker qwen3-rerank-0.6b)
        BASELINE_RUNS_DIR="${BASELINE_RUNS_DIR_OVERRIDE:-$POC_DIR/runs-edit}"
        ;;
    *)
        echo "error: CONFIG=$CONFIG is not one of {symbol-only, embed, embed-rerank}" >&2
        exit 1
        ;;
esac

if [ ! -d "$KUBE_DIR/.git" ]; then
    echo "error: KUBE_DIR=$KUBE_DIR does not look like a kubernetes clone" >&2
    exit 1
fi
if [ ! -f "$KUBE_DIR/.vectra/index.db" ]; then
    echo "error: $KUBE_DIR is not vectra-indexed yet — run 'vectra index .' first" >&2
    exit 1
fi
if [ ! -f "$TASKS_FILE" ]; then
    echo "error: TASKS_FILE=$TASKS_FILE does not exist" >&2
    exit 1
fi

mkdir -p "$RUNS_DIR"

read_tasks() {
    node -e '
        const t = JSON.parse(require("fs").readFileSync(process.argv[1], "utf8"));
        for (const x of t.tasks) console.log(`${x.id}\t${x.question}`);
    ' "$TASKS_FILE"
}

# Reset the kubernetes clone to a known clean state. We deliberately
# do NOT use `git clean -fd` here — anything untracked in $KUBE_DIR
# (notably .vectra/) must survive between tasks.
reset_clone() {
    git -C "$KUBE_DIR" reset --hard HEAD >/dev/null 2>&1
    git -C "$KUBE_DIR" checkout -- . >/dev/null 2>&1 || true
}

# Capture what changed in the working tree as a unified diff. We do
# not commit; the next reset_clone will throw it away. The diff is
# the single artifact a human reads to judge "did the edit do what
# was asked".
capture_diff() {
    git -C "$KUBE_DIR" diff --no-color -- . > "$1" 2>/dev/null || true
}

run_one() {
    local id="$1" question="$2"
    local out_dir="$RUNS_DIR/$id"
    mkdir -p "$out_dir"
    echo "$question" > "$out_dir/question.txt"

    echo
    echo "=== [$id] $question"

    local v_elapsed c_elapsed

    # ---- Vectra path ----
    if [ -s "$out_dir/vectra.ndjson" ] && [ -f "$out_dir/vectra.elapsed" ]; then
        v_elapsed=$(cat "$out_dir/vectra.elapsed")
        echo "  · vectra ask …  (cached, ${v_elapsed}s)"
    else
        echo "  · vectra ask … [config=$CONFIG, edit mode]"
        reset_clone
        local v_start=$EPOCHREALTIME
        (
            cd "$KUBE_DIR"
            vectra ask "$question" \
                "${VECTRA_RETRIEVAL_FLAGS[@]}" \
                --stream-json --permission-mode bypassPermissions \
                </dev/null \
                > "$out_dir/vectra.ndjson" 2> "$out_dir/vectra.stderr"
        ) || true
        local v_end=$EPOCHREALTIME
        v_elapsed=$(awk -v s="$v_start" -v e="$v_end" 'BEGIN { printf "%.2f", e - s }')
        echo "$v_elapsed" > "$out_dir/vectra.elapsed"
        capture_diff "$out_dir/vectra.diff"
    fi

    # ---- Claude-only path: reuse from baseline if already run ----
    if [ -s "$out_dir/claude.ndjson" ] && [ -f "$out_dir/claude.elapsed" ]; then
        c_elapsed=$(cat "$out_dir/claude.elapsed")
        echo "  · claude -p …  (cached, ${c_elapsed}s)"
    elif [ "$CONFIG" != "symbol-only" ] && [ -s "$BASELINE_RUNS_DIR/$id/claude.ndjson" ]; then
        cp "$BASELINE_RUNS_DIR/$id/claude.ndjson" "$out_dir/claude.ndjson"
        cp "$BASELINE_RUNS_DIR/$id/claude.stderr" "$out_dir/claude.stderr" 2>/dev/null || true
        cp "$BASELINE_RUNS_DIR/$id/claude.elapsed" "$out_dir/claude.elapsed" 2>/dev/null || true
        cp "$BASELINE_RUNS_DIR/$id/claude.diff" "$out_dir/claude.diff" 2>/dev/null || true
        c_elapsed=$(cat "$out_dir/claude.elapsed")
        echo "  · claude -p …  (reused from baseline, ${c_elapsed}s)"
    else
        echo "  · claude -p … [edit mode]"
        reset_clone
        local c_start=$EPOCHREALTIME
        (
            cd "$KUBE_DIR"
            claude -p "$question" \
                --output-format=stream-json \
                --include-partial-messages \
                --verbose \
                --permission-mode bypassPermissions \
                </dev/null \
                > "$out_dir/claude.ndjson" 2> "$out_dir/claude.stderr"
        ) || true
        local c_end=$EPOCHREALTIME
        c_elapsed=$(awk -v s="$c_start" -v e="$c_end" 'BEGIN { printf "%.2f", e - s }')
        echo "$c_elapsed" > "$out_dir/claude.elapsed"
        capture_diff "$out_dir/claude.diff"
    fi

    # Always end on a clean tree for the next task.
    reset_clone

    # ---- Extract human-readable answer ----
    extract_text "$out_dir/vectra.ndjson" > "$out_dir/vectra.txt"
    extract_text "$out_dir/claude.ndjson" > "$out_dir/claude.txt"

    # ---- Extract result event ----
    local v_meta c_meta
    v_meta=$(extract_result "$out_dir/vectra.ndjson")
    c_meta=$(extract_result "$out_dir/claude.ndjson")

    node -e '
        const id = process.argv[1];
        const vMeta = process.argv[2] ? JSON.parse(process.argv[2]) : null;
        const cMeta = process.argv[3] ? JSON.parse(process.argv[3]) : null;
        const meta = {
            id,
            mode: "edit",
            config: process.argv[7],
            vectra: { wall_seconds: parseFloat(process.argv[4]), result: vMeta },
            claude: { wall_seconds: parseFloat(process.argv[5]), result: cMeta },
        };
        require("fs").writeFileSync(process.argv[6], JSON.stringify(meta, null, 2));
    ' "$id" "$v_meta" "$c_meta" "$v_elapsed" "$c_elapsed" "$out_dir/meta.json" "$CONFIG"

    echo "  · wrote $out_dir/  (vectra ${v_elapsed}s, claude ${c_elapsed}s)"
}

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
echo "Edit-POC harness — kubernetes clone at $KUBE_DIR"
echo "config: $CONFIG  ·  tasks: $(basename "$TASKS_FILE")  ·  output: $RUNS_DIR"
echo

# Confirm the working tree is clean before starting; otherwise the
# first reset_clone might wipe legitimate user work. We deliberately
# pass --untracked-files=no — `.vectra/` is always present in an
# indexed clone and counts as untracked, but it is the harness's
# own state and must not block the run.
if [ -n "$(git -C "$KUBE_DIR" status --porcelain --untracked-files=no 2>/dev/null)" ]; then
    echo "error: $KUBE_DIR has uncommitted tracked changes — clean it before running" >&2
    git -C "$KUBE_DIR" status --short --untracked-files=no >&2
    exit 1
fi

while IFS=$'\t' read -r id question; do
    [ -z "$id" ] && continue
    run_one "$id" "$question"
done < <(read_tasks)

echo
echo "All tasks complete. Outputs in $RUNS_DIR"
