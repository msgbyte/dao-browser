# Dream Report Privacy And Rerun Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add Dream Analysis domain exclusions and date-specific manual reruns without leaking excluded history into dream material.

**Architecture:** Store excluded domains as a profile pref, normalize and validate domains in native code, and filter annotated history visits in `DreamMaterialCollector` before search extraction or aggregation. Extend `DaoDreamService` manual runs to accept a date and preserve existing completed reports on failed reruns. Expose the feature through the Agent settings Dream panel and the standalone `dao://dream` history page, sharing the Dream runner bridge where needed.

**Tech Stack:** Chromium C++ services and WebUI message handlers, Profile prefs, HistoryService annotated visits, Lit TypeScript WebUI, Vitest, Chromium browser tests.

**Repo Constraint:** Do not create a branch or worktree. Do not run `git add`, `git commit`, `git push`, or any state-changing git command unless the user explicitly asks for that exact action.

---

## File Map

- Modify `src/dao/browser/dao_pref_names.h`: add the Dream excluded domains pref name.
- Modify `src/dao/browser/dao_pref_names.cc`: register the excluded domains list pref.
- Modify `src/dao/browser/agent/dao_dream_material_collector.h`: expose domain normalization/matching helpers for tests and add exclusion-aware collection state.
- Modify `src/dao/browser/agent/dao_dream_material_collector.cc`: normalize exclusion prefs and filter visits before query extraction/domain aggregation.
- Modify `src/dao/browser/agent/dao_dream_service.h`: add optional-date manual trigger and failure-preservation state.
- Modify `src/dao/browser/agent/dao_dream_service.cc`: validate manual dates, run specified date windows, and preserve completed reports on failed manual reruns.
- Modify `src/dao/browser/ui/webui/dao_agent_ui.h`: split shared Dream runner message handling from Agent-only settings/report card handling; add exclusion message handlers.
- Modify `src/dao/browser/ui/webui/dao_agent_ui.cc`: register shared runner messages on `dao://agent` and `dao://dream`, add exclusion APIs, and pass optional rerun dates to `DaoDreamService`.
- Modify `src/dao/browser/ui/webui/resources/agent/dao_settings_view.ts`: add Dream excluded-domain editor and keep existing "Dream now" behavior.
- Modify `src/dao/browser/ui/webui/resources/agent/dao_dream_app.ts`: add history-page rerun controls and date-specific rerun support.
- Modify `src/dao/browser/ui/webui/resources/agent/i18n/locales/en.ts`: add English strings for exclusions and reruns.
- Modify `src/dao/browser/ui/webui/resources/agent/i18n/locales/zh-CN.ts`: add hand-authored Chinese strings.
- Modify `src/dao/browser/agent/dao_dream_browsertest.cc`: add native privacy and rerun regression coverage.
- Modify `src/dao/browser/ui/webui/resources/agent/__tests__/dao_settings_view.test.ts`: add settings UI tests.
- Modify `src/dao/browser/ui/webui/resources/agent/__tests__/dao_dream_app.test.ts`: add dream history rerun UI tests.

---

### Task 1: Native Domain Exclusion Tests

**Files:**
- Modify: `src/dao/browser/agent/dao_dream_browsertest.cc`
- Later implementation files: `src/dao/browser/dao_pref_names.{h,cc}`, `src/dao/browser/agent/dao_dream_material_collector.{h,cc}`

- [ ] **Step 1: Write failing helper tests**

Add these tests after the existing `SearchQueryExtraction` test:

```cpp
IN_PROC_BROWSER_TEST_F(DaoDreamStaticTest, DreamDomainExclusionNormalization) {
  EXPECT_EQ("example.com",
            DreamMaterialCollector::NormalizeExcludedDomainForTesting(
                " HTTPS://Example.COM:443/path?q=1 "));
  EXPECT_EQ("app.example.com",
            DreamMaterialCollector::NormalizeExcludedDomainForTesting(
                "app.example.com."));
  EXPECT_EQ("", DreamMaterialCollector::NormalizeExcludedDomainForTesting(""));
  EXPECT_EQ("", DreamMaterialCollector::NormalizeExcludedDomainForTesting(
                    "localhost"));
  EXPECT_EQ("", DreamMaterialCollector::NormalizeExcludedDomainForTesting(
                    "com"));
  EXPECT_EQ("", DreamMaterialCollector::NormalizeExcludedDomainForTesting(
                    "127.0.0.1"));
}

IN_PROC_BROWSER_TEST_F(DaoDreamStaticTest, DreamDomainExclusionMatching) {
  std::set<std::string> excluded = {"example.com"};
  EXPECT_TRUE(DreamMaterialCollector::IsDomainExcludedForTesting(
      "example.com", excluded));
  EXPECT_TRUE(DreamMaterialCollector::IsDomainExcludedForTesting(
      "app.example.com", excluded));
  EXPECT_FALSE(DreamMaterialCollector::IsDomainExcludedForTesting(
      "notexample.com", excluded));
  EXPECT_FALSE(DreamMaterialCollector::IsDomainExcludedForTesting(
      "example.co", excluded));
}
```

- [ ] **Step 2: Write failing collection test**

Add this browser test after `CollectorRedactsUrlsAndAggregates`:

```cpp
IN_PROC_BROWSER_TEST_F(DaoDreamBrowserTest,
                       CollectorExcludesConfiguredDomainsBeforeAggregation) {
  PrefService* prefs = browser()->profile()->GetPrefs();
  base::Value::List excluded;
  excluded.Append("github.com");
  prefs->SetList(prefs::kDaoDreamExcludedDomains, std::move(excluded));

  history::HistoryService* history = HistoryServiceFactory::GetForProfile(
      browser()->profile(), ServiceAccessType::EXPLICIT_ACCESS);
  ASSERT_TRUE(history);
  const base::Time now = base::Time::Now();
  history->AddPageWithDetails(
      GURL("https://github.com/foo/bar?token=SECRET123"),
      u"Sensitive GitHub title", 1, 0, now - base::Hours(1), false,
      history::SOURCE_BROWSED);
  history->AddPageWithDetails(
      GURL("https://gist.github.com/private/snippet"),
      u"Sensitive Gist title", 1, 0, now - base::Hours(2), false,
      history::SOURCE_BROWSED);
  history->AddPageWithDetails(
      GURL("https://notgithub.com/public"),
      u"Public title", 1, 0, now - base::Hours(3), false,
      history::SOURCE_BROWSED);

  DaoAgentMemoryService* memory =
      DaoAgentMemoryServiceFactory::GetForProfile(browser()->profile());
  ASSERT_TRUE(memory);
  DreamMaterialCollector collector(browser()->profile(), memory);

  base::DictValue pack;
  base::RunLoop loop;
  collector.Collect(now - base::Hours(6), now,
                    base::BindLambdaForTesting([&](base::DictValue p) {
                      pack = std::move(p);
                      loop.Quit();
                    }));
  loop.Run();

  std::string json;
  base::JSONWriter::Write(pack, &json);
  EXPECT_EQ(std::string::npos, json.find("github.com"));
  EXPECT_EQ(std::string::npos, json.find("gist.github.com"));
  EXPECT_EQ(std::string::npos, json.find("Sensitive GitHub title"));
  EXPECT_EQ(std::string::npos, json.find("Sensitive Gist title"));
  EXPECT_EQ(std::string::npos, json.find("SECRET123"));
  EXPECT_NE(std::string::npos, json.find("notgithub.com"));

  const base::DictValue* stats = pack.FindDict("stats");
  ASSERT_TRUE(stats);
  EXPECT_EQ(2, stats->FindInt("excluded_history_visits").value_or(0));
}
```

- [ ] **Step 3: Write failing excluded search query test**

Add this test near the collection tests:

```cpp
IN_PROC_BROWSER_TEST_F(DaoDreamBrowserTest,
                       CollectorDoesNotExtractQueriesFromExcludedDomains) {
  PrefService* prefs = browser()->profile()->GetPrefs();
  base::Value::List excluded;
  excluded.Append("google.com");
  prefs->SetList(prefs::kDaoDreamExcludedDomains, std::move(excluded));

  history::HistoryService* history = HistoryServiceFactory::GetForProfile(
      browser()->profile(), ServiceAccessType::EXPLICIT_ACCESS);
  ASSERT_TRUE(history);
  const base::Time now = base::Time::Now();
  history->AddPage(GURL("https://www.google.com/search?q=private+query"),
                   now - base::Hours(1), history::SOURCE_BROWSED);
  history->AddPage(GURL("https://www.bing.com/search?q=public+query"),
                   now - base::Hours(2), history::SOURCE_BROWSED);

  DaoAgentMemoryService* memory =
      DaoAgentMemoryServiceFactory::GetForProfile(browser()->profile());
  ASSERT_TRUE(memory);
  DreamMaterialCollector collector(browser()->profile(), memory);

  base::DictValue pack;
  base::RunLoop loop;
  collector.Collect(now - base::Hours(6), now,
                    base::BindLambdaForTesting([&](base::DictValue p) {
                      pack = std::move(p);
                      loop.Quit();
                    }));
  loop.Run();

  std::string json;
  base::JSONWriter::Write(pack, &json);
  EXPECT_EQ(std::string::npos, json.find("private query"));
  EXPECT_NE(std::string::npos, json.find("public query"));
}
```

- [ ] **Step 4: Run the C++ browser test and verify RED**

Run only if the browser test binary is already built:

```bash
engine/src/out/dao-debug/browser_tests --gtest_filter="DaoDream*"
```

Expected: compile or test failure because `kDaoDreamExcludedDomains`,
`NormalizeExcludedDomainForTesting`, and `IsDomainExcludedForTesting` do not
exist yet. If the binary is not built, skip this command and rely on the final
`npm run rebuild` compile confirmation after implementation.

---

### Task 2: Implement Native Domain Exclusions

**Files:**
- Modify: `src/dao/browser/dao_pref_names.h`
- Modify: `src/dao/browser/dao_pref_names.cc`
- Modify: `src/dao/browser/agent/dao_dream_material_collector.h`
- Modify: `src/dao/browser/agent/dao_dream_material_collector.cc`

- [ ] **Step 1: Add the pref name**

In `dao_pref_names.h`, add:

```cpp
// List pref storing normalized domains excluded from Dream Analysis material
// collection. A value excludes the domain and its subdomains.
inline constexpr char kDaoDreamExcludedDomains[] =
    "dao.dream_excluded_domains";
```

- [ ] **Step 2: Register the pref**

In `RegisterProfilePrefs`, add:

```cpp
registry->RegisterListPref(kDaoDreamExcludedDomains);
```

- [ ] **Step 3: Expose test helpers and exclusion state**

In `dao_dream_material_collector.h`, add `#include <set>` and the helper
declarations:

```cpp
  static std::string NormalizeExcludedDomainForTesting(
      const std::string& input);
  static bool IsDomainExcludedForTesting(
      const std::string& host,
      const std::set<std::string>& excluded_domains);
```

