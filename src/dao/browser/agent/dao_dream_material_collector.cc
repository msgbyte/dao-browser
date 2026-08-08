// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/agent/dao_dream_material_collector.h"

#include <algorithm>
#include <limits>
#include <map>
#include <set>
#include <utility>
#include <vector>

#include "base/barrier_closure.h"
#include "base/functional/bind.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/history/history_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "components/history/core/browser/history_service.h"
#include "components/history/core/browser/history_types.h"
#include "dao/browser/agent/dao_agent_memory_service.h"
#include "dao/browser/agent/dao_dream_domain_utils.h"
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
    {"google.com", "q"},     {"bing.com", "q"},
    {"duckduckgo.com", "q"}, {"kagi.com", "q"},
    {"baidu.com", "wd"},     {"search.brave.com", "q"},
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

base::TimeDelta TotalDurationFor(const history::AnnotatedVisit& visit,
                                 base::TimeDelta foreground) {
  if (visit.visit_row.visit_duration > foreground) {
    return visit.visit_row.visit_duration;
  }
  return foreground;
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
        host == suffix ||
        (host.size() > suffix.size() + 1 &&
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

  // 5 parts: history (+search, same query), conversation excerpts, exact
  // conversation-session count, preferences, and feedback.
  barrier_ = base::BarrierClosure(
      5, base::BindOnce(&DreamMaterialCollector::OnPartDone,
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
        base::BindOnce(
            [](base::WeakPtr<DreamMaterialCollector> self,
               std::vector<history::AnnotatedVisit> visits) {
              if (!self) {
                return;
              }
              std::map<std::string, DomainAgg> by_domain;
              std::map<std::string, int> foreground_seconds_by_bucket = {
                  {"morning", 0},
                  {"afternoon", 0},
                  {"evening", 0},
                  {"night", 0},
              };
              std::vector<std::string> queries;
              std::set<std::string> seen_queries;
              for (const auto& visit : visits) {
                const GURL& url = visit.url_row.url();
                const std::string domain(url.host());
                if (IsDreamDomainExcluded(domain, self->excluded_domains_)) {
                  self->excluded_history_visits_++;
                  continue;
                }
                // Search-query extraction first (uses URL, then drops it).
                std::string q = ExtractSearchQuery(url.spec());
                if (!q.empty() && seen_queries.insert(q).second &&
                    queries.size() <
                        static_cast<size_t>(kMaxSearchQueries)) {
                  queries.push_back(TruncateMaterialText(q));
                }
                // Domain aggregation — only domain + title survive.
                if (domain.empty()) {
                  continue;
                }
                DomainAgg& agg = by_domain[domain];
                agg.visit_count++;
                const base::TimeDelta foreground =
                    ForegroundDurationFor(visit);
                const int foreground_seconds = MaterialSeconds(foreground);
                agg.foreground_seconds += foreground_seconds;
                agg.total_seconds +=
                    MaterialSeconds(TotalDurationFor(visit, foreground));
                const std::string bucket =
                    BucketFor(visit.visit_row.visit_time);
                agg.buckets[bucket]++;
                agg.foreground_seconds_by_bucket[bucket] += foreground_seconds;
                foreground_seconds_by_bucket[bucket] += foreground_seconds;
                const std::string title =
                    TruncateMaterialText(
                        base::UTF16ToUTF8(visit.url_row.title()));
                if (!title.empty() &&
                    agg.titles.size() <
                        static_cast<size_t>(kMaxTitlesPerDomain) &&
                    std::find(agg.titles.begin(), agg.titles.end(),
                              title) == agg.titles.end()) {
                  agg.titles.push_back(title);
                }
              }
              // Top-N domains by foreground attention, then visit count.
              const int domain_count = static_cast<int>(by_domain.size());
              const int query_count = static_cast<int>(seen_queries.size());
              std::vector<std::pair<std::string, DomainAgg>> sorted(
                  std::make_move_iterator(by_domain.begin()),
                  std::make_move_iterator(by_domain.end()));
              std::sort(sorted.begin(), sorted.end(),
                        [](const auto& a, const auto& b) {
                          if (a.second.foreground_seconds !=
                              b.second.foreground_seconds) {
                            return a.second.foreground_seconds >
                                   b.second.foreground_seconds;
                          }
                          if (a.second.visit_count != b.second.visit_count) {
                            return a.second.visit_count >
                                   b.second.visit_count;
                          }
                          return a.first < b.first;
                        });
              if (sorted.size() > static_cast<size_t>(kMaxDomains)) {
                sorted.resize(kMaxDomains);
              }
              base::ListValue domains;
              for (auto& [domain, agg] : sorted) {
                base::DictValue d;
                d.Set("domain", domain);
                d.Set("visit_count", agg.visit_count);
                d.Set("foreground_seconds", agg.foreground_seconds);
                d.Set("total_seconds", agg.total_seconds);
                d.Set("duration_level",
                      DurationLevelFor(agg.foreground_seconds));
                base::ListValue titles;
                for (auto& t : agg.titles) {
                  titles.Append(t);
                }
                d.Set("titles", std::move(titles));
                base::DictValue buckets;
                for (auto& [name, count] : agg.buckets) {
                  buckets.Set(name, count);
                }
                d.Set("buckets", std::move(buckets));
                base::DictValue foreground_seconds_by_bucket;
                for (auto& [name, seconds] :
                     agg.foreground_seconds_by_bucket) {
                  foreground_seconds_by_bucket.Set(name, seconds);
                }
                d.Set("foreground_seconds_by_bucket",
                      std::move(foreground_seconds_by_bucket));
                domains.Append(std::move(d));
              }
              base::ListValue query_list;
              for (auto& q : queries) {
                query_list.Append(q);
              }
              base::DictValue foreground_bucket_stats;
              for (const auto& [name, seconds] :
                   foreground_seconds_by_bucket) {
                foreground_bucket_stats.Set(name, seconds);
              }
              self->OnHistoryResults(
                  std::move(domains), std::move(query_list), domain_count,
                  query_count, std::move(foreground_bucket_stats));
            },
            weak_factory_.GetWeakPtr()),
        &history_tracker_);
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
                         .emplace(msg.session_id,
                                  std::vector<std::string>())
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
    base::ListValue domains,
    base::ListValue queries,
    int domain_count,
    int query_count,
    base::DictValue foreground_seconds_by_bucket) {
  history_part_ = std::move(domains);
  search_part_ = std::move(queries);
  history_domain_count_ = domain_count;
  search_query_count_ = query_count;
  foreground_seconds_by_bucket_part_ =
      std::move(foreground_seconds_by_bucket);
  barrier_.Run();
}

void DreamMaterialCollector::OnConversationsLoaded(
    base::ListValue sessions) {
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
