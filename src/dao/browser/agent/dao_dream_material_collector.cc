// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/agent/dao_dream_material_collector.h"

#include <algorithm>
#include <array>
#include <limits>
#include <map>
#include <set>
#include <utility>
#include <vector>

#include "base/barrier_closure.h"
#include "base/functional/bind.h"
#include "base/numerics/safe_math.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/history/history_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "components/history/core/browser/history_service.h"
#include "components/history/core/browser/history_types.h"
#include "dao/browser/activity/dao_foreground_activity_service.h"
#include "dao/browser/activity/dao_foreground_activity_service_factory.h"
#include "dao/browser/agent/dao_agent_memory_service.h"
#include "dao/browser/agent/dao_dream_domain_utils.h"
#include "dao/browser/agent/dao_dream_foreground_policy.h"
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
    {"google.com", "q"}, {"bing.com", "q"},   {"duckduckgo.com", "q"},
    {"kagi.com", "q"},   {"baidu.com", "wd"}, {"search.brave.com", "q"},
};
constexpr double kMinKnownPreferenceConfidence = 0.9;
constexpr int kMediumForegroundSeconds = 5 * 60;
constexpr int kDeepForegroundSeconds = 30 * 60;

// Local-time bucket for a visit: morning 06-12, afternoon 12-18,
// evening 18-22, night 22-06.
const char* BucketFor(base::Time t) {
  base::Time::Exploded e;
  t.LocalExplode(&e);
  if (e.hour >= 6 && e.hour < 12) {
    return "morning";
  }
  if (e.hour >= 12 && e.hour < 18) {
    return "afternoon";
  }
  if (e.hour >= 18 && e.hour < 22) {
    return "evening";
  }
  return "night";
}

// "YYYY-MM-DD HH:MM" in local time, for the window labels in the pack.
std::string FormatLocalYmdHm(base::Time t) {
  base::Time::Exploded e;
  t.LocalExplode(&e);
  return base::StringPrintf("%04d-%02d-%02d %02d:%02d", e.year, e.month,
                            e.day_of_month, e.hour, e.minute);
}

std::string FormatLocalDate(base::Time t) {
  base::Time::Exploded e;
  t.LocalExplode(&e);
  return base::StringPrintf("%04d-%02d-%02d", e.year, e.month, e.day_of_month);
}

struct DomainAgg {
  int visit_count = 0;
  int foreground_seconds = 0;
  int total_seconds = 0;
  std::vector<std::string> titles;
  std::map<std::string, int> buckets;
  std::map<std::string, int> foreground_seconds_by_bucket;
};

int MaterialSeconds(base::TimeDelta duration) {
  if (duration <= base::Seconds(0)) {
    return 0;
  }
  return static_cast<int>(
      std::min<int64_t>(duration.InSeconds(), std::numeric_limits<int>::max()));
}

int AddMaterialSeconds(int current, int64_t added) {
  base::CheckedNumeric<int> total(current);
  total += std::clamp<int64_t>(added, 0, std::numeric_limits<int>::max());
  return total.ValueOrDefault(std::numeric_limits<int>::max());
}

base::TimeDelta ForegroundDurationFor(const history::AnnotatedVisit& visit) {
  const base::TimeDelta foreground =
      visit.context_annotations.total_foreground_duration;
  if (foreground >= base::Seconds(0)) {
    return foreground;
  }
  if (visit.visit_row.visit_duration > base::Seconds(0)) {
    return visit.visit_row.visit_duration;
  }
  return base::Seconds(0);
}

const char* DurationLevelFor(int foreground_seconds) {
  if (foreground_seconds >= kDeepForegroundSeconds) {
    return "deep";
  }
  if (foreground_seconds >= kMediumForegroundSeconds) {
    return "medium";
  }
  return "light";
}

