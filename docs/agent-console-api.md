# Dao Agent Console API Reference

All commands can be called from the Agent sidebar's DevTools Console (`Cmd+Option+I` on the sidebar → Console tab).

## Quick Start

```js
const bridge = await import('./agent_bridge.js');

// Example: get page info
bridge.executeTool('get_page_info', {}).then(console.log);
```

## Native browser tool catalog

`browser_tool_catalog.json` is the authoritative schema shared by the native
Dao Agent and Local MCP execution paths. Dao Agent exposes all 30 native
browser tools:

```text
get_page_info
get_page_html
get_accessibility_tree
resolve_element_context
capture_screenshot
click_element
agent_click
click_by_ref
move_cursor
highlight_element
scroll_down
scroll_up
scroll_to_element
press_key_chord
type_text
execute_script
list_tabs
switch_tab
open_tab
close_tab
enable_network_tracking
get_network_requests
clear_network_requests
get_network_body
enable_console_tracking
get_console_messages
clear_console_messages
list_page_resources
get_resource_content
search_in_resources
```

Local MCP exposes the same catalog except for the Agent-specific
`resolve_element_context`, for a total of 29 tools. Agent memory, skills,
workspace, provider search/fetch, and other app-level helpers are not MCP
browser tools.

Use `executeTool(name, arguments)` with the catalog names above. The examples
below cover selected tools and older app-level helper calls; consult
`browser_tool_catalog.json` for current native argument and output schemas.

---

## Selected Agent tools (via `executeTool`)

These examples are callable manually for debugging. Some higher-level Agent
helpers in this section are composed in WebUI and are not exposed to MCP.

### get_page_content

Extract `document.body.innerText` from the current page.

```js
bridge.executeTool('get_page_content', {}).then(console.log);
// → { text: "..." }
```

### get_page_info

Get page URL, title, and meta description.

```js
bridge.executeTool('get_page_info', {}).then(console.log);
// → { url: "https://...", title: "...", description: "..." }
```

### get_readable_content

Extract main article content using Mozilla Readability. Best for news/blog/docs.

```js
bridge.executeTool('get_readable_content', {}).then(console.log);
// → { title: "...", byline: "...", excerpt: "...", textContent: "..." }
```

### click_element

Click an element by CSS selector (no visual animation).

```js
bridge.executeTool('click_element', { selector: 'button.submit' });
```

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `selector` | string | yes | CSS selector of the element |

### agent_click

Click with visual cursor animation — pointer moves to element, highlights it, performs CDP click.

```js
bridge.executeTool('agent_click', {
  selector: 'a.nav-link',
  description: 'Click the navigation link'
});
```

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `selector` | string | yes | CSS selector of the element |
| `description` | string | no | Human-readable action description |

### move_cursor

Move the visual cursor to viewport coordinates without clicking.

```js
bridge.executeTool('move_cursor', { x: 200, y: 300 });
```

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `x` | number | yes | Viewport X coordinate |
| `y` | number | yes | Viewport Y coordinate |

### highlight_element

Highlight an element with a purple border overlay (Shadow DOM injection).

```js
bridge.executeTool('highlight_element', { selector: '#main-content' });
```

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `selector` | string | yes | CSS selector of the element |

### execute_script

Run arbitrary JavaScript on the current page via CDP `Runtime.evaluate`.

```js
bridge.executeTool('execute_script', {
  code: 'document.title',
  lock_tab: false
}).then(console.log);
// → { result: "Page Title" }
```

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `code` | string | yes | JavaScript code to execute |
| `lock_tab` | boolean | no | Lock tab input during execution (use `true` for page manipulation) |

### update_soul

Modify the agent's SOUL.md personality prompt.

```js
bridge.executeTool('update_soul', {
  action: 'replace_section',
  section: '## Vibe',
  content: 'Be concise and direct.'
});
```

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `action` | string | yes | `"replace_section"` or `"replace_all"` |
| `section` | string | no | Markdown heading for `replace_section` (e.g. `"## Vibe"`) |
| `content` | string | yes | New content to write |

### save_memory

Save an interaction record to long-term memory.

```js
bridge.executeTool('save_memory', {
  intent: 'Summarize the article',
  outcome: 'Generated a 3-paragraph summary'
});
```

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `intent` | string | yes | What the user wanted to do |
| `outcome` | string | yes | What happened / result |

### save_skill

Save a new slash-command skill.

```js
bridge.executeTool('save_skill', {
  skill_id: 'my-skill',
  skill_md: '---\nname: My Skill\ndescription: Does stuff\nhosts: []\nrequiresPageContent: false\n---\nInstructions here.',
  host: ''
});
```

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `skill_id` | string | yes | Unique ID (lowercase, hyphens) |
| `skill_md` | string | yes | Full SKILL.md with YAML frontmatter |
| `host` | string | yes | Target hostname, empty for global |

---

## Native Bridge (via `callNative` / `callNativeArgs`)

Lower-level calls to C++ WebUI handlers. These are split across three handlers.

