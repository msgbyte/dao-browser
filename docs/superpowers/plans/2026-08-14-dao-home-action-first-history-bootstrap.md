# Dao Home Action-First History Bootstrap Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the explicit browsing-history bootstrap publish an action-first Dao Home with automatically tested live sources after one grouped trusted approval, while keeping history and live payloads ephemeral.

**Architecture:** Replace report-shaped history material with a native typed bootstrap brief, bind it to one visible Home document and Agent turn, and enforce a native bootstrap transaction across provisional connector planning, grouped approval, sequential testing, semantic preview, and one forced `history_bootstrap` publish. Keep generated HTML/CSS/JavaScript flexible, but require `experience.json` plus browser-owned DOM checks for launch actions and tested source slots.

**Tech Stack:** Chromium C++ (`KeyedService`, `SequenceBound`, `base::expected`, `WebContents`, isolated-world JavaScript), Lit/TypeScript trusted WebUI, Vitest, Chromium `browser_tests`, GRIT/XTB localization.

## Global Constraints

- Canonical changes live under `src/dao/`, `src/patches/`, and `docs/`; never edit `engine/` directly.
- `npm run rebuild` is the only compile-confirmation command. After it succeeds, `npm run test:build` may build `browser_tests` only as test preparation; never report that command as compile confirmation. Never run direct Ninja/Siso/GN commands.
- Do not run `i18n.sh`; add English GRIT strings and hand-authored `zh-CN` translations only.
- Never hardcode user-visible copy in C++ or TypeScript.
- Browsing history is ephemeral ranking input only; persisted files and revisions must not contain history titles, visit counts, time buckets, or captured connector samples.
- Automatic history bootstrap proposes at most three connectors and keeps sensitive destinations launch-only.
- One grouped approval produces independent exact connector receipts; a changed module, schema, scope, capability, or budget invalidates only that connector's receipt.
- Permission rejection and connector failure degrade to launch actions; they do not prevent a useful launchpad from publishing.
- Active Home document, visible `WebContents`, Agent turn, base revision, and mutation lease must remain valid at every asynchronous boundary and final store commit.
- Connector refresh and collection stop when Home is hidden or loses ownership; no background refresh survives the active session.
- The final successful flow publishes exactly one visible revision, with native-forced kind `history_bootstrap`.
- Commit commands below are review checkpoints only. Do not execute any state-changing Git command without separate explicit user authorization.

## File Structure

### New files

- `src/dao/browser/home/dao_home_bootstrap_transaction.h` — Native state machine, per-source receipt/test state, final-draft eligibility, and cleanup inventory.
- `src/dao/browser/home/dao_home_bootstrap_transaction.cc` — Transaction transitions and exact state guards.
- `src/dao/browser/home/dao_home_bootstrap_transaction_unittest.cc` — Deterministic transaction, receipt, partial-failure, rejection, and cancellation tests.
- `src/dao/browser/home/dao_home_experience.h` — Typed `experience.json` contract and preview requirements.
- `src/dao/browser/home/dao_home_experience.cc` — Parser and structural validator for start-surface metadata.
- `src/dao/browser/home/dao_home_experience_unittest.cc` — Parser bounds, duplicates, invalid IDs, and connector-slot tests.

### Existing files with focused responsibility changes

- `src/dao/browser/home/dao_home_types.{h,cc}` — Shared bootstrap brief, permission-batch, fingerprint, and test-outcome value types.
- `src/dao/browser/home/dao_home_history_material.{h,cc}` — Convert history rows into ordered launch targets and eligible source candidates without report fields.
- `src/dao/browser/home/dao_home_history_material_unittest.cc` — Privacy boundary and destination-classification tests.
- `src/dao/browser/home/dao_home_manifest.{h,cc}` — Canonical connector fingerprint including module/schema digests and budgets.
- `src/dao/browser/home/dao_home_project_store.{h,cc}` — Read draft connector fingerprints/experience metadata, validate history-bootstrap files, and publish grants atomically.
- `src/dao/browser/home/dao_home_project_store_unittest.cc` — Fingerprint and required experience-file persistence tests.
- `src/dao/browser/home/dao_home_project_service.{h,cc}` — Own the one active bootstrap transaction, pending grouped approval, held decision callback, and teardown cleanup.
- `src/dao/browser/home/dao_home_agent_tools.{h,cc}` — Expose bootstrap-specific tools, record every connector outcome, validate the final draft, and force the publish path.
- `src/dao/browser/ui/webui/dao_agent_ui.{h,cc}` — Propagate history mode/turn ownership and cancel the service transaction when the turn ends.
- `src/dao/browser/ui/webui/dao_home_ui.{h,cc}` — Trusted grouped approval bridge, isolated preview semantics, and owner-loss cancellation.
- `src/dao/browser/ui/webui/resources/agent/home_tools.ts` — Action-first system contract and bootstrap tool schema.
- `src/dao/browser/ui/webui/resources/agent/__tests__/home_tools.test.ts` — Contextual prompt/tool contract tests.
- `src/dao/browser/ui/webui/resources/agent/tool_catalog.ts` — Register the renamed bootstrap tools in the Home group.
- `src/dao/browser/ui/webui/resources/home/home_bridge.ts` — Batch permission types and selected-source reply.
- `src/dao/browser/ui/webui/resources/home/dao_home_app.ts` — Trusted selectable batch dialog and preview semantic request handling.
- `src/dao/browser/ui/webui/resources/home/__tests__/dao_home_app.test.ts` — Grouped approval, deselection, rejection, and preview tests.
- `src/dao/browser/strings/dao_strings.grd` — English grouped-approval and action-first bootstrap strings.
- `src/dao/browser/strings/translations/dao_strings_zh-CN.xtb` — Hand-authored Simplified Chinese translations.
- `src/dao/browser/ui/dao_ui_sources.gni` — Add new native sources and unit tests.
- `src/dao/browser/home/dao_home_browsertest.cc` — Owner-bound batch flow, partial source success, launchpad fallback, one-revision publish, and live-runtime integration.
- `docs/features.md` — Replace the report-shaped history-material contract with the action-first transaction contract.
- `docs/feature-checklist.md` — Add upgrade/regression checks for grouped approval, partial failure, fallback, semantics, and cleanup.

---

### Task 1: Replace History Reports with a Typed Action Brief

**Files:**
- Modify: `src/dao/browser/home/dao_home_types.h:68-197`
- Modify: `src/dao/browser/home/dao_home_types.cc:47-170`
- Modify: `src/dao/browser/home/dao_home_history_material.h:16-21`
- Modify: `src/dao/browser/home/dao_home_history_material.cc:14-105`
- Test: `src/dao/browser/home/dao_home_history_material_unittest.cc:16-100`

**Interfaces:**
- Produces: `HomeBootstrapBrief BuildHomeBootstrapBrief(const history::QueryResults&, base::Time, std::string locale)`.
- Produces: `base::DictValue HomeBootstrapBriefToValue(const HomeBootstrapBrief&)`.
- Produces: `HomeSourceEligibility`, `HomeLaunchTarget`, `HomeSourceCandidate`, and `HomeBootstrapBrief`.
- Consumers: Tasks 2, 4, and 5.

- [ ] **Step 1: Replace the report assertions with privacy and action assertions**

Add fixtures covering GitHub, Bilibili, Linear, Google Translate, Gmail, Feishu, an unknown domain, query strings, and page titles containing secrets. Assert the typed result and serialized value:

