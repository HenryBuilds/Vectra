#!/usr/bin/env node
// Copyright 2026 Vectra Contributors. Apache-2.0.
//
// Read every runs*/<id>/meta.json + runs*/<id>/{vectra,claude}.txt
// across all auto-discovered configs (symbol-only / embed / embed-
// rerank) and emit a markdown report with side-by-side columns.
//
// Two modes are supported and detected per directory layout:
//
//   research mode → runs/, runs-embed/, runs-embed-rerank/
//       Tasks come from tasks.json. Verifier checks `verify.must_contain`
//       against the assistant text answer. Output is REPORT.md.
//
//   edit mode    → runs-edit/, runs-edit-embed/, runs-edit-embed-rerank/
//       Tasks come from tasks-edit.json. Verifier checks the captured
//       git diff: did the right file change, do the new contents
//       contain the expected anchors. Output is REPORT-EDIT.md.
//
// Pick mode with `node format-report.js [research|edit]`. Default
// is research. Pipe to a file:
//
//   node format-report.js research > REPORT.md
//   node format-report.js edit     > REPORT-EDIT.md

'use strict';

const fs = require('fs');
const path = require('path');

const POC_DIR = __dirname;

// ---------------------------------------------------------------------------
// Config discovery
// ---------------------------------------------------------------------------

const RESEARCH_CONFIGS = [
    { id: 'symbol-only', label: 'symbol-only', runsDir: 'runs', flags: '(no flags)' },
    { id: 'embed', label: 'embed', runsDir: 'runs-embed', flags: '--model qwen3-embed-0.6b' },
    {
        id: 'embed-rerank',
        label: 'embed+rerank',
        runsDir: 'runs-embed-rerank',
        flags: '--model qwen3-embed-0.6b --reranker qwen3-rerank-0.6b',
    },
];

const EDIT_CONFIGS = [
    { id: 'symbol-only', label: 'symbol-only', runsDir: 'runs-edit', flags: '(no flags)' },
    { id: 'embed', label: 'embed', runsDir: 'runs-edit-embed', flags: '--model qwen3-embed-0.6b' },
    {
        id: 'embed-rerank',
        label: 'embed+rerank',
        runsDir: 'runs-edit-embed-rerank',
        flags: '--model qwen3-embed-0.6b --reranker qwen3-rerank-0.6b',
    },
];