### DaoAgentUIHandler (Page interaction)

```js
// With params object:
bridge.callNative('methodName', { key: value });

// With positional args:
bridge.callNativeArgs('methodName', arg1, arg2);
```

| Method | Params | Description |
|--------|--------|-------------|
| `getPageContent` | — | Get `document.body.innerText` via CDP |
| `getPageInfo` | — | Get URL, title, meta description |
| `clickElement` | `{ selector }` | CDP click on element |
| `executeScript` | `{ code, lockTab }` | CDP `Runtime.evaluate` |
| `moveCursor` | `{ x, y }` | Animate cursor to viewport coords |
| `agentClick` | `{ selector, description }` | Full agent click pipeline |
| `highlightElement` | `{ selector }` | Inject highlight overlay |
| `clearHighlight` | — | Remove highlight overlay |

### DaoAgentMemoryHandler (Memory & proactive engine)

| Method | Params | Description |
|--------|--------|-------------|
| `getMemoryContext` | domain, path, url, title | Get relevant memories for context |
| `endSession` | sessionId, summary, domain, path, url, title | End a chat session with summary |
| `loadConversations` | — | Load all conversation records |
| `getPreferences` | — | Get saved user preferences |
| `updatePreference` | preferenceId, value | Update a preference |
| `deleteMemory` | type, id | Delete a memory record |
| `getEpisodes` | — | Load page interaction episodes |
| `clearAllMemory` | — | Wipe all memory data |
| `getStorageStats` | — | Get memory storage statistics |
| `dismissSuggestion` | scenarioId | Dismiss a proactive suggestion |
| `acceptSuggestion` | scenarioId | Accept a proactive suggestion |
| `getMemoryEnabled` | — | Check if memory is enabled |
| `setMemoryEnabled` | enabled (bool) | Toggle memory on/off |
| `setProactiveEnabled` | enabled (bool) | Toggle proactive suggestions |
| `setConfidenceThreshold` | threshold (number) | Set suggestion confidence (0.5-0.85) |
| `recordActionFeedback` | scenarioId, outcome, ... | Record feedback for a scenario |
| `saveEpisode` | domain, path, url, title, intent, outcome, ... | Save a page episode |
| `saveSummary` | sessionId, summary, domain, path, url, title | Save conversation summary |
| `getPageContentForScenario` | — | Get page content for scenario matching |

### DaoAgentSkillHandler (Skill management)

| Method | Params | Description |
|--------|--------|-------------|
| `getSkillRegistry` | — | List all registered skills |
| `getSkillContent` | skillId | Read skill file content |
| `saveUserSkill` | skillId, skillMd, host | Create/update a user skill |
| `deleteUserSkill` | skillId | Delete a user skill |
| `openSkillsDirectory` | — | Open skills folder in Finder (via `chrome.send`) |
| `openSkillManager` | — | Open skill manager tab (via `chrome.send`) |

---

## Agent Stats (via `agent_bridge.ts`)

Track API calls, tool usage, tokens, and estimated cost.

```js
import { getAgentStats, resetAgentStats, recordApiCall, recordToolCall } from './agent_bridge.js';

// View current stats
console.log(getAgentStats());
// → { apiCalls: 12, toolCalls: { get_page_info: 5, agent_click: 3 },
//    promptTokens: 8000, completionTokens: 2000, totalTokens: 10000,
//    estimatedCost: 0.015, lastReset: 1712200000000 }

// Reset all stats
resetAgentStats();

// Manual recording (normally called automatically)
recordApiCall(500, 100);       // promptTokens, completionTokens
recordToolCall('agent_click'); // increment tool counter
```

---

## DevTools Console Debug Examples

```js
const bridge = await import('./agent_bridge.js');

// 1. Check what page the agent sees
const info = await bridge.executeTool('get_page_info', {});
console.log('Current page:', info);

// 2. Test element coordinates (debug agent_click)
const coords = await bridge.callNative('executeScript', {
  code: `(() => {
    const el = document.querySelector('YOUR_SELECTOR');
    if (!el) return JSON.stringify({error: 'not found'});
    const r = el.getBoundingClientRect();
    return JSON.stringify({x: r.x, y: r.y, w: r.width, h: r.height,
      cx: r.left + r.width/2, cy: r.top + r.height/2});
  })()`,
  lockTab: false
});
console.log('Element coords:', JSON.parse(coords.result));

// 3. Test cursor movement
await bridge.executeTool('move_cursor', {x: 100, y: 100});
await bridge.executeTool('move_cursor', {x: 400, y: 300});

// 4. Test highlight → click flow
await bridge.executeTool('highlight_element', {selector: 'a'});
// wait, observe, then:
await bridge.callNative('clearHighlight');

// 5. Full agent_click test
await bridge.executeTool('agent_click', {
  selector: 'a',
  description: 'Test click on first link'
});

// 6. View accumulated stats
console.table(bridge.getAgentStats().toolCalls);
```