Add private state:

```cpp
  std::set<std::string> excluded_domains_;
  int excluded_history_visits_ = 0;
```

- [ ] **Step 4: Implement normalization and matching**

In `dao_dream_material_collector.cc`, add includes:

```cpp
#include "base/strings/string_util.h"
#include "components/prefs/pref_service.h"
#include "dao/browser/dao_pref_names.h"
#include "net/base/registry_controlled_domains/registry_controlled_domain.h"
#include "net/base/url_util.h"
```

Add helper functions in the anonymous namespace:

```cpp
std::string NormalizeExcludedDomain(std::string input) {
  input = base::ToLowerASCII(base::TrimWhitespaceASCII(input, base::TRIM_ALL));
  if (input.empty()) {
    return std::string();
  }

  GURL parsed(input);
  if (!parsed.is_valid() || !parsed.has_host()) {
    parsed = GURL("https://" + input);
  }
  if (!parsed.is_valid() || !parsed.has_host()) {
    return std::string();
  }

  std::string host = base::ToLowerASCII(parsed.host());
  while (!host.empty() && host.back() == '.') {
    host.pop_back();
  }
  if (host.empty() || host.find('.') == std::string::npos ||
      net::HostStringIsLocalhost(host) || net::ParseURLHostnameToAddress(host)) {
    return std::string();
  }
  const size_t registry_length =
      net::registry_controlled_domains::GetRegistryLength(
          host, net::registry_controlled_domains::EXCLUDE_UNKNOWN_REGISTRIES,
          net::registry_controlled_domains::EXCLUDE_PRIVATE_REGISTRIES);
  if (registry_length == std::string::npos ||
      registry_length == host.size()) {
    return std::string();
  }
  return host;
}

bool IsDomainExcluded(const std::string& host,
                      const std::set<std::string>& excluded_domains) {
  const std::string normalized = NormalizeExcludedDomain(host);
  if (normalized.empty()) {
    return false;
  }
  for (const std::string& excluded : excluded_domains) {
    if (normalized == excluded) {
      return true;
    }
    if (normalized.size() > excluded.size() &&
        normalized.compare(normalized.size() - excluded.size(),
                           excluded.size(), excluded) == 0 &&
        normalized[normalized.size() - excluded.size() - 1] == '.') {
      return true;
    }
  }
  return false;
}

std::set<std::string> LoadExcludedDomains(Profile* profile) {
  std::set<std::string> domains;
  for (const base::Value& value :
       profile->GetPrefs()->GetList(prefs::kDaoDreamExcludedDomains)) {
    if (!value.is_string()) {
      continue;
    }
    std::string normalized = NormalizeExcludedDomain(value.GetString());
    if (!normalized.empty()) {
      domains.insert(std::move(normalized));
    }
  }
  return domains;
}
```

If `net::ParseURLHostnameToAddress` is not available with that signature,
replace the IP-literal check with Chromium's available `url::HostIsIPAddress`
helper after inspecting local includes.

- [ ] **Step 5: Wire helpers and collection filtering**

Add the public test helper definitions:

```cpp
std::string DreamMaterialCollector::NormalizeExcludedDomainForTesting(
    const std::string& input) {
  return NormalizeExcludedDomain(input);
}

bool DreamMaterialCollector::IsDomainExcludedForTesting(
    const std::string& host,
    const std::set<std::string>& excluded_domains) {
  return IsDomainExcluded(host, excluded_domains);
}
```

At the start of `Collect`, reset:

```cpp
excluded_domains_ = LoadExcludedDomains(profile_);
excluded_history_visits_ = 0;
```

Inside the history visit loop, before `ExtractSearchQuery(url.spec())`, add:

```cpp
const std::string domain(url.host());
if (IsDomainExcluded(domain, self->excluded_domains_)) {
  self->excluded_history_visits_++;
  continue;
}
```

Then reuse `domain` for aggregation instead of redeclaring it.

In `OnPartDone`, add:

```cpp
stats.Set("excluded_history_visits", excluded_history_visits_);
```

- [ ] **Step 6: Run focused verification**

Run:

```bash
engine/src/out/dao-debug/browser_tests --gtest_filter="DaoDream*"
```

Expected: the new domain exclusion tests pass if the binary is current. If the
binary is stale or missing, defer native verification to `npm run rebuild`.

---

### Task 3: Manual Rerun Native Tests

**Files:**
- Modify: `src/dao/browser/agent/dao_dream_browsertest.cc`
- Later implementation files: `src/dao/browser/agent/dao_dream_service.{h,cc}`

- [ ] **Step 1: Write failing specified-date manual run test**

Add this test near the existing manual dream tests:

```cpp
IN_PROC_BROWSER_TEST_F(DaoDreamBrowserTest, ManualDreamRunsSpecifiedDate) {
  history::HistoryService* history = HistoryServiceFactory::GetForProfile(
      browser()->profile(), ServiceAccessType::EXPLICIT_ACCESS);
  ASSERT_TRUE(history);
  history->AddPage(GURL("https://example.com/june-10"),
                   LocalTime(2026, 6, 10, 12, 0), history::SOURCE_BROWSED);

  DaoDreamService* service = dream_service();
  ASSERT_TRUE(service);
  base::SimpleTestClock clock;
  clock.SetNow(LocalTime(2026, 6, 12, 12, 0));
  service->SetClockForTesting(&clock);

  FakeRunner runner;
  service->SetRunner(&runner);
  base::RunLoop runner_loop;
  runner.quit_closure = runner_loop.QuitClosure();

  service->StartManualDreamForDate(
      "2026-06-10",
      base::BindLambdaForTesting([](bool ok, const std::string& error) {}));
  runner_loop.Run();

  EXPECT_TRUE(runner.ran);
  EXPECT_EQ("2026-06-10", runner.last_dream_date);
  service->ClearRunner(&runner);
}
```

