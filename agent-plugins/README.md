# Agent plugins

Dao installs its `dao-browser` and `dao-lite` entry skills for local agents while
the MCP server switch is on (see
`src/dao/browser/mcp/dao_local_agent_integration.mm`).

These packages are an additional distribution channel: they add a marketplace
listing and `/dao-lite` and `/dao-browser` commands. `dao-lite` handles searches,
research, and background browser tasks; `dao-browser` is only for explicitly
requested existing tabs. Their guides come from the installed Dao through
`dao-browser skill --lite` and `dao-browser skill`, respectively. Both modes can
use sign-ins, but only background consent lasts one fixed hour across connections.

- `claude-code/` — Claude Code plugin with both entry skills and commands. Add it with
  `/plugin marketplace add <path or repo>/agent-plugins`, then
  `/plugin install dao-browser@dao-browser`.
- `codex/` — Codex plugin (`.codex-plugin/plugin.json`) with both entry skills.
- `cursor/` — Cursor plugin (`.cursor-plugin/plugin.json`) with both entry
  skills.

Every `*/skills/dao-browser/SKILL.md` and `*/skills/dao-lite/SKILL.md` must stay
identical to their respective `src/dao/browser/cli/dao_*_entry_skill.md`;
`scripts/__tests__/agent_plugins.test.ts` checks this.
