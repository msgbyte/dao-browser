# Dream Analysis System Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

> **⚠️ PROJECT OVERRIDES (CLAUDE.md — these beat the default skill workflow):**
> 1. **NO git commits anywhere in this plan.** Leave all changes unstaged. The user commits. Subagents must be told the same.
> 2. **NO per-task builds.** Chromium builds are expensive — batch all code, then run `npm run rebuild` only at the dedicated build-verification tasks (Task 10, 13). NEVER run autoninja/ninja/siso/gn directly.
> 3. Edit only `src/dao/` and `src/patches/` — never `engine/` as a deliverable. After editing patches, `npm run import` applies them.
> 4. Export patches only with `npm run export -- <file>` (never bare export).

**Goal:** Nightly/idle-triggered batch job that summarizes the day's browsing via the user's LLM, writes habits into agent memory, and shows a morning report card (with habit confirm/reject and a debug-inputs view) in the Agent panel.

**Architecture:** New profile-keyed `DaoDreamService` (C++) does scheduling (5-min poll: night window 22:00–06:00 + system idle ≥1h, catch-up path, manual path), collects 4 material sources (HistoryService, conversations, action feedback, search keywords) sanitized to domain+title granularity, then hands the material pack to the resident preloaded agent WebUI which runs the LLM call via existing `pi_llm_stream` and returns structured JSON for persistence (`MergePreference` + new `dream_reports` table).

**Tech Stack:** Chromium C++ (KeyedService, HistoryService, ui::CalculateIdleTime, sql), WebUI TypeScript (lit, pi-ai), GN patches, vitest.

**Spec:** `docs/superpowers/specs/2026-06-11-dream-analysis-design.md`

---

### Task 1: Prefs

**Files:**
- Modify: `src/dao/browser/dao_pref_names.h`
- Modify: `src/dao/browser/dao_pref_names.cc`

- [ ] **Step 1.1: Declare prefs in `dao_pref_names.h`** — after `kDaoWelcomeShown`:

```cpp
// Boolean pref that controls the Dream Analysis system. When false (the
// default), the dream scheduler never fires and no browsing data is sent
// to the LLM provider. Requires kDaoAgentMemoryEnabled to also be true.
inline constexpr char kDaoDreamEnabled[] = "dao.dream_enabled";

// Boolean pref for dream debug mode. When true, each dream run persists
// the full material pack JSON (the exact LLM input) into the report row
// so the user can inspect what was summarized.
inline constexpr char kDaoDreamDebug[] = "dao.dream_debug";
```

- [ ] **Step 1.2: Register in `dao_pref_names.cc`** — inside `RegisterProfilePrefs`:

```cpp
  registry->RegisterBooleanPref(kDaoDreamEnabled, false);
  registry->RegisterBooleanPref(kDaoDreamDebug, false);
```

No new patch needed — `browser_prefs.cc.patch` already calls `dao::prefs::RegisterProfilePrefs`.

---

### Task 2: DreamReport type + dream_reports table (schema v3)

**Files:**
- Modify: `src/dao/browser/agent/dao_agent_memory_types.h`
- Modify: `src/dao/browser/agent/dao_agent_memory_types.cc`
- Modify: `src/dao/browser/agent/dao_agent_memory_store.h`
- Modify: `src/dao/browser/agent/dao_agent_memory_store.cc`

- [ ] **Step 2.1: Add `DreamReport` struct to `dao_agent_memory_types.h`** (after `StorageStats`):

```cpp
struct DreamReport {
  DreamReport();
  ~DreamReport();
  DreamReport(const DreamReport&);
  DreamReport& operator=(const DreamReport&);
  DreamReport(DreamReport&&);
  DreamReport& operator=(DreamReport&&);

  int64_t id = 0;
  std::string dream_date;       // "YYYY-MM-DD" local dream-day attribution
  std::string report_markdown;
  std::string habit_candidates; // JSON array (LLM "habits" output)
  std::string material_stats;   // JSON: per-source counts
  std::string status;           // "completed" | "failed"
  int attempt_count = 0;
  std::string trigger_kind;     // "nightly" | "catchup" | "manual"
  std::string debug_material_json;  // material pack when debug mode on
  base::Time viewed_at;         // null = unread
  base::Time created_at;
};
```

- [ ] **Step 2.2: Add rule-of-six definitions to `dao_agent_memory_types.cc`** (mirror the `StorageStats` neighbors exactly):

```cpp
DreamReport::DreamReport() = default;
DreamReport::~DreamReport() = default;
DreamReport::DreamReport(const DreamReport&) = default;
DreamReport& DreamReport::operator=(const DreamReport&) = default;
DreamReport::DreamReport(DreamReport&&) = default;
DreamReport& DreamReport::operator=(DreamReport&&) = default;
```

- [ ] **Step 2.3: Bump schema + declare CRUD in `dao_agent_memory_store.h`**:
  - Change `static constexpr int kCurrentSchemaVersion = 2;` → `= 3;`
  - Add to the public section (after the Stats block):

```cpp
  // Dream reports (no FTS — plain queries only)
  bool SaveDreamReport(const DreamReport& report);   // upsert by dream_date
  std::optional<DreamReport> GetDreamReportByDate(const std::string& date);
  std::optional<DreamReport> GetLatestUnviewedDreamReport();
  bool MarkDreamReportViewed(int64_t id);
```

  Add `#include <optional>` to the header includes if not present.

- [ ] **Step 2.4: Create table in `CreateSchema()`** (`dao_agent_memory_store.cc`, alongside the other CREATE TABLE statements, inside the same transaction):

```cpp
  if (!db_->Execute(
          "CREATE TABLE IF NOT EXISTS dream_reports ("
          "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
          "  dream_date TEXT NOT NULL UNIQUE,"
          "  report_markdown TEXT NOT NULL DEFAULT '',"
          "  habit_candidates TEXT NOT NULL DEFAULT '[]',"
          "  material_stats TEXT NOT NULL DEFAULT '{}',"
          "  status TEXT NOT NULL,"
          "  attempt_count INTEGER NOT NULL DEFAULT 0,"
          "  trigger_kind TEXT NOT NULL,"
          "  debug_material_json TEXT NOT NULL DEFAULT '',"
          "  viewed_at INTEGER,"
          "  created_at INTEGER NOT NULL)")) {
    return false;
  }
  if (!db_->Execute(
          "CREATE INDEX IF NOT EXISTS idx_dream_reports_date "
          "ON dream_reports(dream_date)")) {
    return false;
  }
```

- [ ] **Step 2.5: Migration v2→v3 in `MigrateIfNeeded()`** (after the v1→v2 block). Because `CreateSchema()` uses `IF NOT EXISTS` and runs after migration, the table is created there; the migration only needs the version bump:

```cpp
  // v2 → v3: dream_reports table (created in CreateSchema via IF NOT
  // EXISTS; only the version number needs to move).
  if (version == 2) {
    std::ignore = meta_table_->SetVersionNumber(3);
    std::ignore = meta_table_->SetCompatibleVersionNumber(3);
  }
```

  Note the existing v1→v2 block sets version to 2 and falls through — verify the local `version` variable is re-read or the blocks chain (`if (version == 1) {...; version = 2;}` style). Match the existing code's chaining convention; if it doesn't chain, add `version = 2;` at the end of the v1 block and `version = 3;` at the end of the new block.

- [ ] **Step 2.6: Implement CRUD in `dao_agent_memory_store.cc`** (use the file-local `TimeToInt`/`TimeFromInt` helpers; follow the style of existing methods — `DCHECK_CALLED_ON_VALID_SEQUENCE` first, `sql::Statement` with `GetUniqueStatement`):

```cpp
bool DaoAgentMemoryStore::SaveDreamReport(const DreamReport& report) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  sql::Statement stmt(db_->GetUniqueStatement(
      "INSERT INTO dream_reports (dream_date, report_markdown, "
      "habit_candidates, material_stats, status, attempt_count, "
      "trigger_kind, debug_material_json, viewed_at, created_at) "
      "VALUES (?,?,?,?,?,?,?,?,NULL,?) "
      "ON CONFLICT(dream_date) DO UPDATE SET "
      "report_markdown=excluded.report_markdown, "
      "habit_candidates=excluded.habit_candidates, "
      "material_stats=excluded.material_stats, "
      "status=excluded.status, "
      "attempt_count=excluded.attempt_count, "
      "trigger_kind=excluded.trigger_kind, "
      "debug_material_json=excluded.debug_material_json, "
      "viewed_at=NULL, "
      "created_at=excluded.created_at"));
  stmt.BindString(0, report.dream_date);
  stmt.BindString(1, report.report_markdown);
  stmt.BindString(2, report.habit_candidates);
  stmt.BindString(3, report.material_stats);
  stmt.BindString(4, report.status);
  stmt.BindInt(5, report.attempt_count);
  stmt.BindString(6, report.trigger_kind);
  stmt.BindString(7, report.debug_material_json);
  stmt.BindInt64(8, TimeToInt(base::Time::Now()));
  return stmt.Run();
}

namespace {
dao::DreamReport DreamReportFromStatement(sql::Statement& stmt) {
  dao::DreamReport r;
  r.id = stmt.ColumnInt64(0);
  r.dream_date = stmt.ColumnString(1);
  r.report_markdown = stmt.ColumnString(2);
  r.habit_candidates = stmt.ColumnString(3);
  r.material_stats = stmt.ColumnString(4);
  r.status = stmt.ColumnString(5);
  r.attempt_count = stmt.ColumnInt(6);
  r.trigger_kind = stmt.ColumnString(7);
  r.debug_material_json = stmt.ColumnString(8);
  if (stmt.GetColumnType(9) != sql::ColumnType::kNull) {
    r.viewed_at = TimeFromInt(stmt.ColumnInt64(9));
  }
  r.created_at = TimeFromInt(stmt.ColumnInt64(10));
  return r;
}
constexpr char kDreamReportColumns[] =
    "id, dream_date, report_markdown, habit_candidates, material_stats, "
    "status, attempt_count, trigger_kind, debug_material_json, viewed_at, "
    "created_at";
}  // namespace

std::optional<DreamReport> DaoAgentMemoryStore::GetDreamReportByDate(
    const std::string& date) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  sql::Statement stmt(db_->GetUniqueStatement(
      base::StrCat({"SELECT ", kDreamReportColumns,
                    " FROM dream_reports WHERE dream_date = ?"})));
  stmt.BindString(0, date);
  if (!stmt.Step()) {
    return std::nullopt;
  }
  return DreamReportFromStatement(stmt);
}

std::optional<DreamReport>
DaoAgentMemoryStore::GetLatestUnviewedDreamReport() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  sql::Statement stmt(db_->GetUniqueStatement(
      base::StrCat({"SELECT ", kDreamReportColumns,
                    " FROM dream_reports WHERE viewed_at IS NULL "
                    "AND status = 'completed' "
                    "ORDER BY dream_date DESC LIMIT 1"})));
  if (!stmt.Step()) {
    return std::nullopt;
  }
  return DreamReportFromStatement(stmt);
}

bool DaoAgentMemoryStore::MarkDreamReportViewed(int64_t id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  sql::Statement stmt(db_->GetUniqueStatement(
      "UPDATE dream_reports SET viewed_at = ? WHERE id = ?"));
  stmt.BindInt64(0, TimeToInt(base::Time::Now()));
  stmt.BindInt64(1, id);
  return stmt.Run();
}
```

  Add `#include "base/strings/strcat.h"` if not already included. Place the anonymous-namespace helpers above the first method that uses them (file-scope anon namespace near the existing `TimeToInt` helpers is cleanest — move them there).

---

### Task 3: Memory service async wrappers for dream reports

**Files:**
- Modify: `src/dao/browser/agent/dao_agent_memory_service.h`
- Modify: `src/dao/browser/agent/dao_agent_memory_service.cc`

- [ ] **Step 3.1: Declare in `dao_agent_memory_service.h`** (after the Stats block):

```cpp
  // Dream reports
  void SaveDreamReport(DreamReport report,
                       base::OnceCallback<void(bool)> callback);
  void GetDreamReportByDate(
      const std::string& date,
      base::OnceCallback<void(std::optional<DreamReport>)> callback);
  void GetLatestUnviewedDreamReport(
      base::OnceCallback<void(std::optional<DreamReport>)> callback);
  void MarkDreamReportViewed(int64_t id,
                             base::OnceCallback<void(bool)> callback);
```

  Add `#include <optional>` to the header if missing.

- [ ] **Step 3.2: Implement in `dao_agent_memory_service.cc`** — same `PostTaskAndReplyWithResult` pattern as every other method:

```cpp
// --- Dream Reports ---

void DaoAgentMemoryService::SaveDreamReport(
    DreamReport report,
    base::OnceCallback<void(bool)> callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(ui_sequence_checker_);
  background_task_runner_->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(
          [](DaoAgentMemoryStore* store, DreamReport r) {
            return store->SaveDreamReport(r);
          },
          store_.get(), std::move(report)),
      std::move(callback));
}

void DaoAgentMemoryService::GetDreamReportByDate(
    const std::string& date,
    base::OnceCallback<void(std::optional<DreamReport>)> callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(ui_sequence_checker_);
  background_task_runner_->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(
          [](DaoAgentMemoryStore* store, std::string d) {
            return store->GetDreamReportByDate(d);
          },
          store_.get(), date),
      std::move(callback));
}

void DaoAgentMemoryService::GetLatestUnviewedDreamReport(
    base::OnceCallback<void(std::optional<DreamReport>)> callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(ui_sequence_checker_);
  background_task_runner_->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(
          [](DaoAgentMemoryStore* store) {
            return store->GetLatestUnviewedDreamReport();
          },
          store_.get()),
      std::move(callback));
}

void DaoAgentMemoryService::MarkDreamReportViewed(
    int64_t id,
    base::OnceCallback<void(bool)> callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(ui_sequence_checker_);
  background_task_runner_->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(
          [](DaoAgentMemoryStore* store, int64_t report_id) {
            return store->MarkDreamReportViewed(report_id);
          },
          store_.get(), id),
      std::move(callback));
}
```