- [ ] **Step 2: Write failing future-date validation test**

Add:

```cpp
IN_PROC_BROWSER_TEST_F(DaoDreamBrowserTest, ManualDreamRejectsFutureDate) {
  DaoDreamService* service = dream_service();
  ASSERT_TRUE(service);
  base::SimpleTestClock clock;
  clock.SetNow(LocalTime(2026, 6, 12, 12, 0));
  service->SetClockForTesting(&clock);

  bool callback_success = true;
  std::string callback_error;
  service->StartManualDreamForDate(
      "2026-06-13",
      base::BindLambdaForTesting([&](bool ok, const std::string& error) {
        callback_success = ok;
        callback_error = error;
      }));

  EXPECT_FALSE(callback_success);
  EXPECT_EQ("dream date cannot be in the future", callback_error);
  EXPECT_EQ(DaoDreamService::State::kIdle, service->state());
}
```

- [ ] **Step 3: Write failing failed-rerun preservation test**

Add:

```cpp
IN_PROC_BROWSER_TEST_F(DaoDreamBrowserTest,
                       FailedManualRerunPreservesCompletedReport) {
  DaoAgentMemoryService* memory =
      DaoAgentMemoryServiceFactory::GetForProfile(browser()->profile());
  ASSERT_TRUE(memory);

  DreamReport existing;
  existing.dream_date = "2026-06-10";
  existing.report_markdown = "# old report";
  existing.habit_candidates = "[]";
  existing.material_stats = "{}";
  existing.status = "completed";
  existing.trigger_kind = "nightly";
  {
    base::RunLoop loop;
    memory->SaveDreamReport(
        existing, base::BindLambdaForTesting([&](bool ok) {
          ASSERT_TRUE(ok);
          loop.Quit();
        }));
    loop.Run();
  }

  history::HistoryService* history = HistoryServiceFactory::GetForProfile(
      browser()->profile(), ServiceAccessType::EXPLICIT_ACCESS);
  ASSERT_TRUE(history);
  history->AddPage(GURL("https://example.com/june-10"),
                   LocalTime(2026, 6, 10, 12, 0), history::SOURCE_BROWSED);

  DaoDreamService* service = dream_service();
  ASSERT_TRUE(service);
  base::SimpleTestClock clock;
  clock.SetNow(LocalTime(2026, 6, 12, 12, 0));
  service->SetClockForTesting(&clock);

  FakeRunner runner;
  service->SetRunner(&runner);
  base::RunLoop runner_loop;
  runner.quit_closure = runner_loop.QuitClosure();
  base::RunLoop callback_loop;

  service->StartManualDreamForDate(
      "2026-06-10",
      base::BindLambdaForTesting([&](bool ok, const std::string& error) {
        EXPECT_FALSE(ok);
        EXPECT_EQ("rerun failed", error);
        callback_loop.Quit();
      }));
  runner_loop.Run();
  service->OnDreamFailed("2026-06-10", "rerun failed");
  callback_loop.Run();

  std::optional<DreamReport> got;
  base::RunLoop verify_loop;
  memory->GetDreamReportByDate(
      "2026-06-10",
      base::BindLambdaForTesting([&](std::optional<DreamReport> r) {
        got = std::move(r);
        verify_loop.Quit();
      }));
  verify_loop.Run();
  ASSERT_TRUE(got.has_value());
  EXPECT_EQ("completed", got->status);
  EXPECT_EQ("# old report", got->report_markdown);
  service->ClearRunner(&runner);
}
```

- [ ] **Step 4: Run the C++ test and verify RED**

Run if possible:

```bash
engine/src/out/dao-debug/browser_tests --gtest_filter="DaoDream*Manual*"
```

Expected: compile failure because `StartManualDreamForDate` is not defined.

---

### Task 4: Implement Date-Specific Manual Reruns

**Files:**
- Modify: `src/dao/browser/agent/dao_dream_service.h`
- Modify: `src/dao/browser/agent/dao_dream_service.cc`

- [ ] **Step 1: Add the service API and state**

In `dao_dream_service.h`, add:

```cpp
  void StartManualDreamForDate(
      const std::string& dream_date,
      base::OnceCallback<void(bool success, const std::string& error)> callback);
```

Add private state:

```cpp
  bool preserve_completed_report_on_failure_ = false;
```

If tests need date parsing directly, add a private `ValidateManualDreamDate`
helper in the `.cc` file rather than exposing more public API.

- [ ] **Step 2: Validate manual dates**

In `dao_dream_service.cc`, add a helper near `ParseYmd`:

```cpp
bool IsFutureDreamDate(const std::string& ymd, base::Time now) {
  base::Time date_midnight;
  if (!ParseYmd(ymd, &date_midnight)) {
    return true;
  }
  base::Time today_midnight;
  return ParseYmd(FormatYmd(now), &today_midnight) &&
         date_midnight > today_midnight;
}
```

- [ ] **Step 3: Implement `StartManualDreamForDate`**

Implement:

```cpp
void DaoDreamService::StartManualDreamForDate(
    const std::string& dream_date,
    base::OnceCallback<void(bool success, const std::string& error)> callback) {
  base::Time parsed;
  if (!ParseYmd(dream_date, &parsed)) {
    std::move(callback).Run(false, "invalid dream date");
    return;
  }
  if (IsFutureDreamDate(dream_date, clock_->Now())) {
    std::move(callback).Run(false, "dream date cannot be in the future");
    return;
  }
  if (state_ != State::kIdle) {
    std::move(callback).Run(false, "dream already running");
    return;
  }
  if (!runner_) {
    std::move(callback).Run(false, "agent webui unavailable");
    return;
  }
  preserve_completed_report_on_failure_ = true;
  manual_callback_ = std::move(callback);
  StartDream(dream_date, TriggerKind::kManual);
}
```

Keep existing `StartManualDream` as:

```cpp
void DaoDreamService::StartManualDream(
    base::OnceCallback<void(bool success, const std::string& error)> callback) {
  StartManualDreamForDate(DreamDateFor(clock_->Now()), std::move(callback));
}
```

- [ ] **Step 4: Preserve completed reports on failed manual reruns**

In `MarkFailed`, before creating a failed `DreamReport`, add:

```cpp
if (self->preserve_completed_report_on_failure_ && existing &&
    existing->status == "completed") {
  self->FinishRun(false, error);
  return;
}
```

In `FinishRun`, reset:

```cpp
preserve_completed_report_on_failure_ = false;
```

- [ ] **Step 5: Run focused verification**

Run if possible:

```bash
engine/src/out/dao-debug/browser_tests --gtest_filter="DaoDream*Manual*"
```

Expected: manual rerun tests pass if the binary is current.

---

### Task 5: Native WebUI APIs And Shared Dream Runner

**Files:**
- Modify: `src/dao/browser/ui/webui/dao_agent_ui.h`
- Modify: `src/dao/browser/ui/webui/dao_agent_ui.cc`

- [ ] **Step 1: Add WebUI handler tests by using WebUI-facing TypeScript tests first**

Do not write C++ WebUI handler tests unless there is an existing local harness.
The WebUI API behavior will be driven by the UI tests in Tasks 6 and 7 and by
the C++ service tests in Tasks 1 and 3.

- [ ] **Step 2: Split shared runner handling**

In `dao_agent_ui.h`, create a shared base handler:

```cpp
class DaoDreamRunnerHandler : public content::WebUIMessageHandler,
                              public DaoDreamService::Runner {
 public:
  DaoDreamRunnerHandler();
  ~DaoDreamRunnerHandler() override;

  void RegisterMessages() override;
  void OnJavascriptAllowed() override;
  void OnJavascriptDisallowed() override;
  void RunDream(const std::string& dream_date,
                const base::DictValue& material) override;

 protected:
  DaoDreamService* GetDreamService();

 private:
  void HandleDreamComplete(const base::ListValue& args);
  void HandleDreamFailed(const base::ListValue& args);

  base::WeakPtrFactory<DaoDreamRunnerHandler> weak_factory_{this};
};
```

Then make `DaoAgentDreamHandler` inherit only from `content::WebUIMessageHandler`
for Agent-specific settings/report-card messages, or keep it separate with no
runner methods.

- [ ] **Step 3: Move runner method implementations**

Move these existing methods from `DaoAgentDreamHandler` to
`DaoDreamRunnerHandler`:

```cpp
HandleDreamComplete
HandleDreamFailed
RunDream
OnJavascriptAllowed
OnJavascriptDisallowed
GetDreamService
```

Register only:

```cpp
dreamComplete
dreamFailed
```

in `DaoDreamRunnerHandler::RegisterMessages`.

- [ ] **Step 4: Register shared runner on both pages**

In `DaoAgentUI::DaoAgentUI`, add:

```cpp
web_ui->AddMessageHandler(std::make_unique<DaoDreamRunnerHandler>());
```

Keep `DaoAgentDreamHandler` for Agent settings messages.

In `DaoDreamUI::DaoDreamUI`, add:

```cpp
web_ui->AddMessageHandler(std::make_unique<DaoDreamRunnerHandler>());
```

This lets `dao://dream` run the LLM without duplicating report handler message
names.

- [ ] **Step 5: Add exclusion APIs**

In `DaoAgentDreamHandler`, add message handlers:

```cpp
getDreamExcludedDomains
addDreamExcludedDomain
removeDreamExcludedDomain
```

Implement them against `profile->GetPrefs()->GetList/SetList` using the same
normalization helper as the collector. If the helper currently lives only in
the `.cc` anonymous namespace, move normalization/matching into a small
Dao-owned helper section in `dao_dream_material_collector.{h,cc}` or a focused
new `dao_dream_domain_exclusions.{h,cc}` file. Prefer the new helper file if
the UI handler would otherwise depend on test-only collector APIs.

For invalid domains:

```cpp
RejectJavascriptCallback(base::Value(callback_id),
                         base::Value("invalid domain"));
```

For duplicate domains, resolve successfully with:

```cpp
base::DictValue response;
response.Set("domain", normalized);
ResolveJavascriptCallback(base::Value(callback_id), response);
```

- [ ] **Step 6: Extend `HandleStartManualDream`**

Parse optional params:

```cpp
std::string dream_date;
if (args.size() >= 2 && args[1].is_dict()) {
  if (const std::string* date = args[1].GetDict().FindString("date")) {
    dream_date = *date;
  }
}
```

Call:

```cpp
if (!dream_date.empty()) {
  service->StartManualDreamForDate(dream_date, std::move(callback));
} else {
  service->StartManualDream(std::move(callback));
}
```

Keep the no-argument path compatible with the existing settings UI.

---

### Task 6: Settings UI Tests And Implementation

