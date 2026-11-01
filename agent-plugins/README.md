# Agent plugins

Dao settings provide manual installation, updates, and removal of the entry
skills and command link independently of the MCP server switch. Codex, Cursor,
and other compatible agents share `~/.agents/skills/{dao-lite,dao-browser}`;
Claude Code has a separate manual entry for
`~/.claude/skills/{dao-lite,dao-browser}`. Opening settings or checking for updates flags stale installed
copies. Updating the shared skill also removes Dao-managed legacy copies from
`~/.codex/skills/{dao-lite,dao-browser}`, while preserving user-owned files.

These packages are an additional distribution channel: they add a marketplace
listing and `/dao-lite` and `/dao-browser` commands. An explicit user choice of
`dao-browser` or `dao-lite` takes priority. Only when neither is specified,
use `dao-browser` for existing Dao tabs or windows and `dao-lite` for searches,
research, and background browser tasks. Their guides come from the installed Dao
through `dao-browser skill --lite` and `dao-browser skill`, respectively. Both
modes can use sign-ins, but only background consent lasts one fixed hour across
connections.

- `claude-code/` — Claude Code plugin with both entry skills and commands. Add it with
  `/plugin marketplace add <path or repo>/agent-plugins`, then
  `/plugin install dao-browser@dao-browser`.
- `agents/` — Shared [Agent Plugins](https://agent-plugins.org/) package with a
  root `plugin.json` and both entry skills in `skills/`. Use this same package
  for [Codex](https://developers.openai.com/plugins/build/plugins),
  [Cursor](https://cursor.com/docs/reference/plugins), and other compatible
  agents. OpenAI-specific presentation metadata lives in
  `extensions.com.openai`; the skill files are shared.

Plugin sources previously pointing to `agent-plugins/codex/` or
`agent-plugins/cursor/` must now point to `agent-plugins/agents/`. The shared
package requires a client that supports the portable Agent Plugins format.
For clients that only support older plugin formats, use Dao settings to install
the local skills into `~/.agents/skills/`, if the client supports that location,
or update the client. Local skill installation paths and the Claude marketplace
entry are unchanged.

Every `*/skills/dao-browser/SKILL.md` and `*/skills/dao-lite/SKILL.md` must stay
identical to their respective `src/dao/browser/cli/dao_*_entry_skill.md`;
`scripts/__tests__/agent_plugins.test.ts` checks this.