std::string TruncateMaterialText(const std::string& text) {
  std::u16string utf16 = base::UTF8ToUTF16(text);
  if (utf16.size() <=
      static_cast<size_t>(DreamMaterialCollector::kMaxTextChars)) {
    return text;
  }
  utf16.resize(DreamMaterialCollector::kMaxTextChars);
  return base::UTF16ToUTF8(utf16);
}

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
  const std::string host(url.host());
  for (const auto& engine : kSearchEngines) {
    const std::string suffix(engine.host_suffix);
    const bool match =
        host == suffix || (host.size() > suffix.size() + 1 &&
                           host.compare(host.size() - suffix.size() - 1,
                                        std::string::npos, "." + suffix) == 0);
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

// static
std::string DreamMaterialCollector::NormalizeExcludedDomainForTesting(
    const std::string& input) {
  return NormalizeDreamExcludedDomain(input);
}

// static
bool DreamMaterialCollector::IsDomainExcludedForTesting(
    const std::string& host,
    const std::set<std::string>& excluded_domains) {
  return IsDreamDomainExcluded(host, excluded_domains);
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
  preferences_part_.clear();
  feedback_part_.clear();
  foreground_seconds_by_bucket_part_.clear();
  foreground_seconds_by_bucket_part_.Set("morning", 0);
  foreground_seconds_by_bucket_part_.Set("afternoon", 0);
  foreground_seconds_by_bucket_part_.Set("evening", 0);
  foreground_seconds_by_bucket_part_.Set("night", 0);
  excluded_domains_ = LoadDreamExcludedDomains(profile_);
  history_domain_count_ = 0;
  search_query_count_ = 0;
  conversation_session_count_ = 0;
  excluded_history_visits_ = 0;

  history_visits_.clear();
  activity_snapshot_ = {};

  // History, activity, conversation excerpts, exact conversation-session
  // count, preferences, and feedback.
  barrier_ = base::BarrierClosure(
      6, base::BindOnce(&DreamMaterialCollector::OnPartDone,
                        weak_factory_.GetWeakPtr()));

  // Part 1: history → domains + search queries.
  history::HistoryService* history = HistoryServiceFactory::GetForProfile(
      profile_, ServiceAccessType::EXPLICIT_ACCESS);
  if (!history) {
    barrier_.Run();
  } else {
    history::QueryOptions options;
    options.begin_time = window_start;
    options.end_time = window_end;
    options.max_count = 0;  // everything in range
    options.duplicate_policy = history::QueryOptions::KEEP_ALL_DUPLICATES;
    history->GetAnnotatedVisits(
        options, /*compute_redirect_chain_start_properties=*/false,
        /*get_unclustered_visits_only=*/false,
        base::BindOnce(&DreamMaterialCollector::OnHistoryResults,
                       weak_factory_.GetWeakPtr()),
        &history_tracker_);
  }

  foreground_query_time_ = base::Time::Now();
  const std::string activity_start_date = FormatLocalDate(window_start_);
  const std::string activity_end_date = FormatLocalDate(
      window_end_ > window_start_ ? window_end_ - base::Microseconds(1)
                                  : window_end_);
  DaoForegroundActivityService* activity =
      DaoForegroundActivityServiceFactory::GetForProfile(profile_);
  if (activity) {
    activity->GetSnapshot(
        activity_start_date, activity_end_date,
        base::BindOnce(&DreamMaterialCollector::OnActivitySnapshot,
                       weak_factory_.GetWeakPtr()));
  } else {
    barrier_.Run();
  }

  // Part 2: agent conversations in window. The user's questions carry the
  // intent; keep the first 2 user messages per session.
  memory_service_->LoadConversationMessagesInRange(
      window_start_, window_end_, 500,
      base::BindOnce(
          [](base::WeakPtr<DreamMaterialCollector> self,
             std::vector<ConversationMessage> messages) {
            if (!self) {
              return;
            }
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
                it = by_session
                         .emplace(msg.session_id, std::vector<std::string>())
                         .first;
              }
              if (it->second.size() < 2) {
                it->second.push_back(TruncateMaterialText(msg.content));
              }
            }
            base::ListValue sessions;
            for (const auto& sid : session_order) {
              base::DictValue s;
              s.Set("session_id", sid);
              base::ListValue msgs;
              for (const auto& m : by_session[sid]) {
                msgs.Append(m);
              }
              s.Set("messages", std::move(msgs));
              sessions.Append(std::move(s));
            }
            self->OnConversationsLoaded(std::move(sessions));
          },
          weak_factory_.GetWeakPtr()));

  memory_service_->CountUserConversationSessionsInRange(
      window_start_, window_end_,
      base::BindOnce(
          [](base::WeakPtr<DreamMaterialCollector> self, int session_count) {
            if (self) {
              self->OnConversationSessionCountLoaded(session_count);
            }
          },
          weak_factory_.GetWeakPtr()));

  // Part 3: known high-confidence preferences. These are existing memory, not
  // evidence from the current day. Keep this stricter than the generic memory
  // context so unconfirmed dream guesses do not feed the next dream.
  memory_service_->GetPreferences(
      kMaxPreferences, kMinKnownPreferenceConfidence,
      base::BindOnce(
          [](base::WeakPtr<DreamMaterialCollector> self,
             std::vector<Preference> preferences) {
            if (!self) {
              return;
            }
            base::ListValue list;
            for (const auto& p : preferences) {
              base::DictValue pref;
              pref.Set("key", TruncateMaterialText(p.key));
              pref.Set("value", TruncateMaterialText(p.value));
              pref.Set("confidence", p.confidence);
              pref.Set("evidence_count", p.evidence_count);
              list.Append(std::move(pref));
            }
            self->OnPreferencesLoaded(std::move(list));
          },
          weak_factory_.GetWeakPtr()));

  // Part 4: proactive-action feedback. Scenario stat columns already
  // aggregate lifetime counts; v1 surfaces those coarse signals.
  memory_service_->GetPersonalScenarios(base::BindOnce(
      [](base::WeakPtr<DreamMaterialCollector> self,
         std::vector<ScenarioDefinition> scenarios) {
        if (!self) {
          return;
        }
        base::ListValue feedback;
        for (const auto& s : scenarios) {
          if (s.times_triggered == 0) {
            continue;
          }
          base::DictValue f;
          f.Set("scenario_id", s.id);
          f.Set("name", TruncateMaterialText(s.name));
          f.Set("shown", s.times_triggered);
          f.Set("clicked", s.times_accepted);
          f.Set("dismissed", s.times_dismissed);
          feedback.Append(std::move(f));
        }
        self->OnFeedbackLoaded(std::move(feedback));
      },
      weak_factory_.GetWeakPtr()));
}