```cpp
TEST(DaoHomeHistoryMaterialTest, BuildsActionsWithoutReportMaterial) {
  const base::Time now = base::Time::UnixEpoch() + base::Days(100);
  history::QueryResults results;
  results.SetURLResults({
      Result("https://github.com/private/repo?token=secret", u"Secret PR", now),
      Result("https://www.bilibili.com/video/BV-secret", u"Private title", now),
      Result("https://mail.google.com/mail/u/1/#inbox", u"Inbox", now),
      Result("https://linear.app/acme/issue/DAO-1", u"Roadmap", now),
  });

  HomeBootstrapBrief brief = BuildHomeBootstrapBrief(results, now, "en-US");
  ASSERT_EQ(4u, brief.launch_targets.size());
  EXPECT_EQ("github", brief.launch_targets[0].id);
  EXPECT_EQ(GURL("https://github.com/"), brief.launch_targets[0].url);
  EXPECT_EQ(HomeSourceEligibility::kLaunchAndFeed,
            brief.launch_targets[0].source_eligibility);
  const auto gmail = std::ranges::find_if(
      brief.launch_targets,
      [](const HomeLaunchTarget& target) { return target.id == "gmail"; });
  ASSERT_NE(brief.launch_targets.end(), gmail);
  EXPECT_EQ(HomeSourceEligibility::kSensitiveLaunchOnly,
            gmail->source_eligibility);

  std::string json;
  ASSERT_TRUE(base::JSONWriter::Write(HomeBootstrapBriefToValue(brief), &json));
  EXPECT_EQ(std::string::npos, json.find("visit_count"));
  EXPECT_EQ(std::string::npos, json.find("time_buckets"));
  EXPECT_EQ(std::string::npos, json.find("Secret PR"));
  EXPECT_EQ(std::string::npos, json.find("token"));
}
```

Add a second test asserting that only the top 12 launch targets and top three eligible, diverse source candidates are emitted, with Gmail/Feishu excluded from candidates.

Add an origin-boundary test proving `evilgithub.com` and `github.com.evil.test` do not match the GitHub rule.

- [ ] **Step 2: Run the focused test to capture the old behavior**

Run after the current sources are imported and compiled:

```bash
npm run rebuild
npm run test:build
engine/src/out/dao-debug/browser_tests --gtest_filter='DaoHomeHistoryMaterialTest.*'
```

Expected: FAIL because the current function returns `window_days`, `visit_count`, `titles`, and `time_buckets` instead of typed launch targets.

- [ ] **Step 3: Define the shared action-oriented types**

Add these exact fields to `dao_home_types.h` and default/copy/move definitions to `dao_home_types.cc`:

```cpp
enum class HomeSourceEligibility {
  kLaunchAndFeed,
  kLaunchOnly,
  kSensitiveLaunchOnly,
  kUnsupported,
};

struct HomeLaunchTarget {
  std::string id;
  std::string label_hint;
  GURL url;
  std::string category_hint;
  HomeSourceEligibility source_eligibility =
      HomeSourceEligibility::kUnsupported;
};

struct HomeSourceCandidate {
  std::string launch_target_id;
  std::string connector_kind_hint;
};

struct HomeBootstrapBrief {
  std::vector<HomeLaunchTarget> launch_targets;
  std::vector<HomeSourceCandidate> source_candidates;
  std::string locale;
};
```

Include `url/gurl.h`; do not add counts, titles, timestamps, or raw paths to these types.

- [ ] **Step 4: Implement trusted destination reduction**

Replace `DomainMaterial` output with an internal rank-only accumulator. Add a constexpr trusted destination catalog whose entries define canonical root URL, label, category, eligibility, and connector hint:

```cpp
struct DestinationRule {
  std::string_view host;
  std::string_view id;
  std::string_view label;
  std::string_view root_url;
  std::string_view category;
  HomeSourceEligibility eligibility;
  std::string_view connector_kind;
};
```

The initial catalog must classify:

```cpp
{"github.com", "github", "GitHub", "https://github.com/", "development",
 HomeSourceEligibility::kLaunchAndFeed, "page_feed"}
{"bilibili.com", "bilibili", "Bilibili", "https://www.bilibili.com/", "video",
 HomeSourceEligibility::kLaunchAndFeed, "page_feed"}
{"linear.app", "linear", "Linear", "https://linear.app/", "work",
 HomeSourceEligibility::kLaunchOnly, ""}
{"translate.google.com", "google_translate", "Google Translate",
 "https://translate.google.com/", "utility",
 HomeSourceEligibility::kLaunchOnly, ""}
{"mail.google.com", "gmail", "Gmail", "https://mail.google.com/", "mail",
 HomeSourceEligibility::kSensitiveLaunchOnly, ""}
{"feishu.cn", "feishu", "Feishu", "https://www.feishu.cn/", "messaging",
 HomeSourceEligibility::kSensitiveLaunchOnly, ""}
```

Match catalog hosts by exact host or dot-delimited subdomain boundary, never substring. Use visit frequency only to sort the internal accumulator. Derive unknown labels from a normalized registrable domain, mark them `kUnsupported`, and emit `https://<registrable-domain>/`. Serialize only `launch_targets`, `source_candidates`, and `locale` with snake_case keys. `QueryHistoryAndOpenAgent` passes the current application locale into the reducer; the reducer never infers locale from history rows.

- [ ] **Step 5: Run the reducer tests**

```bash
npm run rebuild
npm run test:build
engine/src/out/dao-debug/browser_tests --gtest_filter='DaoHomeHistoryMaterialTest.*'
```

Expected: PASS; serialized output contains no report fields or private titles.

- [ ] **Step 6: Record the checkpoint, only if Git authorization is later granted**

```bash
git add src/dao/browser/home/dao_home_types.h src/dao/browser/home/dao_home_types.cc src/dao/browser/home/dao_home_history_material.h src/dao/browser/home/dao_home_history_material.cc src/dao/browser/home/dao_home_history_material_unittest.cc
git commit -m "fix(home): generate action-first history brief"
```

### Task 2: Bind the Action-First Contract to the Exact History Turn

**Files:**
- Modify: `src/dao/browser/home/dao_home_project_service.h:126-180`
- Modify: `src/dao/browser/home/dao_home_project_service.cc:18-33,476-533`
- Modify: `src/dao/browser/home/dao_home_agent_tools.cc:140-150,840-849`
- Modify: `src/dao/browser/ui/webui/dao_home_ui.cc:528-599`
- Modify: `src/dao/browser/ui/webui/dao_agent_ui.cc:891-1027`
- Modify: `src/dao/browser/ui/webui/resources/agent/home_tools.ts:7-54,168-194`
- Modify: `src/dao/browser/ui/webui/resources/agent/tool_catalog.ts:89-108`
- Test: `src/dao/browser/ui/webui/resources/agent/__tests__/home_tools.test.ts:15-83`
- Test: `src/dao/browser/home/dao_home_browsertest.cc:505-592`

**Interfaces:**
- Consumes: `HomeBootstrapBrief` and `HomeBootstrapBriefToValue` from Task 1.
- Produces: `HomeToolContext.bootstrapKind?: 'history'`.
- Produces: `BeginHistoryBootstrapBrief(owner, claim_token, turn_id)` semantics through the existing one-shot owner/claim path.
- Produces: Agent tool `home_get_bootstrap_brief`.
- Consumers: Tasks 4 and 5.

- [ ] **Step 1: Add failing Agent contract tests**

Extend `home_tools.test.ts` so ordinary Home turns do not receive history instructions, while a history turn does:

```ts
setHomeToolContext({
  active: true,
  revision: '',
  bootstrapKind: 'history',
});
expect(getHomeSystemPrompt()).toContain('action-first browser start surface');
expect(getHomeSystemPrompt()).toContain('never page content');
expect(getHomeSystemPrompt()).toContain('Do not show visit counts');
expect(getHomeToolDefinitions().map(tool => tool.function.name))
    .toContain('home_get_bootstrap_brief');
expect(getHomeToolDefinitions().map(tool => tool.function.name))
    .not.toContain('home_get_history_material');
```

Update the browser owner test to assert that `homeContext.bootstrapKind` is set only when the exact claim token is successfully claimed by the originating visible Home document.

- [ ] **Step 2: Run the WebUI contract test and observe the failure**

```bash
npm run test:webui -- src/dao/browser/ui/webui/resources/agent/__tests__/home_tools.test.ts
```

Expected: FAIL because `HomeToolContext` has no bootstrap kind and the old report-oriented tool remains registered.

- [ ] **Step 3: Store the typed brief without converting it early**

Rename the service methods and state to make the boundary explicit:

```cpp
void SetHistoryBootstrapBrief(base::WeakPtr<content::WebContents> owner,
                              std::string claim_token,
                              HomeBootstrapBrief brief);
bool ClaimHistoryBootstrap(content::WebContents* owner,
                           const std::string& claim_token,
                           const std::string& agent_turn_id);
base::expected<HomeBootstrapBrief, HomeError> ConsumeHistoryBootstrapBrief(
    const std::string& agent_turn_id);
```

Keep the existing clear-by-owner, claim, and turn operations, renamed to `ClearHistoryBootstrapForOwner`, `ClearHistoryBootstrapForClaim`, and `ClearHistoryBootstrapForTurn`. The state holds `HomeBootstrapBrief`, not `base::DictValue`.

- [ ] **Step 4: Propagate history mode through native turn startup**

In `HandleBeginAgentTurn`, capture the boolean return from `ClaimHistoryBootstrap`. Set:

```cpp
base::DictValue home_context =
    base::DictValue().Set("active", true).Set("revision", std::string());
if (claimed_history) {
  home_context.Set("bootstrapKind", "history");
}
response.Set("homeContext", std::move(home_context));
```

Do not infer history mode from prompt text. A wrong, missing, reused, or cross-document token yields an ordinary Home context.

- [ ] **Step 5: Replace the tool and add the explicit system contract**

Rename the C++ and TypeScript tool to `home_get_bootstrap_brief`. Serialize the typed brief only at tool return. Add a `HISTORY_BOOTSTRAP_CONTRACT` block containing these enforceable instructions:

```text
Use the bootstrap brief only to choose, rank, and group destinations.
Build an action-first browser start surface, not a report about the user.
Do not show visit counts, time buckets, browsing titles, trend charts,
browsing summaries, or productivity judgments.
Put recognizable launch actions in the first viewport and call
dao.navigation.open from project JavaScript.
Only render live content from connectors that the trusted host approved and
tested. Otherwise render a launch-only or disconnected state.
Use each source candidate's launch_target_id as its connector ID.
```

Return `HOME_PROJECT_CONTRACT + HISTORY_BOOTSTRAP_CONTRACT` only for `bootstrapKind === 'history'`.

- [ ] **Step 6: Run focused WebUI and owner tests**

```bash
npm run test:webui -- src/dao/browser/ui/webui/resources/agent/__tests__/home_tools.test.ts
npm run rebuild
npm run test:build
engine/src/out/dao-debug/browser_tests --gtest_filter='DaoHomeBrowserTest.HiddenHomeCannotStartHistoryBootstrap:DaoHomeBrowserTest.HistoryMaterialBelongsToExactOwnerPromptAndAgentTurn'
```

Expected: PASS; only the claimed history turn can consume the brief and receive the action-first contract.

- [ ] **Step 7: Record the checkpoint, only if Git authorization is later granted**

```bash
git add src/dao/browser/home/dao_home_project_service.h src/dao/browser/home/dao_home_project_service.cc src/dao/browser/home/dao_home_agent_tools.cc src/dao/browser/ui/webui/dao_home_ui.cc src/dao/browser/ui/webui/dao_agent_ui.cc src/dao/browser/ui/webui/resources/agent/home_tools.ts src/dao/browser/ui/webui/resources/agent/tool_catalog.ts src/dao/browser/ui/webui/resources/agent/__tests__/home_tools.test.ts src/dao/browser/home/dao_home_browsertest.cc
git commit -m "fix(home): bind bootstrap contract to history turn"
```

### Task 3: Add Exact Connector Receipts and Start-Surface Metadata

**Files:**
- Create: `src/dao/browser/home/dao_home_experience.h`
- Create: `src/dao/browser/home/dao_home_experience.cc`
- Create: `src/dao/browser/home/dao_home_experience_unittest.cc`
- Modify: `src/dao/browser/home/dao_home_types.h:98-197`
- Modify: `src/dao/browser/home/dao_home_manifest.h:21-27`
- Modify: `src/dao/browser/home/dao_home_manifest.cc:235-276`
- Modify: `src/dao/browser/home/dao_home_project_store.h:41-137`
- Modify: `src/dao/browser/home/dao_home_project_store.cc:372-484,633-768`
- Test: `src/dao/browser/home/dao_home_project_store_unittest.cc:199-274`
- Modify: `src/dao/browser/ui/dao_ui_sources.gni:5-21,234-240`

**Interfaces:**
- Produces: `HomeExperience`, `HomePreviewRequirements`, and `ParseHomeExperience(std::string_view)`.
- Produces: `HomeConnectorAuthorization { std::string connector_id; HomeConnectorBundle bundle; std::string fingerprint; }`.
- Produces: `HomeConnectorFingerprint(connector, limits, module_source, schema_source)`.
- Produces: store methods `GetDraftConnectorAuthorizations` and `GetDraftExperience`.
- Consumers: Tasks 4, 5, and 7.

- [ ] **Step 1: Write failing parser and fingerprint tests**

Create `dao_home_experience_unittest.cc` with a valid example and rejection cases for duplicate IDs, more than 12 actions, more than three source slots, invalid identifier syntax, unknown top-level fields, and a non-`start_surface` kind:

```cpp
TEST(DaoHomeExperienceTest, ParsesBoundedStartSurfaceContract) {
  auto result = ParseHomeExperience(R"({
    "kind":"start_surface",
    "primary_actions":["github","linear","bilibili"],
    "source_slots":["github","bilibili"]
  })");
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(3u, result->primary_actions.size());
  EXPECT_EQ(2u, result->source_slots.size());
}
```

Extend the store test to create two drafts with identical permission scopes but different connector module bytes and assert different fingerprints. Repeat for schema, budget, path, and capability changes; assert ordering-only changes to origins/paths produce the same fingerprint.

- [ ] **Step 2: Run the focused native tests and observe missing interfaces**

```bash
npm run rebuild
npm run test:build
engine/src/out/dao-debug/browser_tests --gtest_filter='DaoHomeExperienceTest.*:DaoHomeProjectStoreTest.ConnectorAuthorizationFingerprint*'
```

Expected: FAIL to compile because the experience parser and full connector authorization fingerprint do not exist.

- [ ] **Step 3: Implement strict `experience.json` parsing**

Define:

```cpp
struct HomeExperience {
  std::vector<std::string> primary_actions;
  std::vector<std::string> source_slots;
};

struct HomePreviewRequirements {
  std::optional<HomeExperience> experience;
  std::map<std::string, GURL> launch_urls;
  base::flat_set<std::string> tested_connector_ids;
};

struct HomeConnectorAuthorization {
  std::string connector_id;
  HomeConnectorBundle bundle;
  std::string fingerprint;
};
```

Accept only `kind`, `primary_actions`, and `source_slots`; require `kind == "start_surface"`; accept IDs matching `[a-z0-9][a-z0-9_-]{0,63}`; require unique arrays; cap actions at 12 and source slots at three.

- [ ] **Step 4: Implement the exact authorization fingerprint**

Canonicalize the existing permission fields and budgets, add SHA-256 digests of module and schema bytes, serialize the canonical dictionary, and SHA-256 the final serialized value:

```cpp
std::string HomeConnectorFingerprint(
    const HomeConnector& connector,
    const HomeLimits& limits,
    std::string_view module_source,
    std::string_view schema_source);
```

The canonical dictionary keys are `permission`, `max_result_bytes`, `max_items_per_connector`, `module_sha256`, and `schema_sha256`. Associate the resulting fingerprint with the connector ID, but do not rely on connector ID as the authorization scope.

- [ ] **Step 5: Expose draft authorization and experience reads from the store**

Add:

```cpp
base::expected<std::vector<HomeConnectorAuthorization>, HomeError>
GetDraftConnectorAuthorizations(
    const std::string& draft_id,
    const std::vector<std::string>& connector_ids) const;

base::expected<HomeExperience, HomeError> GetDraftExperience(
    const std::string& draft_id) const;
```

Reject duplicate connector IDs, missing files, invalid schema/module reads, and missing/invalid `experience.json`. Do not persist fingerprints or authorization receipts in project files or exports.