function configHasData(cfg) {
    const dir = path.join(POC_DIR, cfg.runsDir);
    if (!fs.existsSync(dir)) return false;
    // At least one task must have a meta.json for the config to count.
    for (const sub of fs.readdirSync(dir)) {
        if (fs.existsSync(path.join(dir, sub, 'meta.json'))) return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Formatting helpers
// ---------------------------------------------------------------------------

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
    if (a === undefined || a === null || b === undefined || b === null || b === 0) return '';
    const d = ((a - b) / b) * 100;
    const sign = d >= 0 ? '+' : '';
    return ` (${sign}${d.toFixed(0)}%)`;
}

function metric(meta, side, key) {
    return meta?.[side]?.result?.usage?.[key];
}

function totalInput(meta, side) {
    const u = meta?.[side]?.result?.usage;
    if (!u) return undefined;
    return (
        (u.input_tokens ?? 0) +
        (u.cache_creation_input_tokens ?? 0) +
        (u.cache_read_input_tokens ?? 0)
    );
}

function loadMeta(runsDir, id) {
    const p = path.join(POC_DIR, runsDir, id, 'meta.json');
    if (!fs.existsSync(p)) return null;
    try {
        return JSON.parse(fs.readFileSync(p, 'utf8'));
    } catch {
        return null;
    }
}

function loadFile(runsDir, id, name) {
    const p = path.join(POC_DIR, runsDir, id, name);
    if (!fs.existsSync(p)) return null;
    return fs.readFileSync(p, 'utf8');
}

// ---------------------------------------------------------------------------
// Verifiers
// ---------------------------------------------------------------------------

// Research verifier — anchors must appear in the assistant's text.
function gradeResearch(answer, verify) {
    if (!verify || !Array.isArray(verify.must_contain) || verify.must_contain.length === 0) {
        return { passed: null, missing: [] };
    }
    const text = (answer ?? '').toLowerCase();
    const missing = [];
    for (const needle of verify.must_contain) {
        if (!text.includes(String(needle).toLowerCase())) {
            missing.push(needle);
        }
    }
    return { passed: missing.length === 0, missing };
}

// Edit verifier — diff must touch `touched_path`, the file's new
// contents must include `must_contain_after`, and (optionally) must
// not include `must_not_contain_after`. We read the file out of the
// captured diff itself (we don't read the kubernetes worktree because
// the harness git-resets after each run).
function gradeEdit(diffText, verify) {
    if (!verify || !verify.touched_path) {
        return { passed: null, reason: 'no verifier configured' };
    }
    if (!diffText) {
        return { passed: false, reason: 'no diff captured (assistant produced no edit)' };
    }
    // must_contain_after asks "are these tokens in the post-edit file
    // state" — we look at added + context lines because a structural
    // anchor (`func Foo`) is unchanged by an "add comment above" edit
    // and shows up only as context in the diff.
    const postEditLines = extractAddedLinesForPath(diffText, verify.touched_path);
    if (postEditLines === null) {
        return {
            passed: false,
            reason: `expected diff to touch '${verify.touched_path}' but it did not appear in the diff`,
        };
    }
    const postEdit = postEditLines.toLowerCase();
    const missing = [];
    for (const needle of verify.must_contain_after ?? []) {
        if (!postEdit.includes(String(needle).toLowerCase())) {
            missing.push(needle);
        }
    }
    // must_not_contain_after asks "did the edit introduce a forbidden
    // token" — we only look at strictly added lines, so a forbidden
    // string already present in the pre-edit file (and unchanged) is
    // not flagged. The user's verifier is checking the change, not
    // the file's prior state.
    const strictlyAdded =
        (extractStrictlyAddedLinesForPath(diffText, verify.touched_path) ?? '').toLowerCase();
    const forbidden = [];
    for (const needle of verify.must_not_contain_after ?? []) {
        if (strictlyAdded.includes(String(needle).toLowerCase())) {
            forbidden.push(needle);
        }
    }
    if (missing.length === 0 && forbidden.length === 0) {
        return { passed: true };
    }
    const parts = [];
    if (missing.length > 0) parts.push(`missing: ${missing.map((s) => `\`${s}\``).join(', ')}`);
    if (forbidden.length > 0) parts.push(`forbidden present: ${forbidden.map((s) => `\`${s}\``).join(', ')}`);
    return { passed: false, reason: parts.join('; ') };
}

// Reconstruct the *post-edit* state of the touched file out of a
// unified diff. We keep `+` (added) and ` ` (unchanged context)
// lines and drop `-` (removed) lines, because `must_contain_after`
// is a claim about the file *after* the edit — and a structural
// anchor like `func Foo` that already existed in the file shows up
// in the diff as context, not as an added line. Returns null if
// the path was never touched.
function extractAddedLinesForPath(diff, wantedPath) {
    const wanted = wantedPath.replace(/\\/g, '/');
    const lines = diff.split('\n');
    const out = [];
    let inWanted = false;
    let touched = false;
    for (const ln of lines) {
        if (ln.startsWith('diff --git')) {
            const m = ln.match(/diff --git a\/(.+?)\s+b\/(.+)/);
            const a = m ? m[1].replace(/\\/g, '/') : '';
            const b = m ? m[2].replace(/\\/g, '/') : '';
            inWanted = a === wanted || b === wanted;
            if (inWanted) touched = true;
            continue;
        }
        if (!inWanted) continue;
        if (ln.startsWith('+++ ') || ln.startsWith('--- ')) continue;
        if (ln.startsWith('@@')) continue;
        // Keep both `+` (added) and ` ` (context) — these are the
        // lines present in the post-edit state. Drop `-` (removed).
        if (ln.startsWith('-')) continue;
        if (ln.startsWith('+')) {
            out.push(ln.slice(1));
        } else if (ln.startsWith(' ')) {
            out.push(ln.slice(1));
        } else if (ln.length === 0) {
            // Empty lines inside hunks. Keep — they may matter for
            // multi-line anchor matching.
            out.push('');
        }
    }
    if (!touched) return null;
    return out.join('\n');
}

// Same idea but only the *added* lines — used for the
// `must_not_contain_after` check, where we want to know whether
// the edit introduced a forbidden token. Context lines may carry
// the forbidden token from the pre-edit file (which is fine; it's
// the user's responsibility, not the edit's).
function extractStrictlyAddedLinesForPath(diff, wantedPath) {
    const wanted = wantedPath.replace(/\\/g, '/');
    const lines = diff.split('\n');
    const out = [];
    let inWanted = false;
    let touched = false;
    for (const ln of lines) {
        if (ln.startsWith('diff --git')) {
            const m = ln.match(/diff --git a\/(.+?)\s+b\/(.+)/);
            const a = m ? m[1].replace(/\\/g, '/') : '';
            const b = m ? m[2].replace(/\\/g, '/') : '';
            inWanted = a === wanted || b === wanted;
            if (inWanted) touched = true;
            continue;
        }
        if (!inWanted) continue;
        if (ln.startsWith('+++ ') || ln.startsWith('--- ')) continue;
        if (ln.startsWith('@@')) continue;
        if (ln.startsWith('+') && !ln.startsWith('+++')) {
            out.push(ln.slice(1));
        }
    }
    if (!touched) return null;
    return out.join('\n');
}

// ---------------------------------------------------------------------------
// Aggregation
// ---------------------------------------------------------------------------

// For one config, build a per-task row: vectra metrics, claude metrics,
// grade. The grade depends on mode (research vs edit). Returns a map
// taskId -> row.
function buildRows(cfg, tasks, mode) {
    const out = new Map();
    for (const task of tasks) {
        const meta = loadMeta(cfg.runsDir, task.id);
        if (!meta) continue;

        const v = {
            ctx: totalInput(meta, 'vectra'),
            out: metric(meta, 'vectra', 'output_tokens'),
            usd: meta.vectra?.result?.total_cost_usd,
            sec: meta.vectra?.wall_seconds,
            turns: meta.vectra?.result?.num_turns,
        };
        const c = {
            ctx: totalInput(meta, 'claude'),
            out: metric(meta, 'claude', 'output_tokens'),
            usd: meta.claude?.result?.total_cost_usd,
            sec: meta.claude?.wall_seconds,
            turns: meta.claude?.result?.num_turns,
        };

        let vGrade, cGrade;
        if (mode === 'edit') {
            vGrade = gradeEdit(loadFile(cfg.runsDir, task.id, 'vectra.diff'), task.verify);
            cGrade = gradeEdit(loadFile(cfg.runsDir, task.id, 'claude.diff'), task.verify);
        } else {
            vGrade = gradeResearch(loadFile(cfg.runsDir, task.id, 'vectra.txt'), task.verify);
            cGrade = gradeResearch(loadFile(cfg.runsDir, task.id, 'claude.txt'), task.verify);
        }

        out.set(task.id, { v, c, vGrade, cGrade, meta });
    }
    return out;
}

function sumByConfig(rowsMap, side, field) {
    let s = 0;
    for (const r of rowsMap.values()) {
        const v = r[side]?.[field];
        if (typeof v === 'number') s += v;
    }
    return s;
}

function passedCount(rowsMap, side) {
    let g = 0, p = 0;
    for (const r of rowsMap.values()) {
        const grade = side === 'v' ? r.vGrade : r.cGrade;
        if (grade.passed !== null) {
            g += 1;
            if (grade.passed) p += 1;
        }
    }
    return { g, p };
}

// ---------------------------------------------------------------------------
// Render
// ---------------------------------------------------------------------------

function renderReport(mode) {
    const tasksFile =
        mode === 'edit' ? 'tasks-edit.json' : 'tasks.json';
    const tasks = JSON.parse(fs.readFileSync(path.join(POC_DIR, tasksFile), 'utf8')).tasks;
    const allConfigs = mode === 'edit' ? EDIT_CONFIGS : RESEARCH_CONFIGS;
    const configs = allConfigs.filter(configHasData);

    const out = [];

    // ---- Header ----
    if (mode === 'edit') {
        out.push('# Vectra vs Claude Code · edit-task benchmark');
        out.push('');
        out.push(
            '**Repo under test:** [kubernetes/kubernetes](https://github.com/kubernetes/kubernetes) (shallow clone, ~17k Go files, ~245k symbol chunks).',
        );
        out.push('');
        out.push(
            'Each task asks for a small, well-scoped code edit (add a comment, rename a helper, add a one-line log). Both pipelines run with `--permission-mode bypassPermissions` so Edit / Write / Bash all execute without interactive approval. Between every run the harness `git reset --hard HEAD`s the kubernetes clone so the two pipelines start from an identical tree. Verifier checks the captured diff: was `verify.touched_path` modified, and do the changed lines contain `must_contain_after`?',
        );
    } else {
        out.push('# Vectra vs Claude Code · research-task benchmark');
        out.push('');
        out.push(
            '**Repo under test:** [kubernetes/kubernetes](https://github.com/kubernetes/kubernetes) (shallow clone, ~17k Go files, ~245k symbol chunks).',
        );
        out.push('');
        out.push(
            'Both pipelines run with `--permission-mode plan` so neither can modify code. The comparison is purely *can the system find the right code and reason about it*. Token / cost / wall-clock totals come from each run\'s final `result` event. The verifier checks whether each `verify.must_contain` anchor appears in the assistant\'s text answer.',
        );
    }
    out.push('');
    if (configs.length === 0) {
        out.push('_No data captured yet — run `run-poc.sh` (or `run-edit-poc.sh`) first._');
        return out.join('\n') + '\n';
    }

    out.push('**Configs included.** ' + configs.map((c) => `\`${c.label}\``).join(', ') + '.');
    out.push('');
    for (const cfg of configs) {
        out.push(`- \`${cfg.label}\` — \`${cfg.flags}\``);
    }
    out.push('');

    // Build rows per config
    const rowsByConfig = new Map();
    for (const cfg of configs) {
        rowsByConfig.set(cfg.id, buildRows(cfg, tasks, mode));
    }

    // Claude-alone is identical across vectra configs by design — pull
    // its numbers from the first config that has data.
    const claudeRows = rowsByConfig.get(configs[0].id);

    // ---- Aggregate ----
    out.push('## Aggregate');
    out.push('');
    const headerCols = ['metric', ...configs.map((c) => `Vectra · ${c.label}`), 'Claude alone'];
    out.push('| ' + headerCols.join(' | ') + ' |');
    out.push('|' + headerCols.map((_, i) => (i === 0 ? '---' : '---:')).join('|') + '|');

    // verifier hits row
    const claudeHits = passedCount(claudeRows, 'c');
    const vHitsByCfg = configs.map((c) => passedCount(rowsByConfig.get(c.id), 'v'));
    out.push(
        '| **answers correct** | ' +
            vHitsByCfg.map((h) => `**${h.p} / ${h.g}**`).join(' | ') +
            ` | **${claudeHits.p} / ${claudeHits.g}** |`,
    );

    // numeric rows
    const numericRows = [
        { label: 'context tokens (sum)', side: 'ctx', fmt: fmtTokens },
        { label: 'output tokens (sum)', side: 'out', fmt: fmtTokens },
        { label: 'total cost USD (sum)', side: 'usd', fmt: fmtUsd },
        { label: 'wall-clock (sum)', side: 'sec', fmt: fmtSec },
        { label: 'turns (sum)', side: 'turns', fmt: (n) => (n ?? 0).toString() },
    ];
    for (const nr of numericRows) {
        const claudeVal = sumByConfig(claudeRows, 'c', nr.side);
        const vCells = configs.map((c) => {
            const v = sumByConfig(rowsByConfig.get(c.id), 'v', nr.side);
            return `${nr.fmt(v)}${pct(v, claudeVal)}`;
        });
        out.push(`| ${nr.label} | ${vCells.join(' | ')} | ${nr.fmt(claudeVal)} |`);
    }
    out.push('');
    out.push(
        '"answers correct" = the verifier accepts the answer (research: anchors found in text; edit: diff touched the right file and added the right contents). Generic anchors are avoided so a wrong path does not pass.',
    );
    out.push('');
    out.push(
        '"context tokens" = `input_tokens + cache_creation + cache_read`, i.e. the actual prompt volume Claude saw (cache hits are still work, just billed cheaper).',
    );
    out.push('');
    out.push(
        'Percentages in Vectra cells are relative to Claude-alone — lower is usually better for tokens / cost / time.',
    );
    out.push('');

    // ---- Per-task detail ----
    out.push('## Per-task detail');
    out.push('');
    for (const task of tasks) {
        out.push(`### ${task.id}`);
        out.push('');
        out.push(`> ${task.question}`);
        out.push('');

        const verifyAnchors =
            task.verify?.must_contain?.length
                ? task.verify.must_contain
                : task.verify?.must_contain_after?.length
                  ? task.verify.must_contain_after
                  : null;
        if (verifyAnchors) {
            const where = task.verify?.touched_path ? ` (in \`${task.verify.touched_path}\`)` : '';
            out.push(
                `**Verifier${where}:** \`${verifyAnchors.map((s) => String(s)).join('` · `')}\``,
            );
            out.push('');
        }

        out.push('| metric | ' + configs.map((c) => `Vectra · ${c.label}`).join(' | ') + ' | Claude alone |');
        out.push('|---|' + configs.map(() => '---:').join('|') + '|---:|');

        const claudeRow = claudeRows.get(task.id);
        const cellGrade = (g) =>
            g.passed === true
                ? '✓'
                : g.passed === false
                  ? `✗ ${g.reason ? `(${truncate(g.reason, 80)})` : `(missing: ${(g.missing ?? []).map((s) => `\`${s}\``).join(', ')})`}`
                  : '—';
        const vGradeCells = configs.map((c) => {
            const r = rowsByConfig.get(c.id).get(task.id);
            return r ? cellGrade(r.vGrade) : '—';
        });
        const cGradeCell = claudeRow ? cellGrade(claudeRow.cGrade) : '—';
        out.push(`| answer correct | ${vGradeCells.join(' | ')} | ${cGradeCell} |`);

        const numericTask = [
            { label: 'context tokens', side: 'ctx', fmt: fmtTokens },
            { label: 'output tokens', side: 'out', fmt: fmtTokens },
            { label: 'cost (USD)', side: 'usd', fmt: fmtUsd },
            { label: 'wall-clock', side: 'sec', fmt: fmtSec },
            { label: 'turns', side: 'turns', fmt: (n) => (n ?? 0).toString() },
        ];
        for (const nr of numericTask) {
            const cVal = claudeRow?.c[nr.side];
            const vCells = configs.map((c) => {
                const r = rowsByConfig.get(c.id).get(task.id);
                return r ? nr.fmt(r.v[nr.side]) : '—';
            });
            out.push(`| ${nr.label} | ${vCells.join(' | ')} | ${nr.fmt(cVal)} |`);
        }
        out.push('');

        // For research, fold answers into <details> blocks. For edit,
        // fold the diff itself.
        if (mode === 'edit') {
            for (const cfg of configs) {
                const diff = loadFile(cfg.runsDir, task.id, 'vectra.diff');
                if (diff) {
                    out.push(`<details><summary>Vectra · ${cfg.label} diff</summary>`);
                    out.push('');
                    out.push('```diff');
                    out.push(truncate(diff.trim(), 4000));
                    out.push('```');
                    out.push('');
                    out.push('</details>');
                    out.push('');
                }
            }
            const cdiff = loadFile(configs[0].runsDir, task.id, 'claude.diff');
            if (cdiff) {
                out.push('<details><summary>Claude alone diff</summary>');
                out.push('');
                out.push('```diff');
                out.push(truncate(cdiff.trim(), 4000));
                out.push('```');
                out.push('');
                out.push('</details>');
                out.push('');
            }
        } else {
            for (const cfg of configs) {
                const txt = loadFile(cfg.runsDir, task.id, 'vectra.txt');
                if (txt) {
                    out.push(`<details><summary>Vectra · ${cfg.label} answer</summary>`);
                    out.push('');
                    out.push('```');
                    out.push(truncate(txt.trim(), 4000));
                    out.push('```');
                    out.push('');
                    out.push('</details>');
                    out.push('');
                }
            }
            const ctxt = loadFile(configs[0].runsDir, task.id, 'claude.txt');
            if (ctxt) {
                out.push('<details><summary>Claude alone answer</summary>');
                out.push('');
                out.push('```');
                out.push(truncate(ctxt.trim(), 4000));
                out.push('```');
                out.push('');
                out.push('</details>');
                out.push('');
            }
        }
    }

    return out.join('\n') + '\n';
}

function truncate(s, max) {
    if (!s) return '';
    if (s.length <= max) return s;
    return s.slice(0, max) + `\n… [${s.length - max} more chars truncated]`;
}

// ---------------------------------------------------------------------------
// Entry
// ---------------------------------------------------------------------------

function main() {
    const mode = (process.argv[2] || 'research').toLowerCase();
    if (mode !== 'research' && mode !== 'edit') {
        console.error('usage: format-report.js [research|edit]');
        process.exit(2);
    }
    process.stdout.write(renderReport(mode));
}

main();