---

### Task 4: DreamMaterialCollector

**Files:**
- Create: `src/dao/browser/agent/dao_dream_material_collector.h`
- Create: `src/dao/browser/agent/dao_dream_material_collector.cc`

Collects 4 sources in parallel, joins with `base::BarrierClosure`, produces one `base::Value::Dict`. **Hard privacy rule: no full URL ever enters the output dict** — history is reduced to domain+title+buckets; search URLs are reduced to the query text in C++.

- [ ] **Step 4.1: Write `dao_dream_material_collector.h`:**

```cpp
// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_AGENT_DAO_DREAM_MATERIAL_COLLECTOR_H_
#define DAO_BROWSER_AGENT_DAO_DREAM_MATERIAL_COLLECTOR_H_

#include <string>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/task/cancelable_task_tracker.h"
#include "base/time/time.h"
#include "base/values.h"

class Profile;

namespace dao {

class DaoAgentMemoryService;

// Collects the material pack for one dream run: browsing history
// (domain + title granularity), search keywords, agent conversation
// excerpts, and action-feedback stats for the window
// [window_start, window_end). All queries run in parallel; the callback
// fires on the UI thread with a Dict shaped as:
// {
//   "window": {"start": "...", "end": "..."},
//   "history": [{"domain","visit_count","titles":[..],
//                "buckets":{"morning":N,"afternoon":N,"evening":N,"night":N}}],
//   "search_queries": ["...", ...],
//   "conversations": [{"session_id","messages":["...",...]}],
//   "feedback": [{"scenario_id","shown","clicked","dismissed"}],
//   "stats": {"history_domains":N,"search_queries":N,
//             "conversation_sessions":N,"feedback_scenarios":N}
// }
// PRIVACY INVARIANT: no full URL appears anywhere in the output.
class DreamMaterialCollector {
 public:
  static constexpr int kMaxDomains = 50;
  static constexpr int kMaxSearchQueries = 30;
  static constexpr int kMaxConversationSessions = 10;
  static constexpr int kMaxTitlesPerDomain = 5;

  using CollectCallback = base::OnceCallback<void(base::Value::Dict)>;

  DreamMaterialCollector(Profile* profile,
                         DaoAgentMemoryService* memory_service);
  ~DreamMaterialCollector();

  DreamMaterialCollector(const DreamMaterialCollector&) = delete;
  DreamMaterialCollector& operator=(const DreamMaterialCollector&) = delete;

  // Only one collection may be in flight at a time.
  void Collect(base::Time window_start,
               base::Time window_end,
               CollectCallback callback);

  // Extracts a search query from a known search-engine URL. Returns the
  // empty string when `url_spec` is not a recognized search URL. Public
  // and static for unit testing.
  static std::string ExtractSearchQuery(const std::string& url_spec);

 private:
  void OnHistoryResults(base::Value::List domains,
                        base::Value::List queries);
  void OnConversationsLoaded(base::Value::List sessions);
  void OnFeedbackLoaded(base::Value::List feedback);
  void OnPartDone();

  raw_ptr<Profile> profile_;
  raw_ptr<DaoAgentMemoryService> memory_service_;

  base::Time window_start_;
  base::Time window_end_;
  CollectCallback callback_;
  base::RepeatingClosure barrier_;

  base::Value::List history_part_;
  base::Value::List search_part_;
  base::Value::List conversations_part_;
  base::Value::List feedback_part_;

  base::CancelableTaskTracker history_tracker_;
  base::WeakPtrFactory<DreamMaterialCollector> weak_factory_{this};
};

}  // namespace dao

#endif  // DAO_BROWSER_AGENT_DAO_DREAM_MATERIAL_COLLECTOR_H_
```

- [ ] **Step 4.2: Write `dao_dream_material_collector.cc`.** Key implementation parts (complete file):

```cpp
// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/agent/dao_dream_material_collector.h"

#include <algorithm>
#include <map>
#include <set>
#include <utility>
#include <vector>

#include "base/barrier_closure.h"
#include "base/functional/bind.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "chrome/browser/history/history_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "components/history/core/browser/history_service.h"
#include "components/history/core/browser/history_types.h"
#include "dao/browser/agent/dao_agent_memory_service.h"
#include "net/base/url_util.h"
#include "url/gurl.h"

namespace dao {

namespace {

// Search engines whose query parameter we extract. The URL itself is
// discarded after extraction (privacy invariant: URLs never leave C++).
struct SearchEngine {
  const char* host_suffix;
  const char* query_param;
};
constexpr SearchEngine kSearchEngines[] = {
    {"google.com", "q"},   {"bing.com", "q"},
    {"duckduckgo.com", "q"}, {"kagi.com", "q"},
    {"baidu.com", "wd"},   {"search.brave.com", "q"},
};

// Local-time bucket for a visit: morning 06-12, afternoon 12-18,
// evening 18-22, night 22-06.
const char* BucketFor(base::Time t) {
  base::Time::Exploded e;
  t.LocalExplode(&e);
  if (e.hour >= 6 && e.hour < 12) return "morning";
  if (e.hour >= 12 && e.hour < 18) return "afternoon";
  if (e.hour >= 18 && e.hour < 22) return "evening";
  return "night";
}

struct DomainAgg {
  int visit_count = 0;
  std::vector<std::string> titles;
  std::map<std::string, int> buckets;
};

}  // namespace

DreamMaterialCollector::DreamMaterialCollector(
    Profile* profile,
    DaoAgentMemoryService* memory_service)
    : profile_(profile), memory_service_(memory_service) {}

DreamMaterialCollector::~DreamMaterialCollector() = default;

// static
std::string DreamMaterialCollector::ExtractSearchQuery(
    const std::string& url_spec) {
  GURL url(url_spec);
  if (!url.is_valid() || !url.has_host()) {
    return std::string();
  }
  const std::string host = url.host();
  for (const auto& engine : kSearchEngines) {
    const std::string suffix(engine.host_suffix);
    const bool match =
        host == suffix ||
        (host.size() > suffix.size() &&
         host.compare(host.size() - suffix.size() - 1, std::string::npos,
                      "." + suffix) == 0);
    if (!match) {
      continue;
    }
    std::string query;
    if (net::GetValueForKeyInQuery(url, engine.query_param, &query) &&
        !query.empty()) {
      return query;
    }
    return std::string();
  }
  return std::string();
}

void DreamMaterialCollector::Collect(base::Time window_start,
                                     base::Time window_end,
                                     CollectCallback callback) {
  DCHECK(callback_.is_null()) << "Collection already in flight";
  window_start_ = window_start;
  window_end_ = window_end;
  callback_ = std::move(callback);
  history_part_.clear();
  search_part_.clear();
  conversations_part_.clear();
  feedback_part_.clear();

  // 3 parts: history(+search, same query), conversations, feedback.
  barrier_ = base::BarrierClosure(
      3, base::BindOnce(&DreamMaterialCollector::OnPartDone,
                        weak_factory_.GetWeakPtr()));

  // Part 1: history → domains + search queries.
  history::HistoryService* history =
      HistoryServiceFactory::GetForProfile(
          profile_, ServiceAccessType::EXPLICIT_ACCESS);
  if (!history) {
    barrier_.Run();
  } else {
    history::QueryOptions options;
    options.begin_time = window_start;
    options.end_time = window_end;
    options.max_count = 0;  // everything in range
    options.duplicate_policy = history::QueryOptions::KEEP_ALL_DUPLICATES;
    history->QueryHistory(
        std::u16string(), options,
        base::BindOnce(
            [](base::WeakPtr<DreamMaterialCollector> self,
               history::QueryResults results) {
              if (!self) {
                return;
              }
              std::map<std::string, DomainAgg> by_domain;
              std::vector<std::string> queries;
              std::set<std::string> seen_queries;
              for (const auto& row : results) {
                // Search-query extraction first (uses URL, then drops it).
                std::string q =
                    ExtractSearchQuery(row.url().spec());
                if (!q.empty() && seen_queries.insert(q).second &&
                    queries.size() <
                        static_cast<size_t>(kMaxSearchQueries)) {
                  queries.push_back(q);
                }
                // Domain aggregation — only domain + title survive.
                const std::string domain = row.url().host();
                if (domain.empty()) {
                  continue;
                }
                DomainAgg& agg = by_domain[domain];
                agg.visit_count++;
                agg.buckets[BucketFor(row.visit_time())]++;
                const std::string title =
                    base::UTF16ToUTF8(row.title());
                if (!title.empty() &&
                    agg.titles.size() <
                        static_cast<size_t>(kMaxTitlesPerDomain) &&
                    std::find(agg.titles.begin(), agg.titles.end(),
                              title) == agg.titles.end()) {
                  agg.titles.push_back(title);
                }
              }
              // Top-N domains by visit count.
              std::vector<std::pair<std::string, DomainAgg>> sorted(
                  std::make_move_iterator(by_domain.begin()),
                  std::make_move_iterator(by_domain.end()));
              std::sort(sorted.begin(), sorted.end(),
                        [](const auto& a, const auto& b) {
                          return a.second.visit_count >
                                 b.second.visit_count;
                        });
              if (sorted.size() > static_cast<size_t>(kMaxDomains)) {
                sorted.resize(kMaxDomains);
              }
              base::Value::List domains;
              for (auto& [domain, agg] : sorted) {
                base::Value::Dict d;
                d.Set("domain", domain);
                d.Set("visit_count", agg.visit_count);
                base::Value::List titles;
                for (auto& t : agg.titles) {
                  titles.Append(t);
                }
                d.Set("titles", std::move(titles));
                base::Value::Dict buckets;
                for (auto& [name, count] : agg.buckets) {
                  buckets.Set(name, count);
                }
                d.Set("buckets", std::move(buckets));
                domains.Append(std::move(d));
              }
              base::Value::List query_list;
              for (auto& q : queries) {
                query_list.Append(q);
              }
              self->OnHistoryResults(std::move(domains),
                                     std::move(query_list));
            },
            weak_factory_.GetWeakPtr()),
        &history_tracker_);
  }

  // Part 2: agent conversations in window.
  memory_service_->LoadRecentMessages(
      500,
      base::BindOnce(
          [](base::WeakPtr<DreamMaterialCollector> self,
             std::vector<ConversationMessage> messages) {
            if (!self) {
              return;
            }
            // Group by session; keep first 2 user messages per session.
            std::map<std::string, std::vector<std::string>> by_session;
            std::vector<std::string> session_order;
            for (const auto& msg : messages) {
              if (msg.role != "user") {
                continue;
              }
              if (msg.timestamp < self->window_start_ ||
                  msg.timestamp >= self->window_end_) {
                continue;
              }
              auto it = by_session.find(msg.session_id);
              if (it == by_session.end()) {
                if (by_session.size() >=
                    static_cast<size_t>(kMaxConversationSessions)) {
                  continue;
                }
                session_order.push_back(msg.session_id);
                it = by_session.emplace(msg.session_id,
                                        std::vector<std::string>())
                         .first;
              }
              if (it->second.size() < 2) {
                it->second.push_back(msg.content);
              }
            }
            base::Value::List sessions;
            for (const auto& sid : session_order) {
              base::Value::Dict s;
              s.Set("session_id", sid);
              base::Value::List msgs;
              for (const auto& m : by_session[sid]) {
                msgs.Append(m);
              }
              s.Set("messages", std::move(msgs));
              sessions.Append(std::move(s));
            }
            self->OnConversationsLoaded(std::move(sessions));
          },
          weak_factory_.GetWeakPtr()));

  // Part 3: action feedback stats. There is no windowed aggregate API on
  // the service; reuse GetPersonalScenarios for ids and surface lifetime
  // stats. v1 keeps it simple: scenario stat columns already aggregate.
  memory_service_->GetPersonalScenarios(base::BindOnce(
      [](base::WeakPtr<DreamMaterialCollector> self,
         std::vector<ScenarioDefinition> scenarios) {
        if (!self) {
          return;
        }
        base::Value::List feedback;
        for (const auto& s : scenarios) {
          if (s.times_triggered == 0) {
            continue;
          }
          base::Value::Dict f;
          f.Set("scenario_id", s.id);
          f.Set("name", s.name);
          f.Set("shown", s.times_triggered);
          f.Set("clicked", s.times_accepted);
          f.Set("dismissed", s.times_dismissed);
          feedback.Append(std::move(f));
        }
        self->OnFeedbackLoaded(std::move(feedback));
      },
      weak_factory_.GetWeakPtr()));
}

void DreamMaterialCollector::OnHistoryResults(base::Value::List domains,
                                              base::Value::List queries) {
  history_part_ = std::move(domains);
  search_part_ = std::move(queries);
  barrier_.Run();
}

void DreamMaterialCollector::OnConversationsLoaded(
    base::Value::List sessions) {
  conversations_part_ = std::move(sessions);
  barrier_.Run();
}

void DreamMaterialCollector::OnFeedbackLoaded(base::Value::List feedback) {
  feedback_part_ = std::move(feedback);
  barrier_.Run();
}

void DreamMaterialCollector::OnPartDone() {
  base::Value::Dict pack;
  base::Value::Dict window;
  window.Set("start", base::TimeFormatAsIso8601(window_start_));
  window.Set("end", base::TimeFormatAsIso8601(window_end_));
  pack.Set("window", std::move(window));
  base::Value::Dict stats;
  stats.Set("history_domains", static_cast<int>(history_part_.size()));
  stats.Set("search_queries", static_cast<int>(search_part_.size()));
  stats.Set("conversation_sessions",
            static_cast<int>(conversations_part_.size()));
  stats.Set("feedback_scenarios", static_cast<int>(feedback_part_.size()));
  pack.Set("stats", std::move(stats));
  pack.Set("history", std::move(history_part_));
  pack.Set("search_queries", std::move(search_part_));
  pack.Set("conversations", std::move(conversations_part_));
  pack.Set("feedback", std::move(feedback_part_));
  std::move(callback_).Run(std::move(pack));
}

}  // namespace dao
```

  Note: `base::TimeFormatAsIso8601` needs `#include "base/i18n/time_formatting.h"` and dep `//base:i18n` — if that dep is awkward in the GN graph, replace with `base::UnlocalizedTimeFormatWithPattern(t, "yyyy-MM-dd HH:mm")` from the same header, or simply `TimeToInt`-style epoch micros plus a human "YYYY-MM-DD" string composed via `base::Time::Exploded`. Prefer the Exploded approach to avoid new deps:

```cpp
std::string FormatLocalYmdHm(base::Time t) {
  base::Time::Exploded e;
  t.LocalExplode(&e);
  return base::StringPrintf("%04d-%02d-%02d %02d:%02d", e.year, e.month,
                            e.day_of_month, e.hour, e.minute);
}
```

  (with `#include "base/strings/stringprintf.h"`), used for both window strings.

---

### Task 5: DaoDreamService (scheduler + state machine + factory)

**Files:**
- Create: `src/dao/browser/agent/dao_dream_service.h`
- Create: `src/dao/browser/agent/dao_dream_service.cc`
- Create: `src/dao/browser/agent/dao_dream_service_factory.h`
- Create: `src/dao/browser/agent/dao_dream_service_factory.cc`

- [ ] **Step 5.1: Write `dao_dream_service.h`:**

```cpp
// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_AGENT_DAO_DREAM_SERVICE_H_
#define DAO_BROWSER_AGENT_DAO_DREAM_SERVICE_H_

#include <memory>
#include <optional>
#include <string>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/time/clock.h"
#include "base/time/default_clock.h"
#include "base/timer/timer.h"
#include "base/values.h"
#include "components/keyed_service/core/keyed_service.h"
#include "dao/browser/agent/dao_agent_memory_types.h"

class Profile;

namespace dao {

class DaoAgentMemoryService;
class DreamMaterialCollector;

// Orchestrates the Dream Analysis pipeline: trigger decision (nightly /
// catch-up / manual), material collection, hand-off to the agent WebUI
// for LLM summarization, and persistence of results. The LLM call itself
// happens in the resident agent WebUI (see DaoAgentDreamHandler);
// this service exposes the material pack to it and receives the result.
class DaoDreamService : public KeyedService {
 public:
  enum class State { kIdle, kCollecting, kDreaming, kSaving };
  enum class TriggerKind { kNightly, kCatchUp, kManual };

  // The WebUI side registers itself here to receive dream-run requests.
  class Runner {
   public:
    virtual ~Runner() = default;
    // Asks the WebUI to run the LLM summarization for `material`.
    // The runner replies through OnDreamResult / OnDreamFailed.
    virtual void RunDream(const std::string& dream_date,
                          const base::Value::Dict& material) = 0;
  };

  DaoDreamService(Profile* profile, DaoAgentMemoryService* memory_service);
  ~DaoDreamService() override;

  DaoDreamService(const DaoDreamService&) = delete;
  DaoDreamService& operator=(const DaoDreamService&) = delete;

  // KeyedService:
  void Shutdown() override;

  // Test hooks: inject a clock and an idle-seconds supplier.
  void SetClockForTesting(const base::Clock* clock) { clock_ = clock; }
  void SetIdleTimeCallbackForTesting(
      base::RepeatingCallback<int()> callback) {
    idle_time_callback_ = std::move(callback);
  }
  // Runs one scheduler tick immediately (test convenience).
  void TickForTesting() { OnSchedulerTick(); }

  State state() const { return state_; }

  // WebUI runner registration (one at a time; last wins).
  void SetRunner(Runner* runner);
  void ClearRunner(Runner* runner);

  // Manual trigger from the settings UI. Bypasses night/idle/done checks.
  // `callback` fires when the run completes (true) or fails (false).
  void StartManualDream(base::OnceCallback<void(bool)> callback);

  // Result entry points called by the WebUI handler.
  void OnDreamResult(const std::string& dream_date,
                     base::Value::Dict result);
  void OnDreamFailed(const std::string& dream_date,
                     const std::string& error);

  // Computes the dream-day label ("YYYY-MM-DD", local) for `now`.
  // 22:00–24:00 → today; 00:00–06:00 → yesterday; daytime → today.
  static std::string DreamDateFor(base::Time now);

  // Material window [start, end) for a dream date: date 06:00 local →
  // min(date+1 06:00, now).
  static void MaterialWindowFor(const std::string& dream_date,
                                base::Time now,
                                base::Time* start,
                                base::Time* end);

  // True if local time is within the night window 22:00–06:00.
  static bool IsNightTime(base::Time now);

 private:
  void OnSchedulerTick();
  void MaybeStartNightly();
  void MaybeStartCatchUp();
  void StartDream(const std::string& dream_date, TriggerKind kind);
  void OnExistingReportChecked(const std::string& dream_date,
                               TriggerKind kind,
                               std::optional<DreamReport> existing);
  void OnMaterialCollected(const std::string& dream_date,
                           TriggerKind kind,
                           base::Value::Dict material);
  void OnDreamTimeout(const std::string& dream_date);
  void MarkFailed(const std::string& dream_date, const std::string& error);
  void PersistResult(const std::string& dream_date,
                     base::Value::Dict result);
  void FinishRun(bool success);

  int GetIdleSeconds() const;
  bool DreamPrefEnabled() const;

  static constexpr base::TimeDelta kTickInterval = base::Minutes(5);
  static constexpr int kNightlyIdleSeconds = 3600;
  static constexpr int kCatchUpIdleSeconds = 600;
  static constexpr base::TimeDelta kDreamTimeout = base::Minutes(5);
  static constexpr int kMaxAttemptsPerNight = 3;  // 1 initial + 2 retries
  static constexpr double kMaxLLMConfidence = 0.8;

  raw_ptr<Profile> profile_;
  raw_ptr<DaoAgentMemoryService> memory_service_;
  raw_ptr<Runner> runner_ = nullptr;
  raw_ptr<const base::Clock> clock_ =
      base::DefaultClock::GetInstance();
  base::RepeatingCallback<int()> idle_time_callback_;

  State state_ = State::kIdle;
  std::string active_dream_date_;
  TriggerKind active_kind_ = TriggerKind::kNightly;
  std::string pending_debug_material_json_;
  base::Value::Dict pending_material_stats_;
  base::OnceCallback<void(bool)> manual_callback_;

  base::RepeatingTimer tick_timer_;
  base::OneShotTimer dream_timeout_timer_;
  std::unique_ptr<DreamMaterialCollector> collector_;

  base::WeakPtrFactory<DaoDreamService> weak_factory_{this};
};

}  // namespace dao

#endif  // DAO_BROWSER_AGENT_DAO_DREAM_SERVICE_H_
```

- [ ] **Step 5.2: Write `dao_dream_service.cc`.** Complete implementation:

```cpp
// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/agent/dao_dream_service.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/strings/stringprintf.h"
#include "chrome/browser/profiles/profile.h"
#include "components/prefs/pref_service.h"
#include "dao/browser/agent/dao_agent_memory_service.h"
#include "dao/browser/agent/dao_dream_material_collector.h"
#include "dao/browser/dao_pref_names.h"
#include "ui/base/idle/idle.h"

namespace dao {

namespace {

std::string FormatYmd(base::Time t) {
  base::Time::Exploded e;
  t.LocalExplode(&e);
  return base::StringPrintf("%04d-%02d-%02d", e.year, e.month,
                            e.day_of_month);
}

// Parses "YYYY-MM-DD" into local midnight. Returns false on bad input.
bool ParseYmd(const std::string& ymd, base::Time* out) {
  int y = 0, m = 0, d = 0;
  if (sscanf(ymd.c_str(), "%d-%d-%d", &y, &m, &d) != 3) {
    return false;
  }
  base::Time::Exploded e = {};
  e.year = y;
  e.month = m;
  e.day_of_month = d;
  return base::Time::FromLocalExploded(e, out);
}

}  // namespace

DaoDreamService::DaoDreamService(Profile* profile,
                                 DaoAgentMemoryService* memory_service)
    : profile_(profile), memory_service_(memory_service) {
  collector_ =
      std::make_unique<DreamMaterialCollector>(profile, memory_service);
  tick_timer_.Start(FROM_HERE, kTickInterval,
                    base::BindRepeating(&DaoDreamService::OnSchedulerTick,
                                        weak_factory_.GetWeakPtr()));
}

DaoDreamService::~DaoDreamService() = default;

void DaoDreamService::Shutdown() {
  tick_timer_.Stop();
  dream_timeout_timer_.Stop();
  runner_ = nullptr;
  weak_factory_.InvalidateWeakPtrs();
}

void DaoDreamService::SetRunner(Runner* runner) {
  runner_ = runner;
}

void DaoDreamService::ClearRunner(Runner* runner) {
  if (runner_ == runner) {
    runner_ = nullptr;
  }
}

// static
std::string DaoDreamService::DreamDateFor(base::Time now) {
  base::Time::Exploded e;
  now.LocalExplode(&e);
  if (e.hour < 6) {
    return FormatYmd(now - base::Days(1));
  }
  return FormatYmd(now);
}

// static
void DaoDreamService::MaterialWindowFor(const std::string& dream_date,
                                        base::Time now,
                                        base::Time* start,
                                        base::Time* end) {
  base::Time midnight;
  if (!ParseYmd(dream_date, &midnight)) {
    *start = now - base::Hours(24);
    *end = now;
    return;
  }
  *start = midnight + base::Hours(6);
  base::Time hard_end = midnight + base::Hours(30);  // next day 06:00
  *end = std::min(hard_end, now);
}

// static
bool DaoDreamService::IsNightTime(base::Time now) {
  base::Time::Exploded e;
  now.LocalExplode(&e);
  return e.hour >= 22 || e.hour < 6;
}

int DaoDreamService::GetIdleSeconds() const {
  if (idle_time_callback_) {
    return idle_time_callback_.Run();
  }
  return ui::CalculateIdleTime();
}

bool DaoDreamService::DreamPrefEnabled() const {
  PrefService* prefs = profile_->GetPrefs();
  return prefs->GetBoolean(prefs::kDaoAgentMemoryEnabled) &&
         prefs->GetBoolean(prefs::kDaoDreamEnabled);
}

void DaoDreamService::OnSchedulerTick() {
  if (!DreamPrefEnabled() || state_ != State::kIdle || !runner_) {
    return;
  }
  const base::Time now = clock_->Now();
  if (IsNightTime(now) && GetIdleSeconds() >= kNightlyIdleSeconds) {
    MaybeStartNightly();
    return;
  }
  if (GetIdleSeconds() >= kCatchUpIdleSeconds) {
    MaybeStartCatchUp();
  }
}

void DaoDreamService::MaybeStartNightly() {
  const std::string date = DreamDateFor(clock_->Now());
  memory_service_->GetDreamReportByDate(
      date, base::BindOnce(&DaoDreamService::OnExistingReportChecked,
                           weak_factory_.GetWeakPtr(), date,
                           TriggerKind::kNightly));
}

void DaoDreamService::MaybeStartCatchUp() {
  const std::string yesterday =
      FormatYmd(clock_->Now() - base::Days(1));
  memory_service_->GetDreamReportByDate(
      yesterday, base::BindOnce(&DaoDreamService::OnExistingReportChecked,
                                weak_factory_.GetWeakPtr(), yesterday,
                                TriggerKind::kCatchUp));
}

void DaoDreamService::OnExistingReportChecked(
    const std::string& dream_date,
    TriggerKind kind,
    std::optional<DreamReport> existing) {
  if (state_ != State::kIdle) {
    return;  // another path won the race
  }
  if (existing) {
    if (existing->status == "completed") {
      return;  // already dreamed
    }
    if (existing->attempt_count >= kMaxAttemptsPerNight) {
      return;  // give up for this date
    }
  }
  StartDream(dream_date, kind);
}

void DaoDreamService::StartDream(const std::string& dream_date,
                                 TriggerKind kind) {
  state_ = State::kCollecting;
  active_dream_date_ = dream_date;
  active_kind_ = kind;

  base::Time start, end;
  MaterialWindowFor(dream_date, clock_->Now(), &start, &end);
  collector_->Collect(
      start, end,
      base::BindOnce(&DaoDreamService::OnMaterialCollected,
                     weak_factory_.GetWeakPtr(), dream_date, kind));
}

void DaoDreamService::OnMaterialCollected(const std::string& dream_date,
                                          TriggerKind kind,
                                          base::Value::Dict material) {
  // Empty material → silent skip (not a failure).
  const base::Value::List* history = material.FindList("history");
  const base::Value::List* convs = material.FindList("conversations");
  if ((!history || history->empty()) && (!convs || convs->empty())) {
    FinishRun(false);
    return;
  }
  if (!runner_) {
    MarkFailed(dream_date, "agent webui unavailable");
    return;
  }

  // Persist debug input if debug mode is on.
  pending_debug_material_json_.clear();
  if (profile_->GetPrefs()->GetBoolean(prefs::kDaoDreamDebug)) {
    base::JSONWriter::WriteWithOptions(
        material, base::JSONWriter::OPTIONS_PRETTY_PRINT,
        &pending_debug_material_json_);
  }
  if (const base::Value::Dict* stats = material.FindDict("stats")) {
    pending_material_stats_ = stats->Clone();
  }

  state_ = State::kDreaming;
  dream_timeout_timer_.Start(
      FROM_HERE, kDreamTimeout,
      base::BindOnce(&DaoDreamService::OnDreamTimeout,
                     weak_factory_.GetWeakPtr(), dream_date));
  runner_->RunDream(dream_date, material);
}

void DaoDreamService::OnDreamTimeout(const std::string& dream_date) {
  if (state_ != State::kDreaming || dream_date != active_dream_date_) {
    return;
  }
  MarkFailed(dream_date, "timeout");
}

void DaoDreamService::OnDreamResult(const std::string& dream_date,
                                    base::Value::Dict result) {
  if (state_ != State::kDreaming || dream_date != active_dream_date_) {
    return;  // stale result (timeout already fired, or unexpected)
  }
  dream_timeout_timer_.Stop();
  state_ = State::kSaving;
  PersistResult(dream_date, std::move(result));
}

void DaoDreamService::OnDreamFailed(const std::string& dream_date,
                                    const std::string& error) {
  if (state_ != State::kDreaming || dream_date != active_dream_date_) {
    return;
  }
  dream_timeout_timer_.Stop();
  MarkFailed(dream_date, error);
}

void DaoDreamService::MarkFailed(const std::string& dream_date,
                                 const std::string& error) {
  LOG(ERROR) << "Dream run failed for " << dream_date << ": " << error;
  // Read existing attempt count, then write a failed row with +1.
  memory_service_->GetDreamReportByDate(
      dream_date,
      base::BindOnce(
          [](base::WeakPtr<DaoDreamService> self, std::string date,
             std::optional<DreamReport> existing) {
            if (!self) {
              return;
            }
            DreamReport report;
            if (existing) {
              report = std::move(*existing);
            }
            report.dream_date = date;
            report.status = "failed";
            report.attempt_count += 1;
            report.trigger_kind =
                self->active_kind_ == TriggerKind::kManual ? "manual"
                : self->active_kind_ == TriggerKind::kCatchUp
                    ? "catchup"
                    : "nightly";
            self->memory_service_->SaveDreamReport(
                std::move(report),
                base::BindOnce(
                    [](base::WeakPtr<DaoDreamService> self, bool ok) {
                      if (self) {
                        self->FinishRun(false);
                      }
                    },
                    self));
          },
          weak_factory_.GetWeakPtr(), dream_date));
}

void DaoDreamService::PersistResult(const std::string& dream_date,
                                    base::Value::Dict result) {
  // 1. Habits → MergePreference (confidence capped; contradict skipped).
  if (const base::Value::List* habits = result.FindList("habits")) {
    for (const base::Value& h : *habits) {
      const base::Value::Dict* habit = h.GetIfDict();
      if (!habit) {
        continue;
      }
      const std::string* key = habit->FindString("key");
      const std::string* value = habit->FindString("value");
      const std::string* relation = habit->FindString("relation");
      if (!key || !value || key->empty() || value->empty()) {
        continue;
      }
      if (relation && *relation == "contradict") {
        continue;  // surfaced in the report only; user adjudicates
      }
      double confidence =
          habit->FindDouble("confidence").value_or(0.5);
      confidence = std::min(confidence, kMaxLLMConfidence);
      memory_service_->MergePreference(*key, *value, confidence,
                                       base::DoNothing());
    }
  }

  // 2. Report row.
  DreamReport report;
  report.dream_date = dream_date;
  const std::string* md = result.FindString("report_markdown");
  report.report_markdown = md ? *md : "";
  if (const base::Value::List* habits = result.FindList("habits")) {
    base::JSONWriter::Write(*habits, &report.habit_candidates);
  }
  base::JSONWriter::Write(pending_material_stats_,
                          &report.material_stats);
  report.status = "completed";
  report.trigger_kind = active_kind_ == TriggerKind::kManual ? "manual"
                        : active_kind_ == TriggerKind::kCatchUp
                            ? "catchup"
                            : "nightly";
  report.debug_material_json = pending_debug_material_json_;
  memory_service_->SaveDreamReport(
      std::move(report),
      base::BindOnce(
          [](base::WeakPtr<DaoDreamService> self, bool ok) {
            if (self) {
              self->FinishRun(ok);
            }
          },
          weak_factory_.GetWeakPtr()));
}

void DaoDreamService::StartManualDream(
    base::OnceCallback<void(bool)> callback) {
  if (state_ != State::kIdle) {
    std::move(callback).Run(false);
    return;
  }
  if (!runner_) {
    std::move(callback).Run(false);
    return;
  }
  manual_callback_ = std::move(callback);
  StartDream(DreamDateFor(clock_->Now()), TriggerKind::kManual);
}

void DaoDreamService::FinishRun(bool success) {
  state_ = State::kIdle;
  active_dream_date_.clear();
  pending_debug_material_json_.clear();
  pending_material_stats_.clear();
  if (manual_callback_) {
    std::move(manual_callback_).Run(success);
  }
}

}  // namespace dao
```

- [ ] **Step 5.3: Write `dao_dream_service_factory.h`** (mirror `DaoAgentMemoryServiceFactory` exactly):

```cpp
// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_AGENT_DAO_DREAM_SERVICE_FACTORY_H_
#define DAO_BROWSER_AGENT_DAO_DREAM_SERVICE_FACTORY_H_

#include "base/no_destructor.h"
#include "components/keyed_service/content/browser_context_keyed_service_factory.h"

class Profile;

namespace dao {

class DaoDreamService;

class DaoDreamServiceFactory : public BrowserContextKeyedServiceFactory {
 public:
  // Returns nullptr when agent memory or the dream feature is disabled,
  // or for incognito profiles.
  static DaoDreamService* GetForProfile(Profile* profile);
  static DaoDreamServiceFactory* GetInstance();

 private:
  friend class base::NoDestructor<DaoDreamServiceFactory>;

  DaoDreamServiceFactory();
  ~DaoDreamServiceFactory() override;

  // BrowserContextKeyedServiceFactory:
  std::unique_ptr<KeyedService> BuildServiceInstanceForBrowserContext(
      content::BrowserContext* context) const override;
  content::BrowserContext* GetBrowserContextToUse(
      content::BrowserContext* context) const override;
};

}  // namespace dao

#endif  // DAO_BROWSER_AGENT_DAO_DREAM_SERVICE_FACTORY_H_
```

- [ ] **Step 5.4: Write `dao_dream_service_factory.cc`:**

```cpp
// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/agent/dao_dream_service_factory.h"

#include "chrome/browser/profiles/profile.h"
#include "components/keyed_service/content/browser_context_dependency_manager.h"
#include "components/prefs/pref_service.h"
#include "dao/browser/agent/dao_agent_memory_service.h"
#include "dao/browser/agent/dao_agent_memory_service_factory.h"
#include "dao/browser/agent/dao_dream_service.h"
#include "dao/browser/dao_pref_names.h"

namespace dao {

// static
DaoDreamService* DaoDreamServiceFactory::GetForProfile(Profile* profile) {
  if (!profile->GetPrefs()->GetBoolean(prefs::kDaoAgentMemoryEnabled) ||
      !profile->GetPrefs()->GetBoolean(prefs::kDaoDreamEnabled)) {
    return nullptr;
  }
  return static_cast<DaoDreamService*>(
      GetInstance()->GetServiceForBrowserContext(profile, /*create=*/true));
}

// static
DaoDreamServiceFactory* DaoDreamServiceFactory::GetInstance() {
  static base::NoDestructor<DaoDreamServiceFactory> instance;
  return instance.get();
}

DaoDreamServiceFactory::DaoDreamServiceFactory()
    : BrowserContextKeyedServiceFactory(
          "DaoDreamService",
          BrowserContextDependencyManager::GetInstance()) {
  DependsOn(DaoAgentMemoryServiceFactory::GetInstance());
}

DaoDreamServiceFactory::~DaoDreamServiceFactory() = default;

std::unique_ptr<KeyedService>
DaoDreamServiceFactory::BuildServiceInstanceForBrowserContext(
    content::BrowserContext* context) const {
  Profile* profile = Profile::FromBrowserContext(context);
  DaoAgentMemoryService* memory =
      DaoAgentMemoryServiceFactory::GetForProfile(profile);
  if (!memory) {
    return nullptr;
  }
  return std::make_unique<DaoDreamService>(profile, memory);
}

content::BrowserContext* DaoDreamServiceFactory::GetBrowserContextToUse(
    content::BrowserContext* context) const {
  Profile* profile = Profile::FromBrowserContext(context);
  if (profile->IsOffTheRecord()) {
    return nullptr;
  }
  return context;
}

}  // namespace dao
```

---

### Task 6: WebUI message handler (DaoAgentDreamHandler)

**Files:**
- Modify: `src/dao/browser/ui/webui/dao_agent_ui.h`
- Modify: `src/dao/browser/ui/webui/dao_agent_ui.cc`

The handler is the bridge: implements `DaoDreamService::Runner` (C++→JS push via `FireWebUIListener("dream-run", …)`) and exposes `chrome.send` messages (JS→C++).