- [ ] **Step 6: Require valid experience metadata only for history-bootstrap publication**

In `PublishInternal`, before moving files, call `GetDraftExperience(draft_id)` when `kind == HomeRevisionKind::kHistoryBootstrap`. Reject the publish with `kInvalidManifest` when missing or invalid. Existing ordinary and imported projects remain compatible.

- [ ] **Step 7: Register the new sources and run native tests**

Add the `.cc` file to `dao_browser_ui_sources` and the unittest to `dao_browser_ui_test_sources`, then run:

```bash
npm run rebuild
npm run test:build
engine/src/out/dao-debug/browser_tests --gtest_filter='DaoHomeExperienceTest.*:DaoHomeProjectStoreTest.ConnectorAuthorizationFingerprint*:DaoHomeProjectStoreTest.HistoryBootstrapRequiresExperience'
```

Expected: PASS.

- [ ] **Step 8: Record the checkpoint, only if Git authorization is later granted**

```bash
git add src/dao/browser/home/dao_home_experience.h src/dao/browser/home/dao_home_experience.cc src/dao/browser/home/dao_home_experience_unittest.cc src/dao/browser/home/dao_home_types.h src/dao/browser/home/dao_home_types.cc src/dao/browser/home/dao_home_manifest.h src/dao/browser/home/dao_home_manifest.cc src/dao/browser/home/dao_home_project_store.h src/dao/browser/home/dao_home_project_store.cc src/dao/browser/home/dao_home_project_store_unittest.cc src/dao/browser/ui/dao_ui_sources.gni
git commit -m "feat(home): add exact bootstrap source receipts"
```

### Task 4: Implement the Native Bootstrap Transaction

**Files:**
- Create: `src/dao/browser/home/dao_home_bootstrap_transaction.h`
- Create: `src/dao/browser/home/dao_home_bootstrap_transaction.cc`
- Create: `src/dao/browser/home/dao_home_bootstrap_transaction_unittest.cc`
- Modify: `src/dao/browser/home/dao_home_types.h:160-197`
- Modify: `src/dao/browser/home/dao_home_types.cc:110-170`
- Modify: `src/dao/browser/ui/dao_ui_sources.gni:5-21,234-240`

**Interfaces:**
- Consumes: action brief from Task 1 and connector authorizations/experience from Task 3.
- Produces: `DaoHomeBootstrapTransaction`, `HomePermissionBatchRequest`, and `HomeConnectorTestOutcome`.
- Consumers: Tasks 5 through 8.

- [ ] **Step 1: Write transaction state tests before the class**

Cover these exact transitions:

```text
planning -> awaiting_batch_approval -> testing_sources
testing_sources -> building_final_home -> previewing -> publishing -> complete
planning -> awaiting_batch_approval -> building_final_home (reject all)
any non-terminal state -> cancelled
```

The main partial-success test uses three candidates, approves all three, records two successes and one `auth_required`, validates a final draft containing only the two successful unchanged fingerprints, and permits publication:

```cpp
TEST(DaoHomeBootstrapTransactionTest, AllowsSuccessfulSubsetAfterTesting) {
  DaoHomeBootstrapTransaction transaction =
      MakeTransaction(ThreeSourceBrief());
  HomePermissionBatchRequest request =
      transaction.PreparePermissionBatch(ProvisionalDraft(),
                                         ThreeAuthorizations()).value();
  ASSERT_TRUE(transaction.ResolvePermissionBatch(
      request.id, {"github", "bilibili", "forum"}).has_value());
  ASSERT_TRUE(transaction.RecordConnectorOutcome(
      Success("github", "github-fingerprint")).has_value());
  ASSERT_TRUE(transaction.RecordConnectorOutcome(
      Success("bilibili", "bilibili-fingerprint")).has_value());
  ASSERT_TRUE(transaction.RecordConnectorOutcome(
      Failure("forum", "forum-fingerprint", "auth_required")).has_value());

  auto requirements = transaction.BindFinalDraft(
      FinalDraftWith({"github", "bilibili"}),
      FinalAuthorizations({"github", "bilibili"}),
      Experience({"github", "bilibili", "forum"},
                 {"github", "bilibili"}));
  ASSERT_TRUE(requirements.has_value());
  EXPECT_THAT(requirements->tested_connector_ids,
              testing::UnorderedElementsAre("github", "bilibili"));
}
```

Define `MakeTransaction`, `ThreeSourceBrief`, `ProvisionalDraft`, `ThreeAuthorizations`, `Success`, `Failure`, `FinalDraftWith`, `FinalAuthorizations`, and `Experience` in the same test fixture with complete values. Add negative tests for a changed module fingerprint, an untested connector in the final manifest, a source slot for the failed connector, a different base revision, and a different Agent turn.

Also assert that `BindFinalDraft` rejects an experience whose action IDs are not in the brief or which omits any of the first `min(4, launch_targets.size())` ranked targets. Each source slot ID must equal a successfully tested connector ID.

- [ ] **Step 2: Run the missing-class test**

```bash
npm run rebuild
npm run test:build
engine/src/out/dao-debug/browser_tests --gtest_filter='DaoHomeBootstrapTransactionTest.*'
```

Expected: FAIL to compile because the transaction types are absent.

- [ ] **Step 3: Define bounded shared request and outcome types**

Use these shapes:

```cpp
enum class HomeBootstrapState {
  kPlanning,
  kAwaitingBatchApproval,
  kTestingSources,
  kBuildingFinalHome,
  kPreviewing,
  kPublishing,
  kComplete,
  kCancelled,
  kFailed,
};

struct HomePermissionBatchItem {
  std::string connector_id;
  std::string label;
  HomeConnector connector;
  std::optional<HomeLimits> previous_limits;
  HomeLimits requested_limits;
  std::string fingerprint;
  bool authentication_may_be_required = false;
};

struct HomePermissionBatchRequest {
  std::string id;
  std::string transaction_id;
  std::string draft_id;
  std::string base_revision;
  std::vector<HomePermissionBatchItem> items;
};

enum class HomeConnectorTestStatus {
  kApproved,
  kSucceeded,
  kAuthenticationRequired,
  kRuntimeFailed,
  kSchemaFailed,
  kDeselected,
};

struct HomeConnectorTestOutcome {
  std::string connector_id;
  std::string fingerprint;
  HomeConnectorTestStatus status;
  std::optional<base::Value> sample;
  std::string error_code;
};
```

Cap request items at three and keep samples bounded by the connector executor's existing result budget.

- [ ] **Step 4: Implement guarded transaction methods**

Expose exact transition methods:

```cpp
DaoHomeBootstrapTransaction(
    std::string id,
    std::string agent_turn_id,
    base::WeakPtr<content::WebContents> owner,
    std::string base_revision,
    HomeBootstrapBrief brief,
    scoped_refptr<DaoHomeMutationLease> turn_authorization,
    base::RepeatingCallback<bool()> owner_validator);
base::expected<HomePermissionBatchRequest, HomeError> PreparePermissionBatch(
    const HomeDraft& provisional_draft,
    std::vector<HomeConnectorAuthorization> authorizations);
base::expected<void, HomeError> ResolvePermissionBatch(
    const std::string& request_id,
    const base::flat_set<std::string>& selected_connector_ids);
base::expected<void, HomeError> RecordConnectorOutcome(
    HomeConnectorTestOutcome outcome);
base::expected<HomePreviewRequirements, HomeError> BindFinalDraft(
    const HomeDraft& final_draft,
    std::vector<HomeConnectorAuthorization> authorizations,
    HomeExperience experience);
base::expected<void, HomeError> MarkPreviewed(const std::string& draft_id);
base::expected<void, HomeError> BeginPublish(const std::string& draft_id);
void MarkPublished();
std::vector<std::string> Cancel();
```

Every method checks transaction ID/turn identity internally, current state, base revision, and exact fingerprints. `Cancel()` returns all provisional/final draft IDs that are not the published final revision so the service can discard them asynchronously.

