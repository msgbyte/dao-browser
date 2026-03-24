# TODOS

Deferred items from Agent Memory System planning.

## P1 — Next version

- [ ] **Procedural Memory** — Store multi-step workflows the user frequently performs (e.g., "always open DevTools → Network tab on this domain"). Requires a new `procedures` table and an observation pipeline to detect recurring action sequences. Foundation: episode table's domain+path matching.
- [ ] **Scenario Editor** — Let users manually create/edit personal scenarios (URL pattern, page hints, action prompt). Gives power users deterministic control without waiting for the learning pipeline to discover patterns. Foundation: scenarios table from intent prediction.

## P2 — Future

- [ ] **Memory Decay & Consolidation** — Replace FIFO eviction with intelligent decay when episode count exceeds 200+. Use the existing `confidence` field in episodes/preferences as the foundation for decay scoring.
- [ ] **Clear Browsing Data Integration** — Hook into Chromium's "Clear browsing data" flow (BrowsingDataRemover) so that clearing site data also offers to clear Dao agent memories for that domain.
- [ ] **Token Budget Management** — Dynamic context window budgeting for memory injection. Currently the design injects all relevant memories; at scale this will exceed token limits. Needs a scoring/ranking system to select the most relevant memories within a token budget.
- [ ] **Scenario Import/Export** — JSON export/import of personal scenarios for community sharing. Depends on Scenario Editor. Enables open-source network effects where users share scenario configurations (e.g., "Jira sprint analysis" scenario).
