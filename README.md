<div align="center">
  <img src="branding/dao_logo.svg" alt="Dao Browser" width="120" />

  ### Dao Browser

  An **AI-native**, content-first Chromium-based browser with a vertical tab sidebar — built for the agentic web.

  [Download](https://dao.msgbyte.com/download) · [Website](https://dao.msgbyte.com/) · [Features](docs/features.md) · [Development](docs/development.md)
</div>

---

### Built-in AI Agent

Dao isn't a browser with an AI extension bolted on — the **AI Agent is a first-class citizen**, woven into the browsing surface itself.

- **Tool-calling agent** that can read, click, scroll, and navigate live pages on your behalf — with an animated cursor + tab-lock banner so you always see what it's doing
- **Long-term memory** (SQLite + FTS5) across episodic and semantic stores — Dao remembers what you've worked on, not just what's in the current tab
- **Proactive suggestions** triggered by navigation — Dao notices when a page matches a scenario you've taught it, then offers to act
- **Skill system** — extend the agent with reusable skills; manage them from `chrome://dao-agent/skills`
- **Ask AI from the command bar** (Cmd+L) — turn any thought into an agent task without leaving the keyboard
- **Page → Markdown capture** and **share-card generation** baked into the chat surface

→ Full architecture in [docs/features.md § AI Agent System](docs/features.md#2-ai-agent-system) · API in [docs/agent-console-api.md](docs/agent-console-api.md)

### Other Highlights

- **Vertical Tab Sidebar** — Arc-style collapsible sidebar with spaces, favorites, and dual-line active tabs
- **Spotlight Command Bar** — translucent floating command bar with ghost-text completion + Ask AI integration
- **Picture-in-Picture & Split View** — keep content where you need it
- **Calm Minimalism** — chrome recedes so the web page (and the agent) is the focal point

### Platforms

- `macOS` — `arm64` (Apple Silicon)

### Contributing

Dao Browser is an open-source project. Bug reports and feature requests are welcome via [GitHub Issues](https://github.com/msgbyte/dao-browser/issues). To build from source or contribute code, see the [development guide](docs/development.md).

### License

[MIT](LICENSE) — Dao Browser is built on top of [Chromium](https://www.chromium.org/), which is licensed under the [3-Clause BSD License](https://chromium.googlesource.com/chromium/src/+/main/LICENSE).