`PreparePermissionBatch` accepts only connector IDs present in `brief.source_candidates`, and each provisional connector ID must exactly equal its `launch_target_id`. `BindFinalDraft` fills `HomePreviewRequirements.launch_urls` from the canonical brief, not from generated project data.

- [ ] **Step 5: Run all transaction tests**

```bash
npm run rebuild
npm run test:build
engine/src/out/dao-debug/browser_tests --gtest_filter='DaoHomeBootstrapTransactionTest.*'
```

Expected: PASS, including partial failure, reject-all continuation, tamper rejection, and idempotent cancellation.

- [ ] **Step 6: Record the checkpoint, only if Git authorization is later granted**

```bash
git add src/dao/browser/home/dao_home_bootstrap_transaction.h src/dao/browser/home/dao_home_bootstrap_transaction.cc src/dao/browser/home/dao_home_bootstrap_transaction_unittest.cc src/dao/browser/home/dao_home_types.h src/dao/browser/home/dao_home_types.cc src/dao/browser/ui/dao_ui_sources.gni
git commit -m "feat(home): add history bootstrap transaction"
```

### Task 5: Orchestrate Grouped Approval and Sequential Tests in the Service and Agent Tools

**Files:**
- Modify: `src/dao/browser/home/dao_home_project_service.h:28-180`
- Modify: `src/dao/browser/home/dao_home_project_service.cc:34-553`
- Modify: `src/dao/browser/home/dao_home_agent_tools.h:23-63`
- Modify: `src/dao/browser/home/dao_home_agent_tools.cc:140-160,180-210,400-549,591-646,734-849`
- Modify: `src/dao/browser/ui/webui/dao_agent_ui.cc:650-694,968-1027,1053-1183`
- Modify: `src/dao/browser/ui/webui/resources/agent/home_tools.ts:70-194`
- Modify: `src/dao/browser/ui/webui/resources/agent/tool_catalog.ts:89-108`
- Test: `src/dao/browser/ui/webui/resources/agent/__tests__/home_tools.test.ts`
- Test: `src/dao/browser/home/dao_home_browsertest.cc`

**Interfaces:**
- Consumes: transaction from Task 4 and store reads from Task 3.
- Produces: `RequestBootstrapPermissions`, `ResolveBootstrapPermissions`, `PrepareBootstrapPreview`, `PublishBootstrapDraft`, and cancellation-by-owner/turn.
- Produces: Agent tool `home_request_bootstrap_sources` whose callback remains pending until the trusted decision.
- Consumers: Tasks 6 through 8.

- [ ] **Step 1: Add failing service-flow browser tests**

Use `DaoHomeAgentTools` with a held callback to prove:

1. `home_get_bootstrap_brief` starts one transaction for the claimed turn.
2. `home_request_bootstrap_sources` creates one batch and does not resolve its Agent callback immediately.
3. Resolving the batch with two selected IDs returns exactly those IDs to the held callback.
4. A second connector test cannot start while one is active.
5. Success and `auth_required` both become terminal per-source outcomes.
6. Supplying `kind: "initial"` during the history transaction still publishes as `history_bootstrap`.
7. Supplying `kind: "history_bootstrap"` outside a history transaction is rejected.

- [ ] **Step 2: Add the batch tool schema and run focused WebUI tests**

Extend the `Property` type to support `items`, `minItems`, and `maxItems`. Define:

```ts
definition(
  'home_request_bootstrap_sources',
  'Request one trusted grouped decision for up to three provisional history-bootstrap connectors. The tool returns only after the user decides.',
  {
    base_revision: BASE_REVISION,
    draft_id: {type: 'string'},
    connector_ids: {
      type: 'array',
      items: {type: 'string'},
      minItems: 1,
      maxItems: 3,
    },
  },
  ['base_revision', 'draft_id', 'connector_ids'],
)
```

Assert the tool is present only for history bootstrap context. Run:

```bash
npm run test:webui -- src/dao/browser/ui/webui/resources/agent/__tests__/home_tools.test.ts
```

Expected before implementation: FAIL because the schema type and tool do not exist.

- [ ] **Step 3: Make the project service own one active transaction**

Add these service methods:

```cpp
base::expected<HomeBootstrapBrief, HomeError> BeginHistoryBootstrap(
    const std::string& agent_turn_id,
    const std::string& base_revision,
    scoped_refptr<DaoHomeMutationLease> turn_authorization,
    base::RepeatingCallback<bool()> owner_validator);

using BootstrapDecisionCallback =
    base::OnceCallback<void(base::expected<base::flat_set<std::string>, HomeError>)>;

void RequestBootstrapPermissions(
    const std::string& agent_turn_id,
    const HomeDraft& draft,
    std::vector<std::string> connector_ids,
    BootstrapDecisionCallback callback);
void ResolveBootstrapPermissions(
    content::WebContents* owner,
    const std::string& request_id,
    base::flat_set<std::string> selected_connector_ids,
    ResultCallback<void> callback);
std::optional<HomePermissionBatchRequest> GetPendingBootstrapPermission(
    content::WebContents* owner) const;
base::CallbackListSubscription AddBootstrapPermissionObserver(
    base::RepeatingClosure callback);
void PrepareBootstrapPreview(
    const std::string& agent_turn_id,
    const HomeDraft& draft,
    ResultCallback<HomePreviewRequirements> callback);
void PublishBootstrapDraft(
    const std::string& agent_turn_id,
    const HomeDraft& draft,
    ResultCallback<HomeVersion> callback,
    scoped_refptr<DaoHomeMutationLease> authorization);
void CancelHistoryBootstrapForOwner(content::WebContents* owner);
void CancelHistoryBootstrapForTurn(const std::string& agent_turn_id);
```

The service retrieves connector authorizations on `store_`, creates the batch, and stores the decision callback until trusted UI resolution. The observer carries no permission payload; each Home handler calls `GetPendingBootstrapPermission(web_ui()->GetWebContents())`, which returns a value only for the exact transaction owner. On cancellation the service runs the held callback once with `kCancelled`, discards every transaction-owned draft through `store_`, clears samples/receipts, and notifies the batch observer.

Allow at most one permission surface per Profile: reject a bootstrap batch while a singular request is pending, reject a singular request while a bootstrap batch is pending, and clear the matching observer state before invoking either decision callback.

- [ ] **Step 4: Gate Agent tools on transaction state**

Implement these rules in `DaoHomeAgentTools`:

- `home_get_bootstrap_brief` calls `BeginHistoryBootstrap`; a second call returns the same active brief for the same turn without re-reading history.
- `home_request_bootstrap_sources` validates the exact current base revision, draft, 1–3 unique connector IDs, and active history turn, then holds its callback until service resolution.
- `home_test_connector` records success or structured failure in the transaction. Only one test runs at a time.
- `home_preview` obtains `HomePreviewRequirements` from the service before invoking the browser preview runner.
- `home_publish` calls `PublishBootstrapDraft` whenever the exact Agent turn owns a history transaction; it ignores the model-supplied kind and forces native `kHistoryBootstrap`.
- Outside a history transaction, `kHistoryBootstrap` is not accepted from model arguments.

Extend the history system contract with the exact automatic sequence so the Agent does not end the turn after requesting access:

```text
First create a provisional draft containing launch actions and up to three
candidate connectors. Call home_request_bootstrap_sources once and wait for
its trusted decision. Test each returned connector exactly once, sequentially.
Then create a new final draft from the published base revision: keep every
launch action, include only successful unchanged connectors, add
experience.json, preview, and publish. Continue to a launchpad-only final draft
when the selected connector list is empty or every connector test fails.
Do not ask the user to send another chat message to continue this sequence.
```

- [ ] **Step 5: Preserve the turn-level lease across the transaction**

Pass `home_turn_authorization_` separately from per-tool child leases when starting the bootstrap. Store only the turn-level child in the transaction; each individual tool still uses its own callback lease. `AbortAgentTurn`, visibility loss, target replacement, and end-turn call `CancelHistoryBootstrapForTurn` before clearing the active turn ID.