**Files:**
- Modify: `src/dao/browser/ui/webui/resources/agent/__tests__/dao_settings_view.test.ts`
- Modify: `src/dao/browser/ui/webui/resources/agent/dao_settings_view.ts`
- Modify: `src/dao/browser/ui/webui/resources/agent/i18n/locales/en.ts`
- Modify: `src/dao/browser/ui/webui/resources/agent/i18n/locales/zh-CN.ts`

- [ ] **Step 1: Write failing settings UI test**

In `dao_settings_view.test.ts`, add to the dream controls `describe` block:

```ts
it('loads adds and removes dream excluded domains', async () => {
  settingsMocks.callNativeArgs.mockImplementation(async (method: string,
      value?: unknown) => {
    switch (method) {
      case 'getMemoryEnabled':
      case 'getDreamEnabled':
        return true;
      case 'getDreamDebug':
        return false;
      case 'getDreamExcludedDomains':
        return ['github.com'];
      case 'addDreamExcludedDomain':
        expect(value).toBe('https://Example.com/private');
        return {domain: 'example.com'};
      case 'removeDreamExcludedDomain':
        expect(value).toBe('github.com');
        return true;
      case 'getStorageStats':
        return {
          totalSize: 0,
          conversationCount: 0,
          episodeCount: 0,
          preferenceCount: 0,
        };
      default:
        return true;
    }
  });

  const view = await mountMemorySettings();
  expect(view.shadowRoot!.textContent).toContain('github.com');

  const input = view.shadowRoot!.querySelector<HTMLInputElement>(
      'input[data-testid="dream-excluded-domain-input"]');
  expect(input).toBeTruthy();
  input!.value = 'https://Example.com/private';
  input!.dispatchEvent(new Event('input', {bubbles: true}));
  input!.dispatchEvent(new KeyboardEvent('keydown', {
    key: 'Enter',
    bubbles: true,
  }));
  await Promise.resolve();
  await view.updateComplete;

  expect(settingsMocks.callNativeArgs).toHaveBeenCalledWith(
      'addDreamExcludedDomain', 'https://Example.com/private');
  expect(view.shadowRoot!.textContent).toContain('example.com');

  const removeButton = view.shadowRoot!.querySelector<HTMLButtonElement>(
      'button[data-domain="github.com"]');
  expect(removeButton).toBeTruthy();
  removeButton!.click();
  await Promise.resolve();
  await view.updateComplete;

  expect(settingsMocks.callNativeArgs).toHaveBeenCalledWith(
      'removeDreamExcludedDomain', 'github.com');
  expect(view.shadowRoot!.textContent).not.toContain('github.com');
});
```

- [ ] **Step 2: Run settings test and verify RED**

Run:

```bash
npm run test:webui -- src/dao/browser/ui/webui/resources/agent/__tests__/dao_settings_view.test.ts
```

Expected: FAIL because the input and native calls do not exist.

- [ ] **Step 3: Implement settings state and loading**

In `DaoSettingsView.properties`, add:

```ts
dreamExcludedDomains_: {type: Array, state: true},
dreamExcludedDomainInput_: {type: String, state: true},
```

Declare fields:

```ts
declare private dreamExcludedDomains_: string[];
declare private dreamExcludedDomainInput_: string;
```

Initialize in constructor:

```ts
this.dreamExcludedDomains_ = [];
this.dreamExcludedDomainInput_ = '';
```

In `loadMemorySettings_`, add:

```ts
callNativeArgs('getDreamExcludedDomains').then(domains => {
  this.dreamExcludedDomains_ = Array.isArray(domains) ?
      domains.filter((domain): domain is string => typeof domain === 'string') :
      [];
}).catch(() => {});
```

- [ ] **Step 4: Implement settings add/remove methods**

Add methods:

```ts
private async addDreamExcludedDomain_() {
  const input = this.dreamExcludedDomainInput_.trim();
  if (!input) {
    return;
  }
  try {
    const result = await callNativeArgs('addDreamExcludedDomain', input) as
        {domain?: string};
    const domain = result?.domain;
    if (typeof domain === 'string' && domain) {
      this.dreamExcludedDomains_ =
          [...new Set([...this.dreamExcludedDomains_, domain])].sort();
      this.dreamExcludedDomainInput_ = '';
    }
  } catch (e) {
    const msg = e instanceof Error ? e.message : String(e);
    this.fireToast_(t('settings.dream.excluded_add_failed', {error: msg}));
  }
}

private async removeDreamExcludedDomain_(domain: string) {
  try {
    await callNativeArgs('removeDreamExcludedDomain', domain);
    this.dreamExcludedDomains_ =
        this.dreamExcludedDomains_.filter(item => item !== domain);
  } catch (e) {
    const msg = e instanceof Error ? e.message : String(e);
    this.fireToast_(t('settings.dream.excluded_remove_failed', {error: msg}));
  }
}
```

- [ ] **Step 5: Render the editor**

In `renderDream_`, after the enable toggle and before debug mode, add:

