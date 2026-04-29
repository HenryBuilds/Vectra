// Copyright 2026 Vectra Contributors. Apache-2.0.
//
// Render an exec::Patch as a colored unified diff to a stream so the
// REPL can show the user what the LLM is about to apply. The output
// shape mirrors `git diff --color`, so terminal users see the patch
// the way they're already used to reviewing changes.

#pragma once

#include <ostream>

#include "vectra/exec/diff.hpp"

namespace vectra::cli {

struct DiffRenderOptions {
    // ANSI escape codes for adds (green) and removes (red). Disable
    // when the stream is not a TTY or the user passed --no-color so
    // captured logs / pipes stay clean.
    bool use_color = true;
};

// Render every FileDiff in `patch` as a unified diff. Each file gets
// a `--- a/<old> / +++ b/<new>` header followed by its hunks; new
// and deleted files print `/dev/null` on the appropriate side.
void render_diff(std::ostream& out, const exec::Patch& patch, const DiffRenderOptions& opts = {});

}  // namespace vectra::cli