- [ ] **Step 6: Run service-flow tests**

```bash
npm run rebuild
npm run test:build
engine/src/out/dao-debug/browser_tests --gtest_filter='DaoHomeBrowserTest.HistoryBootstrap*'
```

Expected: PASS; the batch callback waits for the trusted decision, tests are sequential, and publication kind is native-forced.

- [ ] **Step 7: Record the checkpoint, only if Git authorization is later granted**

```bash
git add src/dao/browser/home/dao_home_project_service.h src/dao/browser/home/dao_home_project_service.cc src/dao/browser/home/dao_home_agent_tools.h src/dao/browser/home/dao_home_agent_tools.cc src/dao/browser/ui/webui/dao_agent_ui.cc src/dao/browser/ui/webui/resources/agent/home_tools.ts src/dao/browser/ui/webui/resources/agent/tool_catalog.ts src/dao/browser/ui/webui/resources/agent/__tests__/home_tools.test.ts src/dao/browser/home/dao_home_browsertest.cc
git commit -m "feat(home): orchestrate automatic bootstrap sources"
```

### Task 6: Build the Trusted Grouped Permission Dialog

**Files:**
- Modify: `src/dao/browser/ui/webui/dao_home_ui.h:92-161`
- Modify: `src/dao/browser/ui/webui/dao_home_ui.cc:112-147,220-311,461-511,1401-1465`
- Modify: `src/dao/browser/ui/webui/resources/home/home_bridge.ts:33-49,112-122`
- Modify: `src/dao/browser/ui/webui/resources/home/dao_home_app.ts:8-38,67-94,430-507,760-963`
- Test: `src/dao/browser/ui/webui/resources/home/__tests__/dao_home_app.test.ts:9-98,310-378`
- Modify: `src/dao/browser/strings/dao_strings.grd:560-614`
- Modify: `src/dao/browser/strings/translations/dao_strings_zh-CN.xtb`

**Interfaces:**
- Consumes: `HomePermissionBatchRequest` and service resolution from Tasks 4 and 5.
- Produces: trusted selected-ID decision and one grouped approval UI.
- Consumers: Task 8 end-to-end tests.

- [ ] **Step 1: Add failing selectable-batch WebUI tests**

Mock a request with GitHub, Bilibili, and a third source. Assert all exact origins, paths, capabilities, and per-row budgets are visible. Deselect Bilibili, confirm once, and assert:

```ts
expect(bridge.resolveHomeBootstrapPermission).toHaveBeenCalledWith(
    'batch-1', ['github', 'forum']);
```

Add a reject test asserting the secondary action calls the same bridge with an empty array and removes the dialog, allowing the Agent tool to resume with launchpad-only output.

- [ ] **Step 2: Run the focused WebUI test and observe the failure**

```bash
npm run test:webui -- src/dao/browser/ui/webui/resources/home/__tests__/dao_home_app.test.ts
```

Expected: FAIL because the trusted host supports only one connector per permission request.

- [ ] **Step 3: Add a discriminated permission union to the bridge**

Define:

```ts
export interface HomePermissionBatchItem {
  connectorId: string;
  label: string;
  origins: string[];
  paths: string[];
  capabilities: string[];
  mode: 'read';
  previousLimits?: HomeResourceLimits;
  requestedLimits: HomeResourceLimits;
  authenticationMayBeRequired: boolean;
}

export interface HomePermissionBatchRequest {
  kind: 'batch';
  id: string;
  draftId: string;
  baseRevision: string;
  items: HomePermissionBatchItem[];
}

export function resolveHomeBootstrapPermission(
    requestId: string, selectedConnectorIds: string[]): Promise<unknown>;
```

Add `kind: 'single'` to the existing request so normal later connector edits remain compatible.

- [ ] **Step 4: Serialize and owner-bind the native batch request**

Register `resolveHomeBootstrapPermission`. `HandleGetPermission` and the no-payload batch observer query `GetPendingBootstrapPermission(web_ui()->GetWebContents())`, so another Home window never receives the batch contents. Reject malformed IDs, more than three selections, duplicates, selections not present in the pending batch, inactive/hidden callers, and callers whose `WebContents` is not the transaction owner. Pass the selected set to `ResolveBootstrapPermissions` and recheck `HasActiveHomeOwner()` in its completion callback.

- [ ] **Step 5: Render one accessible selectable dialog**

Store selected IDs in a `Set<string>` initialized from all request items. Render each source as a checkbox row with label, origin, paths, localized capabilities, authentication hint, lifecycle, no-write statement, and exact budget diff. The primary action submits selected IDs once; the secondary action submits `[]`.

Add these exact English GRIT source strings:

```text
IDS_DAO_HOME_CONNECT_SOURCES_TITLE = Connect live sources?
IDS_DAO_HOME_CONNECT_SOURCES_DESCRIPTION = Dao will test the selected sources and add the ones that work. Each source can read only the pages and data shown below while Home is active.
IDS_DAO_HOME_AUTHENTICATION_MAY_BE_REQUIRED = May require you to be signed in
IDS_DAO_HOME_CONNECT_SELECTED = Connect selected
IDS_DAO_HOME_CONTINUE_WITHOUT_SOURCES = Continue without live sources
```

Add these hand-authored `zh-CN` translations after computing each GRIT message ID with the in-tree `GenerateMessageId` helper; do not invoke the translation script:

```text
连接实时来源？
Dao 会测试所选来源，并把可用的来源添加到主页。每个来源只能在主页处于打开状态时读取下方显示的页面和数据。
可能需要你先登录
连接所选来源
暂不连接，继续创建
```

- [ ] **Step 6: Run WebUI and Lit checks**

```bash
npm run test:webui -- src/dao/browser/ui/webui/resources/home/__tests__/dao_home_app.test.ts
npm run lint:lit
```

Expected: PASS; one dialog supports exact per-source review, deselection, confirm, and reject-all continuation.

- [ ] **Step 7: Record the checkpoint, only if Git authorization is later granted**

```bash
git add src/dao/browser/ui/webui/dao_home_ui.h src/dao/browser/ui/webui/dao_home_ui.cc src/dao/browser/ui/webui/resources/home/home_bridge.ts src/dao/browser/ui/webui/resources/home/dao_home_app.ts src/dao/browser/ui/webui/resources/home/__tests__/dao_home_app.test.ts src/dao/browser/strings/dao_strings.grd src/dao/browser/strings/translations/dao_strings_zh-CN.xtb
git commit -m "feat(home): group bootstrap source approval"
```

### Task 7: Enforce Browser-Owned Start-Surface Preview Semantics

**Files:**
- Modify: `src/dao/browser/home/dao_home_project_store.h:122-137`
- Modify: `src/dao/browser/home/dao_home_project_store.cc:433-484`
- Modify: `src/dao/browser/home/dao_home_agent_tools.h:27-32`
- Modify: `src/dao/browser/home/dao_home_agent_tools.cc:400-475`
- Modify: `src/dao/browser/ui/webui/dao_home_ui.h:65-161`
- Modify: `src/dao/browser/ui/webui/dao_home_ui.cc:345-381,1070-1142,1341-1381`
- Modify: `src/dao/browser/ui/webui/resources/home/dao_home_app.ts:45-49,547-573,1024-1035`
- Test: `src/dao/browser/ui/webui/resources/home/__tests__/dao_home_app.test.ts:495-583`
- Test: `src/dao/browser/home/dao_home_browsertest.cc`

**Interfaces:**
- Consumes: `HomePreviewRequirements` from Task 3 and final-draft transaction validation from Task 5.
- Produces: a browser-owned semantic preview verdict and preview receipt.
- Consumers: Task 8 final publish tests.

- [ ] **Step 1: Add failing semantic preview cases**

Add native/browser tests for:

- Four declared focusable `[data-dao-action]` elements and two tested `[data-dao-source-slot]` elements pass.
- A report page with `visit_count`, `time_buckets`, and trend markup is rejected before preview.
- A declared action missing from the DOM fails.
- A non-focusable action marker fails.
- A source slot whose `data-dao-connector` is not in `tested_connector_ids` fails.
- A syntactically valid app that throws at top level remains rejected by the existing console/load guard.