```ts
<div class="dream-exclusions">
  <label for="dream-excluded-domain-input">
    ${t('settings.dream.excluded_domains_label')}
  </label>
  <div class="dream-exclusion-input-row">
    <input id="dream-excluded-domain-input"
        data-testid="dream-excluded-domain-input"
        .value=${this.dreamExcludedDomainInput_}
        placeholder=${t('settings.dream.excluded_domains_placeholder')}
        @input=${(e: Event) => {
          this.dreamExcludedDomainInput_ =
              (e.target as HTMLInputElement).value;
        }}
        @keydown=${(e: KeyboardEvent) => {
          if (e.key === 'Enter') {
            e.preventDefault();
            void this.addDreamExcludedDomain_();
          }
        }}>
    <button class="btn-secondary"
        ?disabled=${!this.dreamExcludedDomainInput_.trim()}
        @click=${() => void this.addDreamExcludedDomain_()}>
      ${t('settings.dream.excluded_domains_add')}
    </button>
  </div>
  <div class="dream-exclusion-list">
    ${this.dreamExcludedDomains_.map(domain => html`
      <span class="dream-exclusion-chip">
        <span>${domain}</span>
        <button data-domain=${domain}
            aria-label=${t('settings.dream.excluded_domains_remove',
                {domain})}
            @click=${() => void this.removeDreamExcludedDomain_(domain)}>
          ×
        </button>
      </span>`)}
  </div>
</div>
```

Use existing compact settings styling; keep text in i18n.

- [ ] **Step 6: Add i18n strings**

In `en.ts`, add:

```ts
'settings.dream.excluded_domains_label': 'Excluded domains',
'settings.dream.excluded_domains_placeholder': 'example.com',
'settings.dream.excluded_domains_add': 'Add',
'settings.dream.excluded_domains_remove': 'Remove {domain}',
'settings.dream.excluded_add_failed': 'Could not add domain: {error}',
'settings.dream.excluded_remove_failed': 'Could not remove domain: {error}',
```

Update `settings.dream.enable_desc` to mention excluded domains are omitted.

In `zh-CN.ts`, add hand-authored equivalents:

```ts
'settings.dream.excluded_domains_label': '排除域名',
'settings.dream.excluded_domains_placeholder': 'example.com',
'settings.dream.excluded_domains_add': '添加',
'settings.dream.excluded_domains_remove': '移除 {domain}',
'settings.dream.excluded_add_failed': '无法添加域名:{error}',
'settings.dream.excluded_remove_failed': '无法移除域名:{error}',
```

- [ ] **Step 7: Run settings test and verify GREEN**

Run:

```bash
npm run test:webui -- src/dao/browser/ui/webui/resources/agent/__tests__/dao_settings_view.test.ts
```

Expected: PASS.

---

### Task 7: Dream History Rerun UI Tests And Implementation

**Files:**
- Modify: `src/dao/browser/ui/webui/resources/agent/__tests__/dao_dream_app.test.ts`
- Modify: `src/dao/browser/ui/webui/resources/agent/dao_dream_app.ts`
- Modify: `src/dao/browser/ui/webui/resources/agent/i18n/locales/en.ts`
- Modify: `src/dao/browser/ui/webui/resources/agent/i18n/locales/zh-CN.ts`

- [ ] **Step 1: Write failing history-item rerun test**

Add to `dao_dream_app.test.ts`:

```ts
it('reruns a selected history report date', async () => {
  bridgeMocks.callNative.mockImplementation(async (method: string,
      params?: unknown) => {
    if (method === 'getDreamReports') {
      return [report('2026-06-19'), report('2026-06-18')];
    }
    if (method === 'startManualDream') {
      expect(params).toEqual({date: '2026-06-18'});
      return true;
    }
    return undefined;
  });

  const el = await mountDreamApp('/');
  const rerunButton = el.shadowRoot!.querySelector<HTMLButtonElement>(
      'button[data-rerun-date="2026-06-18"]');
  expect(rerunButton).toBeTruthy();
  rerunButton!.click();
  await Promise.resolve();
  await Promise.resolve();
  await el.updateComplete;

  expect(bridgeMocks.callNative).toHaveBeenCalledWith(
      'startManualDream', {date: '2026-06-18'}, {timeoutMs: 360000});
  expect(bridgeMocks.callNative).toHaveBeenCalledWith(
      'getDreamReports', {limit: 30});
});
```

- [ ] **Step 2: Write failing date-input rerun test**

Add:

```ts
it('reruns a date entered in the history date input', async () => {
  bridgeMocks.callNative.mockImplementation(async (method: string,
      params?: unknown) => {
    if (method === 'getDreamReports') {
      return [report('2026-06-19')];
    }
    if (method === 'startManualDream') {
      expect(params).toEqual({date: '2026-06-17'});
      return true;
    }
    return undefined;
  });

  const el = await mountDreamApp('/');
  const input = el.shadowRoot!.querySelector<HTMLInputElement>(
      'input[data-testid="dream-rerun-date-input"]');
  expect(input).toBeTruthy();
  input!.value = '2026-06-17';
  input!.dispatchEvent(new Event('input', {bubbles: true}));
  const runButton = el.shadowRoot!.querySelector<HTMLButtonElement>(
      'button[data-testid="dream-rerun-date-button"]');
  expect(runButton).toBeTruthy();
  runButton!.click();
  await Promise.resolve();
  await Promise.resolve();
  await el.updateComplete;

  expect(bridgeMocks.callNative).toHaveBeenCalledWith(
      'startManualDream', {date: '2026-06-17'}, {timeoutMs: 360000});
});
```

- [ ] **Step 3: Run dream app test and verify RED**

Run:

```bash
npm run test:webui -- src/dao/browser/ui/webui/resources/agent/__tests__/dao_dream_app.test.ts
```

Expected: FAIL because rerun controls do not exist.

- [ ] **Step 4: Add state and rerun methods**

In `DaoDreamApp.properties`, add:

```ts
rerunDate_: {type: String, state: true},
rerunRunning_: {type: Boolean, state: true},
rerunError_: {type: String, state: true},
```

Declare and initialize:

```ts
declare private rerunDate_: string;
declare private rerunRunning_: boolean;
declare private rerunError_: string;
```