void DreamMaterialCollector::OnHistoryResults(
    std::vector<history::AnnotatedVisit> visits) {
  history_visits_ = std::move(visits);
  barrier_.Run();
}

void DreamMaterialCollector::OnActivitySnapshot(
    DaoForegroundActivitySnapshot snapshot) {
  activity_snapshot_ = std::move(snapshot);
  barrier_.Run();
}

void DreamMaterialCollector::OnConversationsLoaded(base::ListValue sessions) {
  conversations_part_ = std::move(sessions);
  barrier_.Run();
}

void DreamMaterialCollector::OnConversationSessionCountLoaded(
    int session_count) {
  conversation_session_count_ = session_count;
  barrier_.Run();
}

void DreamMaterialCollector::OnPreferencesLoaded(base::ListValue preferences) {
  preferences_part_ = std::move(preferences);
  barrier_.Run();
}

void DreamMaterialCollector::OnFeedbackLoaded(base::ListValue feedback) {
  feedback_part_ = std::move(feedback);
  barrier_.Run();
}

void DreamMaterialCollector::OnPartDone() {
  const std::string start_date = FormatLocalDate(window_start_);
  const std::string end_date = FormatLocalDate(
      window_end_ > window_start_ ? window_end_ - base::Microseconds(1)
                                  : window_end_);
  DreamForegroundRangePolicy foreground_policy =
      ResolveDreamForegroundWindowPolicy(window_start_, window_end_,
                                         activity_snapshot_,
                                         foreground_query_time_);
  std::map<std::string, DomainAgg> by_domain;
  std::map<std::string, int> foreground_seconds_by_bucket = {
      {"morning", 0}, {"afternoon", 0}, {"evening", 0}, {"night", 0}};
  std::vector<std::string> queries;
  std::set<std::string> seen_queries;
  for (const auto& visit : history_visits_) {
    const GURL& url = visit.url_row.url();
    const std::string domain(url.host());
    if (IsDreamDomainExcluded(domain, excluded_domains_)) {
      excluded_history_visits_++;
      continue;
    }
    std::string query = ExtractSearchQuery(url.spec());
    if (!query.empty() && seen_queries.insert(query).second &&
        queries.size() < static_cast<size_t>(kMaxSearchQueries)) {
      queries.push_back(TruncateMaterialText(query));
    }
    if (domain.empty()) {
      continue;
    }
    DomainAgg& aggregate = by_domain[domain];
    aggregate.visit_count++;
    const std::string bucket = BucketFor(visit.visit_row.visit_time);
    aggregate.buckets[bucket]++;
    aggregate.total_seconds += MaterialSeconds(visit.visit_row.visit_duration);
    const DreamForegroundDatePolicy date_policy =
        ResolveDreamForegroundDatePolicy(
            FormatLocalDate(visit.visit_row.visit_time), activity_snapshot_,
            foreground_query_time_);
    if (date_policy.use_legacy) {
      const int seconds = MaterialSeconds(ForegroundDurationFor(visit));
      aggregate.foreground_seconds =
          AddMaterialSeconds(aggregate.foreground_seconds, seconds);
      aggregate.foreground_seconds_by_bucket[bucket] = AddMaterialSeconds(
          aggregate.foreground_seconds_by_bucket[bucket], seconds);
      foreground_seconds_by_bucket[bucket] =
          AddMaterialSeconds(foreground_seconds_by_bucket[bucket], seconds);
    }
    const std::string title =
        TruncateMaterialText(base::UTF16ToUTF8(visit.url_row.title()));
    if (!title.empty() &&
        aggregate.titles.size() < static_cast<size_t>(kMaxTitlesPerDomain) &&
        std::find(aggregate.titles.begin(), aggregate.titles.end(), title) ==
            aggregate.titles.end()) {
      aggregate.titles.push_back(title);
    }
  }

  std::vector<DaoForegroundActivityRow> native_rows;
  for (const auto& row : activity_snapshot_.rows) {
    if (!IsDreamForegroundRowInWindow(row, window_start_, window_end_,
                                      foreground_query_time_)) {
      continue;
    }
    const DreamForegroundDatePolicy date_policy =
        ResolveDreamForegroundDatePolicy(row.local_date, activity_snapshot_,
                                         foreground_query_time_);
    if (date_policy.use_native) {
      native_rows.push_back(row);
    }
  }
  const std::optional<DreamForegroundActivitySummary> native =
      SummarizeDreamForegroundActivity(native_rows, excluded_domains_);
  static constexpr std::array<const char*, 4> kBucketNames = {
      "morning", "afternoon", "evening", "night"};
  if (!native) {
    foreground_policy.coverage = DreamForegroundCoverage::kUnavailable;
    foreground_policy.coverage_seconds = 0;
  } else {
    for (const auto& [domain, milliseconds] : native->foreground_ms_by_host) {
      by_domain[domain].foreground_seconds = AddMaterialSeconds(
          by_domain[domain].foreground_seconds, milliseconds / 1000);
    }
    for (size_t i = 0; i < native->foreground_ms_by_bucket.size(); ++i) {
      foreground_seconds_by_bucket[kBucketNames[i]] =
          AddMaterialSeconds(foreground_seconds_by_bucket[kBucketNames[i]],
                             native->foreground_ms_by_bucket[i] / 1000);
    }
    for (const auto& [domain, buckets] :
         native->foreground_ms_by_host_and_bucket) {
      for (size_t bucket = 0; bucket < buckets.size(); ++bucket) {
        by_domain[domain].foreground_seconds_by_bucket[kBucketNames[bucket]] =
            AddMaterialSeconds(
                by_domain[domain]
                    .foreground_seconds_by_bucket[kBucketNames[bucket]],
                buckets[bucket] / 1000);
      }
    }
  }

  history_domain_count_ = static_cast<int>(by_domain.size());
  search_query_count_ = static_cast<int>(seen_queries.size());
  std::vector<std::pair<std::string, DomainAgg>> sorted(
      std::make_move_iterator(by_domain.begin()),
      std::make_move_iterator(by_domain.end()));
  std::sort(
      sorted.begin(), sorted.end(), [](const auto& left, const auto& right) {
        if (left.second.foreground_seconds != right.second.foreground_seconds) {
          return left.second.foreground_seconds >
                 right.second.foreground_seconds;
        }
        if (left.second.visit_count != right.second.visit_count) {
          return left.second.visit_count > right.second.visit_count;
        }
        return left.first < right.first;
      });
  if (sorted.size() > static_cast<size_t>(kMaxDomains)) {
    sorted.resize(kMaxDomains);
  }
  history_part_.clear();
  for (auto& [domain, aggregate] : sorted) {
    base::DictValue entry;
    entry.Set("domain", domain);
    entry.Set("visit_count", aggregate.visit_count);
    entry.Set("foreground_seconds", aggregate.foreground_seconds);
    entry.Set("total_seconds", aggregate.total_seconds);
    entry.Set("duration_level", DurationLevelFor(aggregate.foreground_seconds));
    base::ListValue titles;
    for (auto& title : aggregate.titles) {
      titles.Append(title);
    }
    entry.Set("titles", std::move(titles));
    base::DictValue buckets;
    base::DictValue foreground_buckets;
    for (const char* name : kBucketNames) {
      buckets.Set(name, aggregate.buckets[name]);
      foreground_buckets.Set(name,
                             aggregate.foreground_seconds_by_bucket[name]);
    }
    entry.Set("buckets", std::move(buckets));
    entry.Set("foreground_seconds_by_bucket", std::move(foreground_buckets));
    history_part_.Append(std::move(entry));
  }
  search_part_.clear();
  for (auto& query : queries) {
    search_part_.Append(std::move(query));
  }
  foreground_seconds_by_bucket_part_.clear();
  for (const auto& [name, seconds] : foreground_seconds_by_bucket) {
    foreground_seconds_by_bucket_part_.Set(name, seconds);
  }

  base::DictValue pack;
  base::DictValue window;
  window.Set("start", FormatLocalYmdHm(window_start_));
  window.Set("end", FormatLocalYmdHm(window_end_));
  pack.Set("window", std::move(window));
  base::DictValue stats;
  stats.Set("history_domains", history_domain_count_);
  stats.Set("search_queries", search_query_count_);
  stats.Set("conversation_sessions", conversation_session_count_);
  stats.Set("preferences", static_cast<int>(preferences_part_.size()));
  stats.Set("feedback_scenarios", static_cast<int>(feedback_part_.size()));
  stats.Set("excluded_history_visits", excluded_history_visits_);
  stats.Set("foreground_source",
            DreamForegroundSourceName(foreground_policy.source));
  stats.Set("foreground_coverage",
            DreamForegroundCoverageName(foreground_policy.coverage));
  stats.Set("coverage_seconds", static_cast<int>(std::min<int64_t>(
                                    foreground_policy.coverage_seconds,
                                    std::numeric_limits<int>::max())));
  base::ListValue source_domains;
  for (const base::Value& entry : history_part_) {
    const base::DictValue* domain_entry = entry.GetIfDict();
    if (!domain_entry) {
      continue;
    }
    const std::string* domain = domain_entry->FindString("domain");
    if (domain && !domain->empty()) {
      source_domains.Append(*domain);
    }
  }
  stats.Set("source_domains", std::move(source_domains));
  stats.Set("foreground_seconds_by_bucket",
            foreground_seconds_by_bucket_part_.Clone());
  pack.Set("stats", std::move(stats));
  pack.Set("history", std::move(history_part_));
  pack.Set("search_queries", std::move(search_part_));
  pack.Set("conversations", std::move(conversations_part_));
  pack.Set("preferences", std::move(preferences_part_));
  pack.Set("feedback", std::move(feedback_part_));
  std::move(callback_).Run(std::move(pack));
}

}  // namespace dao