- [ ] **Step 2: Run the semantic tests and observe the failure**

```bash
npm run rebuild
npm run test:build
engine/src/out/dao-debug/browser_tests --gtest_filter='DaoHomeBrowserTest.HistoryBootstrapPreview*'
```

Expected: FAIL because preview currently validates load/runtime errors only.

- [ ] **Step 3: Reject known report-shaped persistence before loading**

Add `ValidateHistoryBootstrapFiles(draft_id)` on the store. Enumerate text project files within existing project limits and reject exact legacy/bootstrap-report keys `visit_count`, `time_buckets`, `window_days`, and a persisted `titles` field in structured JSON. Do not reject arbitrary numbers or prose unrelated to those known inputs. Call this validation from `PrepareBootstrapPreview` and again inside `PublishBootstrapDraft`.

- [ ] **Step 4: Pass preview requirements to the trusted preview host**

Change the runner signature to:

```cpp
using PreviewRunner = base::RepeatingCallback<void(
    std::string draft_id,
    std::string entry,
    HomePreviewRequirements requirements,
    Callback callback)>;
```

Keep the requirements in `DaoHomeUIHandler` beside the exact preview draft/frame ID. Ordinary previews carry `experience = std::nullopt`; history bootstrap previews require it.

- [ ] **Step 5: Inspect the loaded DOM from a browser-owned isolated world**

After the expected frame commits and the settle timer fires, execute a fixed native-owned script in the exact `RenderFrameHost` identified by `agent_preview_frame_tree_node_id_`. Return only:

```js
({
  actions: [...document.querySelectorAll('[data-dao-action]')].map(node => ({
    id: node.getAttribute('data-dao-action'),
    url: node.getAttribute('data-dao-action-url'),
    focusable: node.matches('button:not([disabled]), [tabindex]:not([tabindex="-1"])'),
    visible: (() => {
      const rect = node.getBoundingClientRect();
      const style = getComputedStyle(node);
      return rect.width > 0 && rect.height > 0 && rect.bottom > 0 &&
          rect.top < innerHeight && style.visibility !== 'hidden' &&
          style.display !== 'none';
    })(),
  })),
  sourceSlots: [...document.querySelectorAll('[data-dao-source-slot]')].map(node => ({
    id: node.getAttribute('data-dao-source-slot'),
    connectorId: node.getAttribute('data-dao-connector'),
  })),
})
```

Compare it in native code against `HomeExperience` and `HomePreviewRequirements`: every declared action and source slot appears exactly once; each action's `data-dao-action-url` exactly matches the canonical URL from `launch_urls`; at least `min(4, primary_actions.size())` actions are focusable and visible in the first viewport; every source slot's connector is in `tested_connector_ids`. Generated code cannot provide or overwrite the verdict.

- [ ] **Step 6: Mark previewed only after semantics succeed**

Move `MarkDraftPreviewed` after the semantic result callback. On mismatch, end the draft preview and return `{code: "invalid_experience", error: ...}` without a preview receipt. Preserve the current runtime error, navigation replacement, owner, timeout, and frame-ID guards.

- [ ] **Step 7: Run semantic and WebUI preview tests**

```bash
npm run test:webui -- src/dao/browser/ui/webui/resources/home/__tests__/dao_home_app.test.ts
npm run rebuild
npm run test:build
engine/src/out/dao-debug/browser_tests --gtest_filter='DaoHomeBrowserTest.HistoryBootstrapPreview*:DaoHomeBrowserTest.AgentPreview*'
```

Expected: PASS; report-shaped or semantically false projects cannot reach publish.

- [ ] **Step 8: Record the checkpoint, only if Git authorization is later granted**

```bash
git add src/dao/browser/home/dao_home_project_store.h src/dao/browser/home/dao_home_project_store.cc src/dao/browser/home/dao_home_agent_tools.h src/dao/browser/home/dao_home_agent_tools.cc src/dao/browser/ui/webui/dao_home_ui.h src/dao/browser/ui/webui/dao_home_ui.cc src/dao/browser/ui/webui/resources/home/dao_home_app.ts src/dao/browser/ui/webui/resources/home/__tests__/dao_home_app.test.ts src/dao/browser/home/dao_home_browsertest.cc
git commit -m "feat(home): validate start-surface previews"
```

### Task 8: Complete Best-Effort Publishing and Lifecycle Cleanup

**Files:**
- Modify: `src/dao/browser/home/dao_home_project_service.cc:228-337,447-533`
- Modify: `src/dao/browser/ui/webui/dao_agent_ui.cc:650-670,679-694,1186-1210`
- Modify: `src/dao/browser/ui/webui/dao_home_ui.cc:1144-1195,1291-1399`
- Test: `src/dao/browser/home/dao_home_bootstrap_transaction_unittest.cc`
- Test: `src/dao/browser/home/dao_home_browsertest.cc`
- Test: `src/dao/browser/ui/webui/resources/home/__tests__/dao_home_app.test.ts`

**Interfaces:**
- Consumes: service, transaction, trusted approval, and semantic preview interfaces from Tasks 4–7.
- Produces: complete best-effort behavior and teardown guarantees.

- [ ] **Step 1: Add end-to-end state tests for all terminal paths**

Implement deterministic tests without an external LLM:

1. Empty Home + rejected batch -> final project has four launch actions, zero connectors/source slots, one `history_bootstrap` version.
2. Three proposed + two successful + one `auth_required` -> final project has all three launch actions, two connectors/source slots, and one version.
3. All connectors fail -> launchpad publishes with zero live slots.
4. Final connector module changes after approval -> preview/publish returns `permission_required` and current head stays empty.
5. Home hides while approval is pending -> held Agent callback returns `cancelled`, all temporary drafts disappear, and no revision is created.
6. Agent turn ends during connector testing -> executor/session is cancelled, receipts/samples are cleared, and no revision is created.
7. Project base revision changes before final commit -> transaction fails stale and cannot overwrite the new head.
8. Clicking every declared launch action sends `navigation.open` through the fixed runtime and opens the trusted navigation dialog; the generated DOM contains no direct external anchor or form target.
9. A published live slot renders a unique value supplied only by the approved connector result, and that value is absent from every revision file and exported package.

Add a `HistoryBootstrapHarness` in `dao_home_browsertest.cc` with these concrete operations so every test drives the same native contract rather than duplicating private service mutation:

```cpp
class HistoryBootstrapHarness {
 public:
  explicit HistoryBootstrapHarness(DaoHomeBrowserTest* test);
  HomeBootstrapBrief Start(const std::string& turn_id);
  HomeDraft ApplyProvisionalPatch(const std::string& patch);
  HomePermissionBatchRequest RequestSources(
      const HomeDraft& draft,
      std::vector<std::string> connector_ids);
  void ResolveSources(const std::string& request_id,
                      base::flat_set<std::string> selected_ids);
  base::Value TestSource(const HomeDraft& draft,
                         const std::string& connector_id,
                         base::Value result);
  HomeDraft ApplyFinalPatch(const std::string& patch);
  base::expected<HomeVersion, HomeError> PreviewAndPublish(
      const HomeDraft& draft);
  HomeSnapshot Snapshot();
  std::vector<HomeVersion> Versions();
};
```

The partial-success test must finish with storage assertions, not only tool responses:

```cpp
IN_PROC_BROWSER_TEST_F(DaoHomeBrowserTest,
                       HistoryBootstrapEndToEndKeepsSuccessfulSubset) {
  HistoryBootstrapHarness flow(this);
  flow.Start("history-turn");
  HomeDraft provisional = flow.ApplyProvisionalPatch(kThreeSourcePlanPatch);
  HomePermissionBatchRequest request = flow.RequestSources(
      provisional, {"github", "bilibili", "forum"});
  flow.ResolveSources(request.id, {"github", "bilibili", "forum"});
  flow.TestSource(provisional, "github", Sample("GitHub item"));
  flow.TestSource(provisional, "bilibili", Sample("Bilibili item"));
  flow.TestSource(provisional, "forum", Error("auth_required"));

  HomeDraft final_draft = flow.ApplyFinalPatch(kTwoSourceStartSurfacePatch);
  auto version = flow.PreviewAndPublish(final_draft);
  ASSERT_TRUE(version.has_value());
  EXPECT_EQ(HomeRevisionKind::kHistoryBootstrap, version->kind);
  EXPECT_EQ(1u, flow.Versions().size());
  EXPECT_THAT(flow.Snapshot().granted_connector_ids,
              testing::UnorderedElementsAre("github", "bilibili"));
}
```

