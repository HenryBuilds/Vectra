#!/usr/bin/env node
// Copyright 2026 Vectra Contributors. Apache-2.0.
//
// Read every runs/<id>/meta.json + runs/<id>/{vectra,claude}.txt and
// emit a markdown report comparing the two pipelines side-by-side.
//
//   node format-report.js > REPORT.md
//
// The harness already wrote the raw NDJSON streams; this script just
// summarises. Re-run after run-poc.sh to refresh the report.

'use strict';

const fs = require('fs');
const path = require('path');

const POC_DIR = __dirname;
const RUNS_DIR = path.join(POC_DIR, 'runs');
const TASKS_FILE = path.join(POC_DIR, 'tasks.json');

function fmtTokens(n) {
    if (n === undefined || n === null) return '—';
    if (n >= 1000) return `${(n / 1000).toFixed(1)}k`;
    return String(n);
}

function fmtUsd(n) {
    if (n === undefined || n === null) return '—';
    return `$${n.toFixed(4)}`;
}

function fmtSec(n) {
    if (n === undefined || n === null) return '—';
    return `${n.toFixed(1)}s`;
}

function pct(a, b) {
    if (a === undefined || b === undefined || b === 0) return '';
    const d = ((a - b) / b) * 100;
    const sign = d >= 0 ? '+' : '';
    return ` (${sign}${d.toFixed(0)}%)`;
}

function loadMeta(id) {
    const p = path.join(RUNS_DIR, id, 'meta.json');
    if (!fs.existsSync(p)) return null;
    return JSON.parse(fs.readFileSync(p, 'utf8'));
}

function loadAnswer(id, side) {
    const p = path.join(RUNS_DIR, id, `${side}.txt`);
    if (!fs.existsSync(p)) return '(no answer captured)';
    const text = fs.readFileSync(p, 'utf8').trim();
    return text || '(empty)';
}

// Grade an answer against the task's verify.must_contain anchors.
// Returns { passed: boolean, missing: string[] }. Case-insensitive
// substring match — generous on purpose, since the anchors are
// already chosen to be discriminating identifiers (function names,
// distinctive file basenames). The grade is a "did the model
// actually find the right code" signal, not a quality check.
function grade(answer, verify) {
    if (!verify || !Array.isArray(verify.must_contain) || verify.must_contain.length === 0) {
        return { passed: null, missing: [] };
    }
    const haystack = answer.toLowerCase();
    const missing = [];
    for (const needle of verify.must_contain) {
        if (!haystack.includes(String(needle).toLowerCase())) {
            missing.push(needle);
        }
    }
    return { passed: missing.length === 0, missing };
}

function gradeIcon(passed) {
    if (passed === true) return '✓';
    if (passed === false) return '✗';
    return '—';
}

function metric(meta, side, key) {
    return meta?.[side]?.result?.usage?.[key];
}

// Total tokens Claude actually saw on the input side. The
// `input_tokens` field only counts fresh, non-cached tokens; on
// cache hits Anthropic still has to do the work, but at a steep
// discount. Summing all three gives the honest "context volume"
// the model processed and is what most users mean when they say
// "how big was the prompt".
function totalInput(meta, side) {
    const u = meta?.[side]?.result?.usage;
    if (!u) return undefined;
    return (
        (u.input_tokens ?? 0) +
        (u.cache_creation_input_tokens ?? 0) +
        (u.cache_read_input_tokens ?? 0)
    );
}

