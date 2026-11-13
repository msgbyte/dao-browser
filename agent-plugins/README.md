# Agent plugins

Dao settings provide manual installation, updates, and removal of the entry
skills and command link independently of the MCP server switch. Codex, Cursor,
and other compatible agents share `~/.agents/skills/{dao-home,dao-lite,dao-browser}`;
Claude Code has a separate manual entry for
`~/.claude/skills/{dao-home,dao-lite,dao-browser}`. Opening settings or checking for updates flags stale installed
copies. Find Home installation steps and Codex/Claude Code examples at
`dao://settings/localAgents` under **Skills and command > How to use > Dao Home**.
Install the appropriate client skills and the `dao-browser` command, then reopen
your AI tool. Home requires no MCP setup. Updating the shared skill also removes Dao-managed legacy copies from
`~/.codex/skills/{dao-home,dao-lite,dao-browser}`, while preserving user-owned files.

These packages are an additional distribution channel: they add a marketplace
listing and `/dao-home`, `/dao-lite`, and `/dao-browser` commands. An explicit user choice of
`dao-home`, `dao-browser`, or `dao-lite` takes priority. When none is specified,
use `dao-home` for Home project creation or editing, then
use `dao-browser` for existing Dao tabs or windows and `dao-lite` for searches,
research, and background browser tasks. Their guides come from the installed Dao
through `dao-browser skill --lite` and `dao-browser skill`, respectively. Both
modes can use sign-ins, but only background consent lasts one fixed hour across
connections.

- `claude-code/` — Claude Code plugin with all three entry skills and commands. Add it with
  `/plugin marketplace add <path or repo>/agent-plugins`, then
  `/plugin install dao-browser@dao-browser`.
- `agents/` — Shared [Agent Plugins](https://agent-plugins.org/) package with a
  root `plugin.json` and all three entry skills in `skills/`. Use this same package
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

Dao Home uses `dao-browser home guide`, private system temporary checkouts, and
its own native connection even when MCP is disabled. It previews in a dedicated
visible window of the approved Profile before publishing a confirmed revision.
Its canonical entry is `src/dao/browser/cli/dao_home_entry_skill.md`; both plugin
copies are checked alongside the browsing skills.