```ts
this.rerunDate_ = '';
this.rerunRunning_ = false;
this.rerunError_ = '';
```

Add constant near the top:

```ts
const DREAM_RUN_NATIVE_TIMEOUT_MS = 6 * 60 * 1000;
```

Add method:

```ts
private async rerunDreamDate_(date: string) {
  const dreamDate = date.trim();
  if (!dreamDate || this.rerunRunning_) {
    return;
  }
  this.rerunRunning_ = true;
  this.rerunError_ = '';
  const previousReport = this.report_;
  try {
    await callNative('startManualDream', {date: dreamDate}, {
      timeoutMs: DREAM_RUN_NATIVE_TIMEOUT_MS,
    });
    await this.loadHistory_();
    const match = this.reports_.find(report => report.dreamDate === dreamDate);
    if (match) {
      this.selectHistoryReport_(match);
    }
  } catch (e) {
    this.report_ = previousReport;
    this.rerunError_ = e instanceof Error ? e.message : String(e);
  } finally {
    this.rerunRunning_ = false;
  }
}
```

- [ ] **Step 5: Render history rerun controls**

In `renderHistoryList_`, add a date input before mapped reports:

```ts
<div class="history-rerun-row">
  <input type="date"
      data-testid="dream-rerun-date-input"
      .value=${this.rerunDate_}
      @input=${(e: Event) => {
        this.rerunDate_ = (e.target as HTMLInputElement).value;
      }}>
  <button data-testid="dream-rerun-date-button"
      ?disabled=${this.rerunRunning_ || !this.rerunDate_}
      @click=${() => void this.rerunDreamDate_(this.rerunDate_)}>
    ${this.rerunRunning_ ? t('dream.page.rerun_running') :
                           t('dream.page.rerun_date')}
  </button>
</div>
${this.rerunError_ ? html`
  <div class="history-rerun-error">
    ${t('dream.page.rerun_failed', {error: this.rerunError_})}
  </div>` : nothing}
```

In each history item, keep the date-select button and add a sibling icon button:

```ts
<button
    class=${'history-item ' +
        (this.report_?.id === report.id ? 'selected' : '')}
    @click=${() => this.selectHistoryReport_(report)}>
  <span class="history-date">${report.dreamDate}</span>
  <span class="history-kind">
    ${this.triggerKindLabel_(report.triggerKind)}
  </span>
</button>
<button class="history-rerun-button"
    data-rerun-date=${report.dreamDate}
    title=${t('dream.page.rerun_report')}
    aria-label=${t('dream.page.rerun_report')}
    ?disabled=${this.rerunRunning_}
    @click=${() => void this.rerunDreamDate_(report.dreamDate)}>
  ↻
</button>
```

Use a simple text glyph only if no Lucide helper is available in this WebUI
surface; otherwise use a refresh icon SVG consistent with existing icon-only
buttons.

- [ ] **Step 6: Add i18n strings**

In `en.ts`, add:

```ts
'dream.page.rerun_report': 'Rerun report',
'dream.page.rerun_date': 'Run date',
'dream.page.rerun_running': 'Dreaming…',
'dream.page.rerun_failed': 'Rerun failed: {error}',
```

In `zh-CN.ts`, add:

```ts
'dream.page.rerun_report': '重新生成报告',
'dream.page.rerun_date': '生成日期',
'dream.page.rerun_running': '正在做梦…',
'dream.page.rerun_failed': '重新生成失败:{error}',
```

- [ ] **Step 7: Run dream app test and verify GREEN**

Run:

```bash
npm run test:webui -- src/dao/browser/ui/webui/resources/agent/__tests__/dao_dream_app.test.ts
```

Expected: PASS.

---

### Task 8: Final Verification

**Files:**
- All modified files from previous tasks.

- [ ] **Step 1: Run WebUI focused tests**

Run:

```bash
npm run test:webui -- src/dao/browser/ui/webui/resources/agent/__tests__/dao_settings_view.test.ts src/dao/browser/ui/webui/resources/agent/__tests__/dao_dream_app.test.ts
```

Expected: PASS.

- [ ] **Step 2: Run Lit lint**

Run:

```bash
npm run lint:lit
```

Expected: PASS.

- [ ] **Step 3: Import patches into engine**

Run:

```bash
npm run import
```

Expected: PASS. Do not use `npm run import -- --force` unless the user
explicitly approves it.

- [ ] **Step 4: Compile confirmation**

Run:

```bash
npm run rebuild
```

Expected: PASS. This is the only allowed compile-confirmation command for this
repo.

- [ ] **Step 5: Optional focused browser tests**

If `npm run rebuild` succeeds and the browser test binary is available, run:

```bash
engine/src/out/dao-debug/browser_tests --gtest_filter="DaoDream*"
```

Expected: PASS.

---

## Self-Review

- Spec coverage: domain exclusion storage, native filtering before aggregation,
  aggregate-only stats, date-specific manual reruns, failed-rerun preservation,
  Agent settings UI, Dream history UI, and verification are all mapped to tasks.
- Placeholder scan: no `TBD`, `TODO`, or implementation-free "add tests later"
  steps remain.
- Type consistency: the plan consistently uses `kDaoDreamExcludedDomains`,
  `StartManualDreamForDate`, `startManualDream({date})`,
  `getDreamExcludedDomains`, `addDreamExcludedDomain`, and
  `removeDreamExcludedDomain`.
- Repo constraint: commit steps are intentionally omitted because
  `AGENTS.md` forbids state-changing git commands without explicit user
  authorization.