- [ ] **Step 6.1: Declare `DaoAgentDreamHandler` in `dao_agent_ui.h`** (after `DaoAgentMemoryHandler`; add `#include "dao/browser/agent/dao_dream_service.h"` to the header's include block):

```cpp
// WebUI message handler for the Dream Analysis system. Registers itself
// as the DaoDreamService runner so the resident agent WebUI executes the
// LLM summarization for nightly dream runs.
class DaoAgentDreamHandler : public content::WebUIMessageHandler,
                             public DaoDreamService::Runner {
 public:
  DaoAgentDreamHandler();
  ~DaoAgentDreamHandler() override;

  // content::WebUIMessageHandler:
  void RegisterMessages() override;
  void OnJavascriptAllowed() override;
  void OnJavascriptDisallowed() override;

  // DaoDreamService::Runner:
  void RunDream(const std::string& dream_date,
                const base::Value::Dict& material) override;

 private:
  DaoDreamService* GetDreamService();

  void HandleDreamComplete(const base::ListValue& args);
  void HandleDreamFailed(const base::ListValue& args);
  void HandleGetDreamEnabled(const base::ListValue& args);
  void HandleSetDreamEnabled(const base::ListValue& args);
  void HandleGetDreamDebug(const base::ListValue& args);
  void HandleSetDreamDebug(const base::ListValue& args);
  void HandleStartManualDream(const base::ListValue& args);
  void HandleGetUnviewedDreamReport(const base::ListValue& args);
  void HandleMarkDreamReportViewed(const base::ListValue& args);

  base::WeakPtrFactory<DaoAgentDreamHandler> weak_factory_{this};
};
```

- [ ] **Step 6.2: Implement in `dao_agent_ui.cc`** (place after the `DaoAgentMemoryHandler` implementation; add includes `dao/browser/agent/dao_dream_service.h`, `dao/browser/agent/dao_dream_service_factory.h`):

```cpp
// ---- DaoAgentDreamHandler ----

DaoAgentDreamHandler::DaoAgentDreamHandler() = default;

DaoAgentDreamHandler::~DaoAgentDreamHandler() {
  if (DaoDreamService* service = GetDreamService()) {
    service->ClearRunner(this);
  }
}

DaoDreamService* DaoAgentDreamHandler::GetDreamService() {
  Profile* profile = Profile::FromWebUI(web_ui());
  return DaoDreamServiceFactory::GetForProfile(profile);
}

void DaoAgentDreamHandler::RegisterMessages() {
  web_ui()->RegisterMessageCallback(
      "dreamComplete",
      base::BindRepeating(&DaoAgentDreamHandler::HandleDreamComplete,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "dreamFailed",
      base::BindRepeating(&DaoAgentDreamHandler::HandleDreamFailed,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "getDreamEnabled",
      base::BindRepeating(&DaoAgentDreamHandler::HandleGetDreamEnabled,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "setDreamEnabled",
      base::BindRepeating(&DaoAgentDreamHandler::HandleSetDreamEnabled,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "getDreamDebug",
      base::BindRepeating(&DaoAgentDreamHandler::HandleGetDreamDebug,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "setDreamDebug",
      base::BindRepeating(&DaoAgentDreamHandler::HandleSetDreamDebug,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "startManualDream",
      base::BindRepeating(&DaoAgentDreamHandler::HandleStartManualDream,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "getUnviewedDreamReport",
      base::BindRepeating(
          &DaoAgentDreamHandler::HandleGetUnviewedDreamReport,
          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "markDreamReportViewed",
      base::BindRepeating(
          &DaoAgentDreamHandler::HandleMarkDreamReportViewed,
          base::Unretained(this)));
}

void DaoAgentDreamHandler::OnJavascriptAllowed() {
  if (DaoDreamService* service = GetDreamService()) {
    service->SetRunner(this);
  }
}

void DaoAgentDreamHandler::OnJavascriptDisallowed() {
  if (DaoDreamService* service = GetDreamService()) {
    service->ClearRunner(this);
  }
}

void DaoAgentDreamHandler::RunDream(const std::string& dream_date,
                                    const base::Value::Dict& material) {
  base::Value::Dict payload;
  payload.Set("dreamDate", dream_date);
  payload.Set("material", material.Clone());
  FireWebUIListener("dream-run", payload);
}

void DaoAgentDreamHandler::HandleDreamComplete(
    const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 2 || !args[0].is_string() || !args[1].is_dict()) {
    return;
  }
  if (DaoDreamService* service = GetDreamService()) {
    service->OnDreamResult(args[0].GetString(),
                           args[1].GetDict().Clone());
  }
}

void DaoAgentDreamHandler::HandleDreamFailed(const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 2 || !args[0].is_string() || !args[1].is_string()) {
    return;
  }
  if (DaoDreamService* service = GetDreamService()) {
    service->OnDreamFailed(args[0].GetString(), args[1].GetString());
  }
}

void DaoAgentDreamHandler::HandleGetDreamEnabled(
    const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 1 || !args[0].is_string()) {
    return;
  }
  Profile* profile = Profile::FromWebUI(web_ui());
  ResolveJavascriptCallback(
      base::Value(args[0].GetString()),
      base::Value(
          profile->GetPrefs()->GetBoolean(prefs::kDaoDreamEnabled)));
}

void DaoAgentDreamHandler::HandleSetDreamEnabled(
    const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 2 || !args[0].is_string() || !args[1].is_bool()) {
    return;
  }
  Profile* profile = Profile::FromWebUI(web_ui());
  profile->GetPrefs()->SetBoolean(prefs::kDaoDreamEnabled,
                                  args[1].GetBool());
  // Instantiate the service (and register us as runner) when enabling.
  if (args[1].GetBool()) {
    if (DaoDreamService* service = GetDreamService()) {
      service->SetRunner(this);
    }
  }
  ResolveJavascriptCallback(base::Value(args[0].GetString()),
                            base::Value(true));
}

void DaoAgentDreamHandler::HandleGetDreamDebug(
    const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 1 || !args[0].is_string()) {
    return;
  }
  Profile* profile = Profile::FromWebUI(web_ui());
  ResolveJavascriptCallback(
      base::Value(args[0].GetString()),
      base::Value(profile->GetPrefs()->GetBoolean(prefs::kDaoDreamDebug)));
}

void DaoAgentDreamHandler::HandleSetDreamDebug(
    const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 2 || !args[0].is_string() || !args[1].is_bool()) {
    return;
  }
  Profile* profile = Profile::FromWebUI(web_ui());
  profile->GetPrefs()->SetBoolean(prefs::kDaoDreamDebug,
                                  args[1].GetBool());
  ResolveJavascriptCallback(base::Value(args[0].GetString()),
                            base::Value(true));
}

void DaoAgentDreamHandler::HandleStartManualDream(
    const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 1 || !args[0].is_string()) {
    return;
  }
  const std::string callback_id = args[0].GetString();
  DaoDreamService* service = GetDreamService();
  if (!service) {
    RejectJavascriptCallback(base::Value(callback_id),
                             base::Value("dream service unavailable"));
    return;
  }
  service->SetRunner(this);
  service->StartManualDream(base::BindOnce(
      [](base::WeakPtr<DaoAgentDreamHandler> self,
         std::string callback_id, bool success) {
        if (!self) {
          return;
        }
        if (success) {
          self->ResolveJavascriptCallback(base::Value(callback_id),
                                          base::Value(true));
        } else {
          self->RejectJavascriptCallback(base::Value(callback_id),
                                         base::Value("dream run failed"));
        }
      },
      weak_factory_.GetWeakPtr(), callback_id));
}

void DaoAgentDreamHandler::HandleGetUnviewedDreamReport(
    const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 1 || !args[0].is_string()) {
    return;
  }
  const std::string callback_id = args[0].GetString();
  Profile* profile = Profile::FromWebUI(web_ui());
  DaoAgentMemoryService* memory =
      DaoAgentMemoryServiceFactory::GetForProfile(profile);
  if (!memory) {
    ResolveJavascriptCallback(base::Value(callback_id), base::Value());
    return;
  }
  memory->GetLatestUnviewedDreamReport(base::BindOnce(
      [](base::WeakPtr<DaoAgentDreamHandler> self, std::string callback_id,
         std::optional<DreamReport> report) {
        if (!self) {
          return;
        }
        if (!report) {
          self->ResolveJavascriptCallback(base::Value(callback_id),
                                          base::Value());
          return;
        }
        base::Value::Dict dict;
        dict.Set("id", static_cast<double>(report->id));
        dict.Set("dreamDate", report->dream_date);
        dict.Set("reportMarkdown", report->report_markdown);
        dict.Set("habitCandidates", report->habit_candidates);
        dict.Set("materialStats", report->material_stats);
        dict.Set("debugMaterialJson", report->debug_material_json);
        dict.Set("triggerKind", report->trigger_kind);
        self->ResolveJavascriptCallback(base::Value(callback_id), dict);
      },
      weak_factory_.GetWeakPtr(), callback_id));
}

void DaoAgentDreamHandler::HandleMarkDreamReportViewed(
    const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 2 || !args[0].is_string() || !args[1].is_double()) {
    return;
  }
  Profile* profile = Profile::FromWebUI(web_ui());
  DaoAgentMemoryService* memory =
      DaoAgentMemoryServiceFactory::GetForProfile(profile);
  if (memory) {
    memory->MarkDreamReportViewed(
        static_cast<int64_t>(args[1].GetDouble()), base::DoNothing());
  }
  ResolveJavascriptCallback(base::Value(args[0].GetString()),
                            base::Value(true));
}
```

  Note `args[1].is_double()` — `chrome.send` serializes JS numbers as doubles; `is_int()` would fail for ids ≥ 2^31. Keep the double cast.

- [ ] **Step 6.3: Register the handler** — in `dao_agent_ui.cc` where the other handlers are added for the main agent page (line ~4507):

```cpp
  web_ui->AddMessageHandler(std::make_unique<DaoAgentDreamHandler>());
```

  Add it ONLY to the main `chrome://dao-agent` controller, NOT to the skills-page controller (the second AddMessageHandler cluster at ~4549).

---

### Task 7: Factory registration patch + BUILD.gn patch

**Files:**
- Modify: `src/patches/chrome/browser/profiles/chrome_browser_main_extra_parts_profiles.cc.patch`
- Modify: `src/patches/chrome/browser/ui/BUILD.gn.patch`

- [ ] **Step 7.1: Add `DaoDreamServiceFactory::GetInstance()` to the profiles patch.** Edit the patch file to extend both hunks:
  - Include block: add `#include "dao/browser/agent/dao_dream_service_factory.h"` after the memory factory include.
  - `EnsureBrowserContextKeyedServiceFactoriesBuilt` block: add `dao::DaoDreamServiceFactory::GetInstance();` after the memory factory line.
  - **Patch-editing rule:** adjust the hunk line counts (`@@ -A,B +C,D @@`) — each hunk gains one `+` line, so `D` increases by 1. If hand-editing counts feels risky, the safer flow is: `npm run import`, edit `engine/src/chrome/browser/profiles/chrome_browser_main_extra_parts_profiles.cc` directly, then `npm run export -- chrome/browser/profiles/chrome_browser_main_extra_parts_profiles.cc`.

- [ ] **Step 7.2: Add new sources to `src/patches/chrome/browser/ui/BUILD.gn.patch`** in the same `+` list as the other agent files (alphabetical placement next to the memory entries):

```
+    "//dao/browser/agent/dao_dream_material_collector.cc",
+    "//dao/browser/agent/dao_dream_material_collector.h",
+    "//dao/browser/agent/dao_dream_service.cc",
+    "//dao/browser/agent/dao_dream_service.h",
+    "//dao/browser/agent/dao_dream_service_factory.cc",
+    "//dao/browser/agent/dao_dream_service_factory.h",
```

  Same hunk-count caveat as 7.1 (6 added lines) — or use the import→edit-engine→scoped-export flow. The deps `//ui/base/idle` and history are already in chrome/browser/ui's dep graph (verified: `//ui/base/idle` at engine BUILD.gn:1879), no new deps needed.

- [ ] **Step 7.3: Verify patches apply:** run `npm run import` and confirm no patch failures in the output.

---

### Task 8: dao_dream_runner.ts (WebUI LLM execution)

**Files:**
- Create: `src/dao/browser/ui/webui/resources/agent/dao_dream_runner.ts`
- Modify: `src/dao/browser/ui/webui/resources/agent/agent_bridge.ts` (add `addWebUIListener` support)
- Modify: `src/dao/browser/ui/webui/resources/agent/dao_agent_app.ts` (import the runner so it's always live)
- Modify: `src/dao/browser/ui/webui/resources/agent/BUILD.gn` (add to `ts_files`)

- [ ] **Step 8.1: Add C++→JS listener plumbing to `agent_bridge.ts`.** The agent bridge has `cr.webUIResponse` but no `webUIListenerCallback` (FireWebUIListener target). Mirror `sidebar_bridge.ts`'s implementation. After the `cr.webUIResponse` assignment add:

```typescript
// ---- WebUI Listener Infrastructure ----
// (mirrors Chromium's cr.ts: FireWebUIListener → webUIListenerCallback)

const webUiListenerMap:
    Record<string, Array<(...args: unknown[]) => void>> = {};

(cr as unknown as {
  webUIListenerCallback: (event: string, ...args: unknown[]) => void;
}).webUIListenerCallback = function(event: string, ...args: unknown[]) {
  const listeners = webUiListenerMap[event];
  if (!listeners) return;
  for (const cb of listeners) {
    cb(...args);
  }
};

export function addWebUIListener(
    event: string, callback: (...args: unknown[]) => void): void {
  (webUiListenerMap[event] = webUiListenerMap[event] || []).push(callback);
}
```

  **Check first** whether `proactiveSuggestion` already has a working receive path — grep `dao_chat_view.ts` for how it consumes suggestions. As of planning, no `webUIListenerCallback` exists in agent resources, so `FireWebUIListener("proactiveSuggestion", …)` currently has no JS receiver; our addition makes `dream-run` (and incidentally future listeners) receivable without touching existing behavior.

- [ ] **Step 8.2: Write `dao_dream_runner.ts`:**

```typescript
// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Dream Analysis LLM runner. Listens for `dream-run` pushes from
// DaoDreamService (via DaoAgentDreamHandler), executes a single-shot LLM
// summarization over the material pack using the user's active provider,
// validates the JSON output, and reports back via chrome.send.
//
// Loaded for side effects from dao_agent_app.ts so the resident
// (preloaded, hidden) agent WebUI can dream without the panel open.

import {addWebUIListener, callLLMStreaming} from './agent_bridge.js';
import type {ChatMessage} from './agent_bridge.js';
import {getActiveLLMConfig} from './llm_config.js';
import {currentLocale} from './i18n/i18n.js';

interface DreamHabit {
  key: string;
  value: string;
  confidence: number;
  evidence: string;
  relation: 'new'|'reinforce'|'contradict';
}

interface DreamResult {
  report_markdown: string;
  habits: DreamHabit[];
  scenario_adjustments:
      Array<{scenario_id: string; suggestion: string}>;
}

const SYSTEM_PROMPT = `You are the browser's dream analyzer. You receive a
JSON "material pack" describing one day of the user's browsing: visited
domains with page titles and time-of-day buckets, search queries, excerpts
of the user's questions to the in-browser AI agent, and feedback stats on
proactive suggestions. Infer the day's main themes and durable behavior
habits.

Output STRICT JSON (no markdown fence, no commentary) with exactly this
shape:
{
  "report_markdown": "<morning report, 200-400 words, written in the
    user's language (locale given below). Cover: main themes of the day,
    time distribution, notable observations. Friendly, calm tone;
    address the user as 'you'.>",
  "habits": [{
    "key": "<dot.namespaced key, e.g. interest.rust_async>",
    "value": "<one-sentence habit/preference description>",
    "confidence": <0.3-0.8>,
    "evidence": "<one-line justification from the material>",
    "relation": "new" | "reinforce" | "contradict"
  }],
  "scenario_adjustments": [{
    "scenario_id": "<id from feedback>",
    "suggestion": "lower_confidence" | "raise_confidence"
  }]
}

Rules:
- 0-6 habits. Only durable patterns, not one-off visits.
- "reinforce"/"contradict" only when the material clearly relates to one
  of the known preferences listed in the user message.
- Never invent domains or facts not present in the material.
- Habit keys and values in English; report_markdown in the user's locale.`;

function validateResult(parsed: unknown): DreamResult|null {
  if (typeof parsed !== 'object' || parsed === null) return null;
  const obj = parsed as Record<string, unknown>;
  if (typeof obj['report_markdown'] !== 'string' ||
      obj['report_markdown'].length === 0) {
    return null;
  }
  const habits: DreamHabit[] = [];
  if (Array.isArray(obj['habits'])) {
    for (const h of obj['habits']) {
      if (typeof h !== 'object' || h === null) continue;
      const hh = h as Record<string, unknown>;
      if (typeof hh['key'] !== 'string' || typeof hh['value'] !== 'string') {
        continue;
      }
      const relation = hh['relation'];
      habits.push({
        key: hh['key'],
        value: hh['value'],
        confidence: typeof hh['confidence'] === 'number' ?
            Math.min(Math.max(hh['confidence'], 0), 0.8) : 0.5,
        evidence: typeof hh['evidence'] === 'string' ? hh['evidence'] : '',
        relation: relation === 'reinforce' || relation === 'contradict' ?
            relation : 'new',
      });
    }
  }
  const adjustments: DreamResult['scenario_adjustments'] = [];
  if (Array.isArray(obj['scenario_adjustments'])) {
    for (const a of obj['scenario_adjustments']) {
      if (typeof a !== 'object' || a === null) continue;
      const aa = a as Record<string, unknown>;
      if (typeof aa['scenario_id'] === 'string' &&
          (aa['suggestion'] === 'lower_confidence' ||
           aa['suggestion'] === 'raise_confidence')) {
        adjustments.push({
          scenario_id: aa['scenario_id'],
          suggestion: aa['suggestion'],
        });
      }
    }
  }
  return {
    report_markdown: obj['report_markdown'],
    habits,
    scenario_adjustments: adjustments,
  };
}

// Strips ```json fences if the model wrapped its output anyway.
export function extractJson(raw: string): string {
  const trimmed = raw.trim();
  const fence = trimmed.match(/^```(?:json)?\s*([\s\S]*?)\s*```$/);
  return fence ? fence[1]! : trimmed;
}

async function callOnce(messages: ChatMessage[]): Promise<string> {
  return new Promise<string>((resolve, reject) => {
    void callLLMStreaming(messages, {
      onToken: () => {},
      onToolCall: () => {},
      onDone: (fullContent) => resolve(fullContent),
      onError: (shortMsg, fullError) =>
          reject(new Error(shortMsg + ': ' + fullError)),
    });
  });
}

export async function runDream(
    dreamDate: string, material: unknown): Promise<DreamResult> {
  const cfg = getActiveLLMConfig();
  if (!cfg.apiKey) {
    throw new Error('no LLM api key configured');
  }
  const userPrompt = `Locale: ${currentLocale()}\n` +
      `Dream date: ${dreamDate}\n` +
      `Material pack:\n${JSON.stringify(material)}`;
  const messages: ChatMessage[] = [
    {role: 'system', content: SYSTEM_PROMPT},
    {role: 'user', content: userPrompt},
  ];

  let lastError = '';
  for (let attempt = 0; attempt < 2; attempt++) {
    const raw = await callOnce(
        lastError ?
            [...messages, {
              role: 'user' as const,
              content: 'Your previous output was not valid JSON (' +
                  lastError + '). Output ONLY the JSON object.',
            }] :
            messages);
    try {
      const result = validateResult(JSON.parse(extractJson(raw)));
      if (result) {
        return result;
      }
      lastError = 'missing required fields';
    } catch (e) {
      lastError = e instanceof Error ? e.message : String(e);
    }
  }
  throw new Error('invalid JSON after retry: ' + lastError);
}

// ---- Wire up the listener (module side effect) ----

let dreamInFlight = false;

addWebUIListener('dream-run', (payload: unknown) => {
  const p = payload as {dreamDate?: string; material?: unknown};
  if (!p || typeof p.dreamDate !== 'string' || dreamInFlight) {
    return;
  }
  dreamInFlight = true;
  const date = p.dreamDate;
  runDream(date, p.material)
      .then((result) => {
        chrome.send('dreamComplete', [date, result]);
      })
      .catch((err: unknown) => {
        const msg = err instanceof Error ? err.message : String(err);
        chrome.send('dreamFailed', [date, msg]);
      })
      .finally(() => {
        dreamInFlight = false;
      });
});
```

- [ ] **Step 8.3: Import for side effects in `dao_agent_app.ts`** — alongside the other imports:

```typescript
import './dao_dream_runner.js';
```

- [ ] **Step 8.4: Add `"dao_dream_runner.ts"` to `ts_files`** in `src/dao/browser/ui/webui/resources/agent/BUILD.gn` (alphabetical, after `dao_compact.ts`).

---

### Task 9: WebUI vitest for runner JSON validation

**Files:**
- Create: `src/dao/browser/ui/webui/resources/agent/__tests__/dao_dream_runner.test.ts`

- [ ] **Step 9.1: Write the test** (mock `agent_bridge` / `llm_config` / `i18n` like sibling tests do):

```typescript
// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {beforeEach, describe, expect, it, vi} from 'vitest';

const callLLMStreaming = vi.fn();

vi.mock('../agent_bridge.js', () => ({
  addWebUIListener: vi.fn(),
  callLLMStreaming:
      (...args: unknown[]) => callLLMStreaming(...args),
}));
vi.mock('../llm_config.js', () => ({
  getActiveLLMConfig: () => ({
    provider: 'openai', apiKey: 'sk-test', baseUrl: '', model: 'gpt-test',
  }),
}));
vi.mock('../i18n/i18n.js', () => ({
  currentLocale: () => 'zh-CN',
}));

import {extractJson, runDream} from '../dao_dream_runner.js';

function respondWith(content: string) {
  callLLMStreaming.mockImplementationOnce(
      (_msgs: unknown, callbacks: {onDone: (c: string) => void}) => {
        callbacks.onDone(content);
        return Promise.resolve();
      });
}

const VALID = JSON.stringify({
  report_markdown: '昨晚的报告',
  habits: [{
    key: 'interest.rust',
    value: 'Learning Rust async',
    confidence: 0.95,  // should be capped to 0.8
    evidence: '12 visits to docs.rs',
    relation: 'new',
  }],
  scenario_adjustments:
      [{scenario_id: 's1', suggestion: 'lower_confidence'}],
});

describe('extractJson', () => {
  it('passes plain JSON through', () => {
    expect(extractJson('{"a":1}')).toBe('{"a":1}');
  });
  it('strips ```json fences', () => {
    expect(extractJson('```json\n{"a":1}\n```')).toBe('{"a":1}');
  });
});

describe('runDream', () => {
  beforeEach(() => {
    callLLMStreaming.mockReset();
  });

  it('parses a valid response and caps confidence at 0.8', async () => {
    respondWith(VALID);
    const result = await runDream('2026-06-11', {history: []});
    expect(result.report_markdown).toBe('昨晚的报告');
    expect(result.habits).toHaveLength(1);
    expect(result.habits[0]!.confidence).toBe(0.8);
    expect(result.scenario_adjustments).toHaveLength(1);
  });

  it('retries once on invalid JSON then succeeds', async () => {
    respondWith('sorry, here is the JSON: not-json');
    respondWith(VALID);
    const result = await runDream('2026-06-11', {});
    expect(result.habits).toHaveLength(1);
    expect(callLLMStreaming).toHaveBeenCalledTimes(2);
  });

  it('fails after two invalid responses', async () => {
    respondWith('garbage');
    respondWith('still garbage');
    await expect(runDream('2026-06-11', {})).rejects.toThrow(
        /invalid JSON after retry/);
  });

  it('drops malformed habit entries instead of failing', async () => {
    respondWith(JSON.stringify({
      report_markdown: 'r',
      habits: [{key: 'k'}, {key: 'k2', value: 'v2'}],
    }));
    const result = await runDream('2026-06-11', {});
    expect(result.habits).toHaveLength(1);
    expect(result.habits[0]!.key).toBe('k2');
  });
});
```

- [ ] **Step 9.2: Run:** `npm run test:webui -- dao_dream_runner`
  Expected: all tests PASS. (If the module's side-effect listener trips up vitest, ensure the `agent_bridge.js` mock is declared before the import — `vi.mock` is hoisted, so the shown order works.)

---

### Task 10: First build verification (C++ core)

- [ ] **Step 10.1:** Run `npm run rebuild` (imports patches + copies src/dao + debug build).
  Expected: build succeeds. Fix any compile errors in the Task 1–8 code before proceeding (common ones: missing includes, `base::DoNothing()` needs `base/functional/callback_helpers.h`, `std::min` needs `<algorithm>`).
- [ ] **Step 10.2:** Do NOT commit (project rule). Just confirm the build is green.

---

### Task 11: Settings UI (toggle + debug toggle + Dream now) and i18n keys

**Files:**
- Modify: `src/dao/browser/ui/webui/resources/agent/dao_settings_view.ts`
- Modify: `src/dao/browser/ui/webui/resources/agent/i18n/locales/en.ts`
- Modify: `src/dao/browser/ui/webui/resources/agent/i18n/locales/zh-CN.ts`

- [ ] **Step 11.1: Add i18n keys to `en.ts`** (new section after the `settings.memory.*` block):

```typescript
  'settings.dream.title': 'Dream Analysis',
  'settings.dream.desc':
      'At night, while you are away, Dao reviews your day and learns ' +
      'your habits.',
  'settings.dream.enable_name': 'Enable Dream Analysis',
  'settings.dream.enable_desc':
      'Each night, the domains you visited, page titles and search ' +
      'keywords from your day are sent to your configured AI provider ' +
      'for analysis.',
  'settings.dream.debug_name': 'Debug mode',
  'settings.dream.debug_desc':
      'Store the exact input sent to the AI for each dream run and show ' +
      'it on the report card.',
  'settings.dream.run_now_button': 'Dream now',
  'settings.dream.run_running': 'Dreaming…',
  'settings.dream.run_done_toast': 'Dream report generated',
  'settings.dream.run_failed_toast': 'Dream run failed: {error}',
  'settings.dream.no_key_toast':
      'Configure an AI provider and API key first',
```

- [ ] **Step 11.2: Add the same keys to `zh-CN.ts`** (tone reference; other locales are filled later by the user via i18n.sh — do NOT run it):

```typescript
  'settings.dream.title': '梦境分析',
  'settings.dream.desc': '夜深人静时，道会回顾你的一天，学习你的习惯。',
  'settings.dream.enable_name': '启用梦境分析',
  'settings.dream.enable_desc':
      '每晚会将你当天访问的域名、页面标题与搜索词发送给你配置的 AI 服务商进行归纳。',
  'settings.dream.debug_name': '调试模式',
  'settings.dream.debug_desc': '保存每次梦境归纳时发送给 AI 的完整输入，并在报告卡片中展示。',
  'settings.dream.run_now_button': '现在做梦',
  'settings.dream.run_running': '正在做梦…',
  'settings.dream.run_done_toast': '梦境报告已生成',
  'settings.dream.run_failed_toast': '梦境运行失败：{error}',
  'settings.dream.no_key_toast': '请先配置 AI 服务商和 API Key',
```

- [ ] **Step 11.3: Add state + render to `dao_settings_view.ts`.**
  - Properties block: add `dreamEnabled_: {type: Boolean, state: true}`, `dreamDebug_: {type: Boolean, state: true}`, `dreamRunning_: {type: Boolean, state: true}` with matching `declare private` fields and constructor defaults (`false`).
  - In the init path that loads `memoryEnabled_` (the method around line 1156 calling `getMemoryEnabled`), also load:

```typescript
    callNative('getDreamEnabled').then(
        (v) => this.dreamEnabled_ = !!v).catch(() => {});
    callNative('getDreamDebug').then(
        (v) => this.dreamDebug_ = !!v).catch(() => {});
```

  (Use `callNativeArgs` if `callNative`'s params shape mismatches — match how `getMemoryEnabled` is called in this file.)
  - In `renderMemory_()` (or as a sibling panel right after it, following the existing `panel` markup), add:

```typescript
  private renderDream_() {
    return html`
      <div class="panel">
        <div class="section-title">${t('settings.dream.title')}</div>
        <div class="section-desc">${t('settings.dream.desc')}</div>
        ${this.renderToggle_(
            t('settings.dream.enable_name'),
            t('settings.dream.enable_desc'),
            this.dreamEnabled_, (v) => {
              this.dreamEnabled_ = v;
              callNativeArgs('setDreamEnabled', v).catch(() => {});
            })}
        ${this.renderToggle_(
            t('settings.dream.debug_name'),
            t('settings.dream.debug_desc'),
            this.dreamDebug_, (v) => {
              this.dreamDebug_ = v;
              callNativeArgs('setDreamDebug', v).catch(() => {});
            })}
        <div class="soul-actions">
          <button class="btn-secondary"
              ?disabled=${this.dreamRunning_ || !this.dreamEnabled_}
              @click=${this.runDreamNow_}>
            ${this.dreamRunning_ ? t('settings.dream.run_running') :
                                   t('settings.dream.run_now_button')}
          </button>
        </div>
      </div>`;
  }

  private async runDreamNow_() {
    this.dreamRunning_ = true;
    try {
      await callNative('startManualDream');
      this.showToast_(t('settings.dream.run_done_toast'));
      window.dispatchEvent(new Event('dao-dream-report-updated'));
    } catch (e) {
      const msg = e instanceof Error ? e.message : String(e);
      this.showToast_(
          t('settings.dream.run_failed_toast', {error: msg}));
    } finally {
      this.dreamRunning_ = false;
    }
  }
```

  - Wire `renderDream_()` into the main render output directly after the memory panel. `showToast_`: reuse the view's existing toast mechanism — grep for how `dao_settings_view.ts` surfaces toasts (the app shell has `toastText_`; settings may dispatch an event or have its own helper such as `saveStatusText_`). Use whatever exists; if nothing exists in settings view, dispatch a `CustomEvent('dao-toast', {detail: msg, bubbles: true, composed: true})` and handle it in `dao_agent_app.ts`'s existing toast plumbing.
  - **No Tailwind utility classes** — only existing scoped classes (`panel`, `section-title`, `btn-secondary`, …) or inline styles (project rule).

---

### Task 12: Dream report card in chat view (+ debug inputs section)

**Files:**
- Modify: `src/dao/browser/ui/webui/resources/agent/dao_chat_view.ts`
- Modify: `src/dao/browser/ui/webui/resources/agent/i18n/locales/en.ts`
- Modify: `src/dao/browser/ui/webui/resources/agent/i18n/locales/zh-CN.ts`

- [ ] **Step 12.1: i18n keys (en.ts):**

```typescript
  'chat.dream.card_title': 'Last night\'s dream report',
  'chat.dream.card_date': 'About your day on {date}',
  'chat.dream.expand': 'Read the report',
  'chat.dream.collapse': 'Collapse',
  'chat.dream.habits_title': 'I think I noticed…',
  'chat.dream.habit_confirm': 'Yes',
  'chat.dream.habit_reject': 'Not really',
  'chat.dream.habit_confirmed': 'Got it, remembered',
  'chat.dream.habit_rejected': 'Okay, forgotten',
  'chat.dream.contradict_prefix': 'I noticed a change:',
  'chat.dream.debug_title': 'Debug: inputs used for this dream',
```

- [ ] **Step 12.2: i18n keys (zh-CN.ts):**

```typescript
  'chat.dream.card_title': '昨晚的梦境报告',
  'chat.dream.card_date': '关于你 {date} 的一天',
  'chat.dream.expand': '阅读报告',
  'chat.dream.collapse': '收起',
  'chat.dream.habits_title': '我好像注意到…',
  'chat.dream.habit_confirm': '是的',
  'chat.dream.habit_reject': '不准确',
  'chat.dream.habit_confirmed': '好的，已记住',
  'chat.dream.habit_rejected': '好的，已忘掉',
  'chat.dream.contradict_prefix': '我注意到一个变化：',
  'chat.dream.debug_title': '调试：本次梦境的输入素材',
```

- [ ] **Step 12.3: Card state + load in `dao_chat_view.ts`.**
  - State: `dreamReport_: {type: Object, state: true}`, `dreamExpanded_`, `dreamDebugExpanded_` booleans, plus a `dreamHabitStates_: Record<number, 'confirmed'|'rejected'>` state object. `declare private dreamReport_: DreamReportData|null;` with:

```typescript
interface DreamReportData {
  id: number;
  dreamDate: string;
  reportMarkdown: string;
  habits: Array<{
    key: string; value: string; confidence: number;
    evidence: string; relation: 'new'|'reinforce'|'contradict';
  }>;
  debugMaterialJson: string;
}
```

  - Load on `connectedCallback` (and on `window` event `dao-dream-report-updated` so a manual run refreshes it):

```typescript
  private async loadDreamReport_() {
    try {
      const raw = await callNative('getUnviewedDreamReport') as {
        id?: number; dreamDate?: string; reportMarkdown?: string;
        habitCandidates?: string; debugMaterialJson?: string;
      } | null;
      if (!raw || typeof raw.id !== 'number') {
        this.dreamReport_ = null;
        return;
      }
      let habits: DreamReportData['habits'] = [];
      try {
        habits = JSON.parse(raw.habitCandidates || '[]');
      } catch {}
      this.dreamReport_ = {
        id: raw.id,
        dreamDate: raw.dreamDate || '',
        reportMarkdown: raw.reportMarkdown || '',
        habits,
        debugMaterialJson: raw.debugMaterialJson || '',
      };
    } catch {
      this.dreamReport_ = null;
    }
  }
```

- [ ] **Step 12.4: Render the card** at the top of the chat scroll area (before the session content; find the top-of-list anchor in the render method). Markdown body renders through the same `marked` pipeline the chat bubbles use (`<markdown-block>` / the existing render helper — match whatever `dao_chat_view.ts` uses for assistant messages). Structure (inline styles / `agent.css` rules only — NO Tailwind):

```typescript
  private renderDreamCard_() {
    const r = this.dreamReport_;
    if (!r) return '';
    return html`
      <div class="dream-card" style="border:1px solid var(--border,rgba(0,0,0,.08));
          border-radius:12px;padding:14px 16px;margin:8px 12px;">
        <div style="display:flex;align-items:center;gap:8px;">
          <span style="font-weight:600;font-size:13px;">
            🌙 ${t('chat.dream.card_title')}</span>
          <span style="font-size:11px;opacity:.6;">
            ${t('chat.dream.card_date', {date: r.dreamDate})}</span>
          <span style="flex:1"></span>
          <button class="btn-secondary" style="font-size:12px;"
              @click=${this.toggleDreamExpanded_}>
            ${this.dreamExpanded_ ? t('chat.dream.collapse') :
                                    t('chat.dream.expand')}
          </button>
        </div>
        ${this.dreamExpanded_ ? html`
          <div style="margin-top:10px;font-size:13px;">
            ${this.renderMarkdown_(r.reportMarkdown)}
          </div>
          ${this.renderDreamHabits_(r)}
          ${r.debugMaterialJson ? html`
            <details style="margin-top:10px;"
                @toggle=${(e: Event) => this.dreamDebugExpanded_ =
                    (e.target as HTMLDetailsElement).open}>
              <summary style="font-size:12px;cursor:pointer;opacity:.7;">
                ${t('chat.dream.debug_title')}</summary>
              <pre style="font-size:11px;max-height:300px;overflow:auto;
                  background:rgba(0,0,0,.04);border-radius:8px;
                  padding:8px;white-space:pre-wrap;">${
                  r.debugMaterialJson}</pre>
            </details>` : ''}
        ` : ''}
      </div>`;
  }
```

  Replace `this.renderMarkdown_(…)` with the actual markdown helper used for assistant bubbles in this file (verify during implementation; the emoji 🌙 here is allowed only if the file already uses emoji — per design language it does NOT: use the Lucide `moon` icon instead, fetched verbatim via `curl -s https://raw.githubusercontent.com/lucide-icons/lucide/main/icons/moon.svg` and inlined as `<svg>` with `stroke="currentColor"`).

- [ ] **Step 12.5: Habit confirm/reject rows:**

```typescript
  private renderDreamHabits_(r: DreamReportData) {
    const newOrContradict =
        r.habits.filter((h) => h.relation !== 'reinforce');
    if (newOrContradict.length === 0) return '';
    return html`
      <div style="margin-top:10px;">
        <div style="font-size:12px;font-weight:600;opacity:.8;">
          ${t('chat.dream.habits_title')}</div>
        ${newOrContradict.map((h, i) => html`
          <div style="display:flex;align-items:center;gap:8px;
              padding:6px 0;font-size:12px;">
            <span style="flex:1;">
              ${h.relation === 'contradict' ?
                  t('chat.dream.contradict_prefix') + ' ' : ''}${h.value}
            </span>
            ${this.dreamHabitStates_[i] === 'confirmed' ?
              html`<span style="opacity:.6;">
                  ${t('chat.dream.habit_confirmed')}</span>` :
             this.dreamHabitStates_[i] === 'rejected' ?
              html`<span style="opacity:.6;">
                  ${t('chat.dream.habit_rejected')}</span>` :
              html`
                <button class="btn-secondary" style="font-size:11px;"
                    @click=${() => this.confirmHabit_(h, i)}>
                  ${t('chat.dream.habit_confirm')}</button>
                <button class="btn-secondary" style="font-size:11px;"
                    @click=${() => this.rejectHabit_(h, i)}>
                  ${t('chat.dream.habit_reject')}</button>`}
          </div>`)}
      </div>`;
  }

  private confirmHabit_(h: DreamReportData['habits'][0], index: number) {
    // Existing message: updatePreference merges key/value/confidence.
    // Confirm = user-grade confidence 0.95.
    callNativeArgs('updatePreference', h.key, h.value, 0.95)
        .catch(() => {});
    this.dreamHabitStates_ = {...this.dreamHabitStates_,
                              [index]: 'confirmed'};
  }

  private rejectHabit_(h: DreamReportData['habits'][0], index: number) {
    // Find and delete the matching preference; record dismissal so
    // future dreams stop re-emitting it. getPreferences returns
    // [{id, key, ...}] — match by key.
    callNative('getPreferences')
        .then((prefs) => {
          const list = prefs as Array<{id: number; key: string}>;
          const match = list.find((p) => p.key === h.key);
          if (match) {
            return callNativeArgs('deleteMemory', 'preference', match.id);
          }
        })
        .catch(() => {});
    this.dreamHabitStates_ = {...this.dreamHabitStates_,
                              [index]: 'rejected'};
  }
```

  **Verify the exact existing message names/arg shapes** for `updatePreference` / `getPreferences` / `deleteMemory` in `dao_agent_ui.cc` (`HandleUpdatePreference`, `HandleGetPreferences`, `HandleDeleteMemory`) and match them — the snippets above assume `(key, value, confidence)`, no-args, and `(kind, id)` respectively; adjust at implementation time if signatures differ.

- [ ] **Step 12.6: Mark viewed on expand** — in `toggleDreamExpanded_`, on first expansion:

```typescript
  private toggleDreamExpanded_() {
    this.dreamExpanded_ = !this.dreamExpanded_;
    if (this.dreamExpanded_ && this.dreamReport_) {
      callNativeArgs('markDreamReportViewed', this.dreamReport_.id)
          .catch(() => {});
      // Keep the card visible this session; it disappears next open.
    }
  }
```

- [ ] **Step 12.7: Run WebUI lint/tests:** `npm run test:webui -- dao_dream` and `npm run lint:lit` (the lint command exists inside `npm run test`'s pipeline — run whatever lint script package.json exposes for lit).
  Expected: PASS.

---

### Task 13: Browser tests (scheduler / collector / store)

**Files:**
- Create: `src/dao/browser/agent/dao_dream_browsertest.cc`
- Modify: `src/patches/chrome/browser/ui/BUILD.gn.patch` (add the test file to the `dao_browser_tests` source_set)

- [ ] **Step 13.1: Add the test file to the `dao_browser_tests` sources** in `src/patches/chrome/browser/ui/BUILD.gn.patch`:

```
+  sources = [
+    "//dao/browser/agent/dao_dream_browsertest.cc",
+    "//dao/browser/ui/views/dao_browser_browsertest.cc",
+  ]
```

  (Same patch-editing flow as Task 7: import → edit engine BUILD.gn → `npm run export -- chrome/browser/ui/BUILD.gn`.)

- [ ] **Step 13.2: Write `dao_dream_browsertest.cc`:**

```cpp
// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <optional>
#include <string>

#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/run_loop.h"
#include "base/test/bind.h"
#include "base/test/simple_test_clock.h"
#include "base/time/time.h"
#include "chrome/browser/history/history_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "components/history/core/browser/history_service.h"
#include "components/prefs/pref_service.h"
#include "content/public/test/browser_test.h"
#include "dao/browser/agent/dao_agent_memory_service.h"
#include "dao/browser/agent/dao_agent_memory_service_factory.h"
#include "dao/browser/agent/dao_dream_material_collector.h"
#include "dao/browser/agent/dao_dream_service.h"
#include "dao/browser/agent/dao_dream_service_factory.h"
#include "dao/browser/dao_pref_names.h"
#include "url/gurl.h"

namespace dao {

namespace {

// Builds a local base::Time for Y-M-D h:m.
base::Time LocalTime(int year, int month, int day, int hour, int minute) {
  base::Time::Exploded e = {};
  e.year = year;
  e.month = month;
  e.day_of_month = day;
  e.hour = hour;
  e.minute = minute;
  base::Time t;
  CHECK(base::Time::FromLocalExploded(e, &t));
  return t;
}

}  // namespace

using DaoDreamStaticTest = InProcessBrowserTest;

// --- Pure-logic tests on the static helpers ---

IN_PROC_BROWSER_TEST_F(DaoDreamStaticTest, DreamDateAttribution) {
  // 23:00 on June 10 → dream day = June 10.
  EXPECT_EQ("2026-06-10",
            DaoDreamService::DreamDateFor(
                LocalTime(2026, 6, 10, 23, 0)));
  // 01:30 on June 11 → still June 10's dream day.
  EXPECT_EQ("2026-06-10",
            DaoDreamService::DreamDateFor(
                LocalTime(2026, 6, 11, 1, 30)));
  // 14:00 on June 11 (manual / catch-up daytime) → June 11.
  EXPECT_EQ("2026-06-11",
            DaoDreamService::DreamDateFor(
                LocalTime(2026, 6, 11, 14, 0)));
}

IN_PROC_BROWSER_TEST_F(DaoDreamStaticTest, NightWindow) {
  EXPECT_TRUE(DaoDreamService::IsNightTime(LocalTime(2026, 6, 10, 22, 0)));
  EXPECT_TRUE(DaoDreamService::IsNightTime(LocalTime(2026, 6, 11, 2, 0)));
  EXPECT_TRUE(DaoDreamService::IsNightTime(LocalTime(2026, 6, 11, 5, 59)));
  EXPECT_FALSE(DaoDreamService::IsNightTime(LocalTime(2026, 6, 11, 6, 0)));
  EXPECT_FALSE(DaoDreamService::IsNightTime(LocalTime(2026, 6, 10, 21, 59)));
  EXPECT_FALSE(DaoDreamService::IsNightTime(LocalTime(2026, 6, 10, 12, 0)));
}

IN_PROC_BROWSER_TEST_F(DaoDreamStaticTest, MaterialWindow) {
  base::Time start, end;
  // Querying June 10's window at June 11 01:00: 06-10 06:00 → 01:00 now.
  const base::Time now = LocalTime(2026, 6, 11, 1, 0);
  DaoDreamService::MaterialWindowFor("2026-06-10", now, &start, &end);
  EXPECT_EQ(LocalTime(2026, 6, 10, 6, 0), start);
  EXPECT_EQ(now, end);  // now < hard_end(06-11 06:00)
  // Querying it the next afternoon (catch-up): clamps to 06-11 06:00.
  DaoDreamService::MaterialWindowFor(
      "2026-06-10", LocalTime(2026, 6, 11, 15, 0), &start, &end);
  EXPECT_EQ(LocalTime(2026, 6, 11, 6, 0), end);
}

IN_PROC_BROWSER_TEST_F(DaoDreamStaticTest, SearchQueryExtraction) {
  EXPECT_EQ("rust async",
            DreamMaterialCollector::ExtractSearchQuery(
                "https://www.google.com/search?q=rust+async"));
  EXPECT_EQ("天气",
            DreamMaterialCollector::ExtractSearchQuery(
                "https://www.baidu.com/s?wd=%E5%A4%A9%E6%B0%94"));
  // Non-search URL → empty.
  EXPECT_EQ("", DreamMaterialCollector::ExtractSearchQuery(
                    "https://github.com/anthropics/claude-code"));
  // Search domain but no query param → empty.
  EXPECT_EQ("", DreamMaterialCollector::ExtractSearchQuery(
                    "https://www.google.com/maps"));
}

// --- Fixture with memory + dream enabled ---

class DaoDreamBrowserTest : public InProcessBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    PrefService* prefs = browser()->profile()->GetPrefs();
    prefs->SetBoolean(prefs::kDaoAgentMemoryEnabled, true);
    prefs->SetBoolean(prefs::kDaoDreamEnabled, true);
  }

  DaoDreamService* dream_service() {
    return DaoDreamServiceFactory::GetForProfile(browser()->profile());
  }
};

IN_PROC_BROWSER_TEST_F(DaoDreamBrowserTest, ServiceGatedByPrefs) {
  EXPECT_NE(nullptr, dream_service());
  browser()->profile()->GetPrefs()->SetBoolean(prefs::kDaoDreamEnabled,
                                               false);
  EXPECT_EQ(nullptr, dream_service());
}

IN_PROC_BROWSER_TEST_F(DaoDreamBrowserTest,
                       CollectorRedactsUrlsAndAggregates) {
  // Seed history inside the window.
  history::HistoryService* history =
      HistoryServiceFactory::GetForProfile(
          browser()->profile(), ServiceAccessType::EXPLICIT_ACCESS);
  ASSERT_TRUE(history);
  const base::Time now = base::Time::Now();
  history->AddPage(GURL("https://github.com/foo/bar?token=SECRET123"),
                   now - base::Hours(1), history::SOURCE_BROWSED);
  history->AddPage(GURL("https://github.com/foo/baz"),
                   now - base::Hours(2), history::SOURCE_BROWSED);
  history->AddPage(GURL("https://www.google.com/search?q=hello+world"),
                   now - base::Hours(3), history::SOURCE_BROWSED);

  DaoAgentMemoryService* memory =
      DaoAgentMemoryServiceFactory::GetForProfile(browser()->profile());
  ASSERT_TRUE(memory);
  DreamMaterialCollector collector(browser()->profile(), memory);

  base::Value::Dict pack;
  base::RunLoop loop;
  collector.Collect(now - base::Hours(6), now,
                    base::BindLambdaForTesting([&](base::Value::Dict p) {
                      pack = std::move(p);
                      loop.Quit();
                    }));
  loop.Run();

  // Privacy invariant: serialized pack contains no full URL / no query
  // params from visited URLs.
  std::string json;
  base::JSONWriter::Write(pack, &json);
  EXPECT_EQ(std::string::npos, json.find("SECRET123"));
  EXPECT_EQ(std::string::npos, json.find("/foo/bar"));
  EXPECT_EQ(std::string::npos, json.find("https://github.com"));

  // Aggregation: github.com appears once with visit_count 2.
  const base::Value::List* domains = pack.FindList("history");
  ASSERT_TRUE(domains);
  bool found_github = false;
  for (const base::Value& d : *domains) {
    const base::Value::Dict& dict = d.GetDict();
    if (*dict.FindString("domain") == "github.com") {
      found_github = true;
      EXPECT_EQ(2, dict.FindInt("visit_count").value_or(0));
    }
  }
  EXPECT_TRUE(found_github);

  // Search extraction.
  const base::Value::List* queries = pack.FindList("search_queries");
  ASSERT_TRUE(queries);
  ASSERT_EQ(1u, queries->size());
  EXPECT_EQ("hello world", (*queries)[0].GetString());
}

IN_PROC_BROWSER_TEST_F(DaoDreamBrowserTest, DreamReportStoreRoundTrip) {
  DaoAgentMemoryService* memory =
      DaoAgentMemoryServiceFactory::GetForProfile(browser()->profile());
  ASSERT_TRUE(memory);

  DreamReport report;
  report.dream_date = "2026-06-10";
  report.report_markdown = "# report";
  report.habit_candidates = "[]";
  report.material_stats = "{}";
  report.status = "completed";
  report.attempt_count = 1;
  report.trigger_kind = "manual";
  report.debug_material_json = "{\"history\":[]}";

  {
    base::RunLoop loop;
    memory->SaveDreamReport(report,
                            base::BindLambdaForTesting([&](bool ok) {
                              EXPECT_TRUE(ok);
                              loop.Quit();
                            }));
    loop.Run();
  }
  {
    base::RunLoop loop;
    memory->GetDreamReportByDate(
        "2026-06-10",
        base::BindLambdaForTesting([&](std::optional<DreamReport> r) {
          ASSERT_TRUE(r.has_value());
          EXPECT_EQ("completed", r->status);
          EXPECT_EQ("{\"history\":[]}", r->debug_material_json);
          EXPECT_TRUE(r->viewed_at.is_null());
          loop.Quit();
        }));
    loop.Run();
  }
  // Unviewed lookup finds it; marking viewed removes it.
  int64_t id = 0;
  {
    base::RunLoop loop;
    memory->GetLatestUnviewedDreamReport(
        base::BindLambdaForTesting([&](std::optional<DreamReport> r) {
          ASSERT_TRUE(r.has_value());
          id = r->id;
          loop.Quit();
        }));
    loop.Run();
  }
  {
    base::RunLoop loop;
    memory->MarkDreamReportViewed(
        id, base::BindLambdaForTesting([&](bool ok) {
          EXPECT_TRUE(ok);
          loop.Quit();
        }));
    loop.Run();
  }
  {
    base::RunLoop loop;
    memory->GetLatestUnviewedDreamReport(
        base::BindLambdaForTesting([&](std::optional<DreamReport> r) {
          EXPECT_FALSE(r.has_value());
          loop.Quit();
        }));
    loop.Run();
  }
}

IN_PROC_BROWSER_TEST_F(DaoDreamBrowserTest, SchedulerSkipsWhenNotIdle) {
  DaoDreamService* service = dream_service();
  ASSERT_TRUE(service);
  base::SimpleTestClock clock;
  clock.SetNow(LocalTime(2026, 6, 10, 23, 30));  // night
  service->SetClockForTesting(&clock);
  service->SetIdleTimeCallbackForTesting(
      base::BindRepeating([] { return 60; }));  // only 1 min idle
  service->TickForTesting();
  EXPECT_EQ(DaoDreamService::State::kIdle, service->state());
}

IN_PROC_BROWSER_TEST_F(DaoDreamBrowserTest,
                       SchedulerStartsNightlyWhenIdleAtNight) {
  DaoDreamService* service = dream_service();
  ASSERT_TRUE(service);
  // No runner registered → tick must NOT start (WebUI unavailable).
  base::SimpleTestClock clock;
  clock.SetNow(LocalTime(2026, 6, 10, 23, 30));
  service->SetClockForTesting(&clock);
  service->SetIdleTimeCallbackForTesting(
      base::BindRepeating([] { return 7200; }));
  service->TickForTesting();
  EXPECT_EQ(DaoDreamService::State::kIdle, service->state());

  // With a runner, the nightly path engages (collector kicks off →
  // state leaves kIdle synchronously).
  class FakeRunner : public DaoDreamService::Runner {
   public:
    void RunDream(const std::string& dream_date,
                  const base::Value::Dict& material) override {
      ran = true;
    }
    bool ran = false;
  } runner;
  service->SetRunner(&runner);
  service->TickForTesting();
  // Tick → async GetDreamReportByDate → StartDream. Pump the loop until
  // the state machine moves or material-empty skip resets it.
  base::RunLoop().RunUntilIdle();
  // Either collecting/dreaming (if history seeded) or back to idle after
  // the empty-material skip — both prove the gate opened; assert on the
  // attempt: state changed at some point. Strongest cheap assertion:
  // no crash and runner_/state consistent. For determinism, seed one
  // history row in this test before ticking and assert state >= kCollecting
  // or runner.ran eventually becomes true via RunLoop + QuitWhenIdle.
  service->ClearRunner(&runner);
}

}  // namespace dao
```

  **Note for the implementer** on the last test: make it deterministic by seeding history (same `AddPage` calls as the collector test) *before* `TickForTesting()`, then spin a `base::RunLoop` with a timeout-quit and assert `runner.ran == true`. The skeleton above marks the intent; tighten it at implementation time. Also verify `history::SOURCE_BROWSED` is the correct constant name in this Chromium version (check `engine/src/components/history/core/browser/history_types.h`).

- [ ] **Step 13.3: Build + run tests:** `npm run test` (builds browser_tests and runs all `Dao*`).
  Expected: all new `DaoDream*` tests PASS, all existing `Dao*` tests still PASS.
  To iterate on just the new ones after the first build:
  `./engine/src/out/dao-debug/browser_tests --gtest_filter="DaoDream*"`

---

### Task 14: features.md + final verification

**Files:**
- Modify: `docs/features.md`

- [ ] **Step 14.1: Document the feature** — add to section 2 (AI Agent System) a new subsection:

```markdown
### 2.5 Dream Analysis (nightly behavior learning)
- **DaoDreamService** (+ Factory) — profile-keyed scheduler: nightly
  (22:00–06:00 local, system idle ≥1h), daytime catch-up for yesterday,
  and manual trigger from Agent settings. Off by default
  (`dao.dream_enabled`), double-gated behind agent memory.
- **DreamMaterialCollector** — aggregates one day of signals: history
  (domain+title+time-bucket granularity, top 50; full URLs never leave
  the browser), search keywords (extracted in C++), agent conversation
  excerpts, proactive-feedback stats.
- **dao_dream_runner.ts** — resident agent WebUI executes the LLM
  summarization (user's configured provider) and returns structured
  habits + a morning-report markdown.
- Results: habits merged into `preferences` (LLM confidence capped at
  0.8; user confirmation raises to 0.95), report archived in the
  `dream_reports` table, morning report card in the Agent panel with
  per-habit confirm/reject and an optional debug view of the exact LLM
  input (`dao.dream_debug`).
```

- [ ] **Step 14.2: Final whole-feature verification:**
  1. `npm run rebuild` — green build.
  2. `npm run test` — all `Dao*` browser tests pass.
  3. `npm run test:webui` — all vitest suites pass.
  4. Manual smoke (optional, needs an API key): `npm run start:debug`, enable memory + dream in Agent settings, enable debug mode, click "Dream now", verify the report card appears with the debug-inputs section.
- [ ] **Step 14.3: Leave everything unstaged.** Summarize the change set for the user; the user decides when/what to commit.

---

## Self-Review Notes (already applied)

- **Spec coverage check:** prefs (T1), schema+store (T2), service wrappers (T3), collector with privacy invariant (T4), scheduler/state machine/static helpers (T5), WebUI bridge handler incl. debug pref (T6), registration patches (T7), LLM runner + bridge listener infra (T8), runner tests (T9), C++ build gate (T10), settings UI + disclosure + debug toggle + manual trigger (T11), report card + habit feedback + debug view + mark-viewed (T12), browser tests incl. URL-redaction assertion (T13), docs (T14). Out-of-scope items from the spec are not implemented (correct).
- **Known judgment calls baked in:**
  - Action-feedback windowing uses scenario lifetime stats (simpler than a new windowed SQL aggregate); acceptable for v1 since the LLM only gets coarse signals.
  - `dream-run` listener infra is added to `agent_bridge.ts` because the agent WebUI currently has no `webUIListenerCallback` receiver at all.
  - Habit reject deletes by key-match via existing `getPreferences`/`deleteMemory` messages instead of adding a new handler.
- **Type consistency:** `DreamReport` fields match across store/service/handler/TS card (`debug_material_json` ↔ `debugMaterialJson`). `Runner` interface names match between service and handler. Pref names `kDaoDreamEnabled`/`kDaoDreamDebug` consistent across C++/patches/TS message names.