Implement `Snapshot()` and `Versions()` with `base::test::TestFuture` over the existing asynchronous service methods; do not add production-only synchronous APIs for the test.

- [ ] **Step 2: Run the new tests and capture lifecycle gaps**

```bash
npm run rebuild
npm run test:build
engine/src/out/dao-debug/browser_tests --gtest_filter='DaoHomeBrowserTest.HistoryBootstrapEndToEnd*:DaoHomeBootstrapTransactionTest.*Cleanup*'
```

Expected before cleanup wiring: at least the hidden-owner and abandoned-draft cases fail.

- [ ] **Step 3: Centralize service-owned cleanup**

Add one `FinishOrCancelBootstrap` helper that:

- Takes the transaction out of the service before running callbacks.
- Resolves a pending decision callback exactly once.
- Clears pending permission state and notifies observers.
- Calls `DiscardDraft` for every unpublished transaction-owned draft with a callback independent of `DaoHomeUIHandler` lifetime.
- Clears samples, receipt fingerprints, and selected/tested state.
- Leaves the successfully published final revision untouched.

- [ ] **Step 4: Wire every owner-loss path**

Call cancellation from:

- `DaoHomeUIHandler::PrimaryPageChanged`.
- `DaoHomeUIHandler::OnVisibilityChanged` when not visible.
- `DaoHomeUIHandler::WebContentsDestroyed`.
- `DaoHomeUIHandler::HandleCancelSession`.
- `DaoAgentUIHandler::AbortAgentTurn`.
- `DaoAgentUIHandler::HandleEndAgentTurn`.
- Replacement turn startup before acquiring the next turn.
- `NotifyProjectChanged` when the change is not the transaction's own successful final publish.

Retain the existing mutation lease invalidation and connector/preview cancellation in all of these paths.

- [ ] **Step 5: Ensure partial outcomes produce explicit final guidance**

Return structured test results to the Agent with `status` equal to `succeeded`, `authentication_required`, `runtime_failed`, or `schema_failed`. Update the history contract to require that the final patch omits failed connector definitions, preserves their launch actions, and uses a disconnected/authentication state only when it is clearly labeled and contains no fake sample content.

- [ ] **Step 6: Run lifecycle, WebUI, and transaction tests**

```bash
npm run test:webui -- src/dao/browser/ui/webui/resources/home/__tests__/dao_home_app.test.ts src/dao/browser/ui/webui/resources/agent/__tests__/home_tools.test.ts
npm run rebuild
npm run test:build
engine/src/out/dao-debug/browser_tests --gtest_filter='DaoHomeBrowserTest.HistoryBootstrap*:DaoHomeBootstrapTransactionTest.*'
```

Expected: PASS; every rejected/failed source degrades independently, and every owner-loss path leaves no draft, receipt, sample, executor, or revision behind.

- [ ] **Step 7: Record the checkpoint, only if Git authorization is later granted**

```bash
git add src/dao/browser/home/dao_home_project_service.cc src/dao/browser/ui/webui/dao_agent_ui.cc src/dao/browser/ui/webui/dao_home_ui.cc src/dao/browser/home/dao_home_bootstrap_transaction_unittest.cc src/dao/browser/home/dao_home_browsertest.cc src/dao/browser/ui/webui/resources/home/__tests__/dao_home_app.test.ts
git commit -m "fix(home): clean up cancelled bootstrap flows"
```

### Task 9: Document the Feature and Run Final Verification

**Files:**
- Modify: `docs/features.md:291-370`
- Modify: `docs/feature-checklist.md:130-140`
- Verify: all files changed in Tasks 1–8.

**Interfaces:**
- Consumes: completed behavior from Tasks 1–8.
- Produces: upgrade documentation and final verification evidence.

- [ ] **Step 1: Update the desktop feature inventory**

Replace the current history-bootstrap paragraph with exact behavior:

- Native brief contains ordered launch targets and at most three eligible source candidates, without titles/counts/time buckets.
- History is ephemeral and one-turn/document bound.
- One grouped trusted dialog allows per-source deselection and creates independent exact receipts.
- Approved sources test sequentially; failures degrade to launch actions.
- Browser-owned semantic preview requires action/source markers and rejects report-shaped legacy fields.
- Native code forces one final `history_bootstrap` revision and cleans all temporary state on owner loss.

- [ ] **Step 2: Update the upgrade/regression checklist**

Add checks for:

```text
empty Home -> action-first launchpad
one grouped approval -> automatic continuation
per-source deselection
two successes plus one auth failure
reject all -> launchpad-only publish
history fields absent from export/version files
connector fingerprint tampering rejected
hidden/navigation/turn-end cancellation cleanup
exactly one final history_bootstrap revision
live slots populated only by approved tested session connectors
```

- [ ] **Step 3: Run static and WebUI verification**

```bash
npm run test:webui
npm run lint:lit
npm run docs:check
git diff --check
```

Expected: all commands exit 0. Record any unrelated pre-existing suite failure separately and do not promote partial evidence to a full pass.

- [ ] **Step 4: Import canonical sources and compile once more**

```bash
npm run rebuild
```

Expected: import/repair and debug compile complete successfully. Do not use a force import.

- [ ] **Step 5: Run the complete focused native regression set**

```bash
npm run test:build
engine/src/out/dao-debug/browser_tests --gtest_filter='DaoHomeHistoryMaterialTest.*:DaoHomeExperienceTest.*:DaoHomeBootstrapTransactionTest.*:DaoHomeProjectStoreTest.*:DaoHomeBrowserTest.*'
```

Expected: all selected tests pass.

- [ ] **Step 6: Perform a real-browser manual verification**

Launch only after the rebuild succeeds:

```bash
npm run start:debug
```

In a regular Profile, open `dao://home/`, choose history creation, verify the grouped dialog, deselect one source, approve once, and confirm the same Agent turn proceeds through testing and publish. Verify the first viewport contains working launch actions; successful sources show current connector-backed data; rejected/failed sources remain launch actions; View source and Version history show no history report fields or captured live payloads; exactly one version has kind `history_bootstrap`.

- [ ] **Step 7: Record the final checkpoint, only if Git authorization is later granted**

```bash
git add docs/features.md docs/feature-checklist.md
git commit -m "docs(home): document action-first bootstrap"
```

## Final Review Checklist

- [ ] `HomeBootstrapBrief` contains no report-shaped or page-title fields.
- [ ] Sensitive targets cannot become history-triggered connector candidates.
- [ ] The grouped dialog is trusted, selectable, localized, and owner-bound.
- [ ] Approval creates independent exact receipt fingerprints.
- [ ] The pending Agent tool resumes after one trusted decision.
- [ ] Connector tests are sequential and record success and every failure class.
- [ ] Final connector definitions must match approved/tested fingerprints exactly.
- [ ] `experience.json` is required only for history-bootstrap publication.
- [ ] Browser-owned preview checks declared actions and tested source slots.
- [ ] Known history-report fields are rejected from persisted bootstrap files.
- [ ] Rejection and all-source failure still publish a useful launchpad.
- [ ] Native code forces `history_bootstrap`; the model cannot claim that kind outside the transaction.
- [ ] Successful bootstrap creates exactly one visible revision.
- [ ] Owner loss clears the decision callback, drafts, receipts, samples, executors, and preview.
- [ ] Runtime live payloads remain session-scoped and are absent from export/history.
- [ ] `docs/features.md` and `docs/feature-checklist.md` match the implemented behavior.
