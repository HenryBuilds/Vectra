# Chat panel

The chat opens beside your editor as a normal tab. Each turn:

1. Vectra retrieves top-K chunks from your index.
2. Claude Code is spawned with those chunks as labeled context.
3. Claude streams its response back; tool uses (Edit / Write /
   Bash) are surfaced inline.

## Permission modes

The toolbar at the top of the chat picks the policy:

- **auto** — Claude edits without asking. Good for rapid changes
  in a clean working tree.
- **ask** — every Edit / Write / MultiEdit / Bash routes through
  an in-chat approval modal with a tool-aware preview (diff for
  edits, command + risk badges for Bash).
- **plan** — Claude proposes a plan and waits for you to accept
  before any tool use.

## Always-allow rules

Click **Always allow** in the modal to remember a pattern (path or
command prefix) so future matching requests skip the modal.
Manage with the **Vectra: Manage always-allow rules** command.
