# Agent plugins

Dao installs its `dao-browser` entry skill for local agents itself while the
MCP server switch is on (see `src/dao/browser/mcp/dao_local_agent_integration.mm`),
so these packages are only a distribution channel: they add a marketplace
listing and a `/dao-browser` command. They contain no version-specific logic;
the guide and runtime always come from the installed Dao through
`dao-browser skill`.

- `claude-code/` — Claude Code plugin with the entry skill and the
  `/dao-browser` command. Add it with
  `/plugin marketplace add <path or repo>/agent-plugins`, then
  `/plugin install dao-browser@dao-browser`.
- `codex/` — Codex plugin (`.codex-plugin/plugin.json`) with the entry skill.
- `cursor/` — Cursor plugin (`.cursor-plugin/plugin.json`) with the entry
  skill.

Every `*/skills/dao-browser/SKILL.md` must stay identical to
`src/dao/browser/cli/dao_browser_entry_skill.md`;
`scripts/__tests__/agent_plugins.test.ts` checks this.
