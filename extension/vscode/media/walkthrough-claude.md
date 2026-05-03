# Claude Code

Vectra retrieves context from your repo and hands it to `claude -p`
on every chat turn. Claude Code does the actual editing.

## Install

```sh
npm i -g @anthropic-ai/claude-code
claude          # signs in via OAuth on first run
```

A free Claude account is enough; Pro / Max sign-in unlocks more
generous rate limits. Do **not** set `ANTHROPIC_API_KEY` — Vectra
deliberately strips it from the subprocess so OAuth wins on
machines that have a stale or empty key in the shell env.

## Verify

`claude --version` should print a version. If not, the `claude`
binary isn't on PATH; reopen the shell or fix your install.