function main() {
    const tasks = JSON.parse(fs.readFileSync(TASKS_FILE, 'utf8')).tasks;
    const out = [];

    out.push('# Vectra vs Claude Code · proof-of-concept run');
    out.push('');
    out.push(
        '**Repo under test:** [kubernetes/kubernetes](https://github.com/kubernetes/kubernetes) (shallow clone, ~17k Go files, ~245k symbol chunks).',
    );
    out.push('');
    out.push(
        '**What this measures.** Each task is run twice in the same shell, in the same kubernetes/ directory:',
    );
    out.push('');
    out.push(
        '- **Vectra path** — `vectra ask "$task" --stream-json --permission-mode plan`. Vectra runs hybrid retrieval (FTS5 over symbol-only index, no embeddings) and hands the top chunks to `claude -p` as labeled context.',
    );
    out.push(
        '- **Claude-only path** — `claude -p "$task" --output-format=stream-json --permission-mode plan`. Claude Code does its own retrieval via its built-in `Glob` / `Grep` / `Read` tools.',
    );
    out.push('');
    out.push(
        'Plan mode keeps both runs read-only, so the comparison is purely *can the system find the right code and reason about it*. Token / cost / wall-clock totals come from each run\'s final `result` event.',
    );
    out.push('');
    out.push('## Aggregate');
    out.push('');

    const rows = [];
    let vTotCtx = 0, vTotOut = 0, vTotUsd = 0, vTotSec = 0, vTotTurns = 0;
    let cTotCtx = 0, cTotOut = 0, cTotUsd = 0, cTotSec = 0, cTotTurns = 0;
    let vGraded = 0, vPassed = 0, cGraded = 0, cPassed = 0;

    for (const task of tasks) {
        const meta = loadMeta(task.id);
        if (!meta) continue;
        const vCtx = totalInput(meta, 'vectra') ?? 0;
        const vOut = metric(meta, 'vectra', 'output_tokens') ?? 0;
        const vUsd = meta.vectra.result?.total_cost_usd ?? 0;
        const vSec = meta.vectra.wall_seconds ?? 0;
        const vTurns = meta.vectra.result?.num_turns ?? 0;
        const cCtx = totalInput(meta, 'claude') ?? 0;
        const cOut = metric(meta, 'claude', 'output_tokens') ?? 0;
        const cUsd = meta.claude.result?.total_cost_usd ?? 0;
        const cSec = meta.claude.wall_seconds ?? 0;
        const cTurns = meta.claude.result?.num_turns ?? 0;

        const vGrade = grade(loadAnswer(task.id, 'vectra'), task.verify);
        const cGrade = grade(loadAnswer(task.id, 'claude'), task.verify);
        if (vGrade.passed !== null) {
            vGraded += 1;
            if (vGrade.passed) vPassed += 1;
        }
        if (cGrade.passed !== null) {
            cGraded += 1;
            if (cGrade.passed) cPassed += 1;
        }

        vTotCtx += vCtx; vTotOut += vOut; vTotUsd += vUsd; vTotSec += vSec; vTotTurns += vTurns;
        cTotCtx += cCtx; cTotOut += cOut; cTotUsd += cUsd; cTotSec += cSec; cTotTurns += cTurns;

        rows.push({ task, vCtx, vOut, vUsd, vSec, vTurns, cCtx, cOut, cUsd, cSec, cTurns, vGrade, cGrade });
    }

    out.push('| metric | Vectra → Claude | Claude alone | Δ |');
    out.push('|---|---:|---:|---:|');
    if (vGraded > 0 || cGraded > 0) {
        out.push(`| **answers correct** (verifier hits) | **${vPassed} / ${vGraded}** | **${cPassed} / ${cGraded}** | — |`);
    }
    out.push(`| context tokens processed (sum) | ${fmtTokens(vTotCtx)} | ${fmtTokens(cTotCtx)} | ${pct(vTotCtx, cTotCtx).trim() || '—'} |`);
    out.push(`| output tokens (sum) | ${fmtTokens(vTotOut)} | ${fmtTokens(cTotOut)} | ${pct(vTotOut, cTotOut).trim() || '—'} |`);
    out.push(`| total cost (USD) | ${fmtUsd(vTotUsd)} | ${fmtUsd(cTotUsd)} | ${pct(vTotUsd, cTotUsd).trim() || '—'} |`);
    out.push(`| wall-clock (sum) | ${fmtSec(vTotSec)} | ${fmtSec(cTotSec)} | ${pct(vTotSec, cTotSec).trim() || '—'} |`);
    out.push(`| turns (sum) | ${vTotTurns} | ${cTotTurns} | ${pct(vTotTurns, cTotTurns).trim() || '—'} |`);
    out.push('');
    out.push('"answers correct" = the assistant\'s text answer contains every `verify.must_contain` anchor for that task (case-insensitive). Anchors are picked so a wrong path does not accidentally pass.');
    out.push('');
    out.push('"context tokens processed" = `input_tokens + cache_creation + cache_read`, the actual prompt volume Claude saw (cache hits are still work, just billed cheaper).');
    out.push('');
    out.push('"Δ" = Vectra path relative to Claude-alone (lower is usually better for tokens / cost / time; turns is mixed signal).');
    out.push('');
    out.push('## Per-task detail');
    out.push('');

    for (const row of rows) {
        const { task } = row;
        out.push(`### ${task.id}`);
        out.push('');
        out.push(`> ${task.question}`);
        out.push('');
        if (task.verify?.must_contain?.length) {
            out.push(
                `**Verifier anchors:** \`${task.verify.must_contain.map((s) => String(s)).join('` · `')}\``,
            );
            out.push('');
        }
        out.push('| metric | Vectra → Claude | Claude alone |');
        out.push('|---|---:|---:|');
        if (row.vGrade.passed !== null || row.cGrade.passed !== null) {
            const vCell =
                row.vGrade.passed === true
                    ? '✓'
                    : `✗ (missing: ${row.vGrade.missing.map((s) => `\`${s}\``).join(', ')})`;
            const cCell =
                row.cGrade.passed === true
                    ? '✓'
                    : `✗ (missing: ${row.cGrade.missing.map((s) => `\`${s}\``).join(', ')})`;
            out.push(`| answer correct | ${vCell} | ${cCell} |`);
        }
        out.push(`| context tokens processed | ${fmtTokens(row.vCtx)} | ${fmtTokens(row.cCtx)} |`);
        out.push(`| output tokens | ${fmtTokens(row.vOut)} | ${fmtTokens(row.cOut)} |`);
        out.push(`| total cost (USD) | ${fmtUsd(row.vUsd)} | ${fmtUsd(row.cUsd)} |`);
        out.push(`| wall-clock | ${fmtSec(row.vSec)} | ${fmtSec(row.cSec)} |`);
        out.push(`| turns | ${row.vTurns} | ${row.cTurns} |`);
        out.push('');
        out.push('<details><summary>Vectra → Claude answer</summary>');
        out.push('');
        out.push('```');
        out.push(loadAnswer(task.id, 'vectra'));
        out.push('```');
        out.push('');
        out.push('</details>');
        out.push('');
        out.push('<details><summary>Claude alone answer</summary>');
        out.push('');
        out.push('```');
        out.push(loadAnswer(task.id, 'claude'));
        out.push('```');
        out.push('');
        out.push('</details>');
        out.push('');
    }

    out.push('## Caveats');
    out.push('');
    out.push(
        '- **n = 4.** This is an anecdote, not a benchmark. Results vary across runs; both pipelines have non-determinism (Claude\'s temperature, Vectra\'s tie-breaking on equal scores).',
    );
    out.push(
        '- **Symbol-only Vectra.** No embeddings used; the index is FTS5 + tree-sitter symbol search. Hybrid (with the qwen3-embed-0.6b GGUF) would shift the input-token mix further toward Vectra.',
    );
    out.push(
        '- **Plan mode.** Read-only; results don\'t reflect Vectra\'s value on edit-style tasks where Claude\'s tool budget grows fastest.',
    );
    out.push(
        '- **Claude\'s own retrieval is good.** On a repo it has likely seen during training (kubernetes), Claude\'s built-in `Grep` / `Glob` is a strong baseline.',
    );

    process.stdout.write(out.join('\n') + '\n');
}

main();
