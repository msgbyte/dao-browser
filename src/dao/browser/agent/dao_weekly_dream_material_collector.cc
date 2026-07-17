// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/agent/dao_weekly_dream_material_collector.h"

#include <algorithm>
#include <cctype>
#include <limits>
#include <map>
#include <set>
#include <string_view>
#include <utility>

#include "base/barrier_closure.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/history/history_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "components/history/core/browser/history_service.h"
#include "dao/browser/agent/dao_agent_memory_service.h"
#include "dao/browser/agent/dao_dream_domain_utils.h"
#include "url/gurl.h"

namespace dao {

namespace {

constexpr int kConversationQueryLimit = 1000;

const char* TimeBucketFor(base::Time timestamp) {
  base::Time::Exploded exploded;
  timestamp.LocalExplode(&exploded);
  if (exploded.hour >= 6 && exploded.hour < 12) {
    return "morning";
  }
  if (exploded.hour >= 12 && exploded.hour < 18) {
    return "afternoon";
  }
  if (exploded.hour >= 18 && exploded.hour < 22) {
    return "evening";
  }
  return "night";
}

std::string LocalDate(base::Time timestamp) {
  base::Time::Exploded exploded;
  timestamp.LocalExplode(&exploded);
  return base::StringPrintf("%04d-%02d-%02d", exploded.year, exploded.month,
                            exploded.day_of_month);
}

int ClampToInt(int64_t value) {
  return static_cast<int>(
      std::clamp<int64_t>(value, 0, std::numeric_limits<int>::max()));
}

int64_t MaterialSeconds(base::TimeDelta duration) {
  return std::max<int64_t>(duration.InSeconds(), 0);
}

base::TimeDelta ForegroundDurationFor(
    const history::AnnotatedVisit& visit) {
  const base::TimeDelta foreground =
      visit.context_annotations.total_foreground_duration;
  if (foreground >= base::Seconds(0)) {
    return foreground;
  }
  return std::max(visit.visit_row.visit_duration, base::Seconds(0));
}

base::TimeDelta TotalDurationFor(const history::AnnotatedVisit& visit,
                                 base::TimeDelta foreground) {
  return std::max(visit.visit_row.visit_duration, foreground);
}

bool IsSchemeCharacter(char character) {
  const unsigned char value = static_cast<unsigned char>(character);
  return std::isalnum(value) || character == '+' || character == '-' ||
         character == '.';
}

bool IsUrlTerminator(char character) {
  const unsigned char value = static_cast<unsigned char>(character);
  return std::isspace(value) || character == '"' || character == '\'' ||
         character == '<' || character == '>' || character == '(' ||
         character == ')' || character == '[' || character == ']' ||
         character == '{' || character == '}';
}

bool IsLinkBoundary(const std::string& text, size_t position) {
  if (position == 0) {
    return true;
  }
  const unsigned char previous =
      static_cast<unsigned char>(text[position - 1]);
  return !std::isalnum(previous) && previous != '_';
}

bool StartsWithInsensitive(const std::string& text,
                           size_t position,
                           std::string_view prefix) {
  return position + prefix.size() <= text.size() &&
         base::EqualsCaseInsensitiveASCII(
             std::string_view(text).substr(position, prefix.size()), prefix);
}

size_t LinkEndAt(const std::string& text, size_t position) {
  if (!IsLinkBoundary(text, position)) {
    return position;
  }

  bool is_link = false;
  size_t payload_start = position;
  if (position + 2 < text.size() && text[position] == '/' &&
      text[position + 1] == '/' &&
      !IsUrlTerminator(text[position + 2])) {
    is_link = true;
    payload_start = position + 2;
  }
  if (!is_link && StartsWithInsensitive(text, position, "www.") &&
      position + 4 < text.size() &&
      !IsUrlTerminator(text[position + 4])) {
    is_link = true;
    payload_start = position + 4;
  }

  if (!is_link && position < text.size() &&
      std::isalpha(static_cast<unsigned char>(text[position]))) {
    size_t scheme_end = position + 1;
    while (scheme_end < text.size() &&
           IsSchemeCharacter(text[scheme_end])) {
      ++scheme_end;
    }
    is_link = scheme_end + 1 < text.size() && text[scheme_end] == ':' &&
              !std::isspace(
                  static_cast<unsigned char>(text[scheme_end + 1]));
    if (is_link) {
      payload_start = scheme_end + 1;
    }
  }
  if (!is_link) {
    return position;
  }

  size_t link_end = position;
  while (link_end < text.size() && !IsUrlTerminator(text[link_end])) {
    ++link_end;
  }

  const size_t minimum_link_end = payload_start + 1;
  constexpr std::string_view kUnicodeTrailingPunctuation[] = {
      "\xE3\x80\x81",  // Ideographic comma.
      "\xE3\x80\x82",  // Ideographic full stop.
      "\xEF\xBC\x8C",  // Fullwidth comma.
      "\xEF\xBC\x9A",  // Fullwidth colon.
      "\xEF\xBC\x9B",  // Fullwidth semicolon.
      "\xEF\xBC\x81",  // Fullwidth exclamation mark.
      "\xEF\xBC\x9F",  // Fullwidth question mark.
      "\xEF\xBC\x89",  // Fullwidth closing parenthesis.
      "\xE3\x80\x8B",  // Right double angle bracket.
      "\xE3\x80\x91",  // Right black lenticular bracket.
      "\xE2\x80\x99",  // Right single quotation mark.
      "\xE2\x80\x9D",  // Right double quotation mark.
  };
  bool trimmed = true;
  while (trimmed && link_end > minimum_link_end) {
    trimmed = false;
    if (std::string_view(".,;:!?").find(text[link_end - 1]) !=
        std::string_view::npos) {
      --link_end;
      trimmed = true;
      continue;
    }
    for (std::string_view punctuation : kUnicodeTrailingPunctuation) {
      if (link_end >= minimum_link_end + punctuation.size() &&
          text.compare(link_end - punctuation.size(), punctuation.size(),
                       punctuation.data(), punctuation.size()) == 0) {
        link_end -= punctuation.size();
        trimmed = true;
        break;
      }
    }
  }
  return link_end;
}

std::string RedactLinks(std::string text) {
  size_t position = 0;
  while (position < text.size()) {
    const size_t link_end = LinkEndAt(text, position);
    if (link_end == position) {
      ++position;
      continue;
    }
    text.replace(position, link_end - position, "[link]");
    position += 6;
  }
  return text;
}

void RedactSensitiveValue(std::string* text, std::string_view sensitive) {
  if (sensitive.empty()) {
    return;
  }
  size_t position = 0;
  while ((position = text->find(sensitive, position)) != std::string::npos) {
    text->replace(position, sensitive.size(), "[session]");
    position += 9;
  }
}

void EraseAll(std::string* text, std::string_view value) {
  size_t position = 0;
  while ((position = text->find(value, position)) != std::string::npos) {
    text->erase(position, value.size());
  }
}

bool IsMaterialDecoration(char16_t character) {
  if (character <= 0x7f) {
    const unsigned char ascii = static_cast<unsigned char>(character);
    return std::isspace(ascii) || std::ispunct(ascii);
  }
  return character == 0x00a0 || character == 0x1680 ||
         (character >= 0x2000 && character <= 0x206f) ||
         (character >= 0x3000 && character <= 0x303f) ||
         (character >= 0xfe10 && character <= 0xfe1f) ||
         (character >= 0xfe30 && character <= 0xfe4f) ||
         (character >= 0xff01 && character <= 0xff0f) ||
         (character >= 0xff1a && character <= 0xff20) ||
         (character >= 0xff3b && character <= 0xff40) ||
         (character >= 0xff5b && character <= 0xff65);
}

bool HasSubstantiveMaterialText(std::string text) {
  EraseAll(&text, "[link]");
  EraseAll(&text, "[session]");
  const std::u16string utf16 = base::UTF8ToUTF16(text);
  return std::any_of(utf16.begin(), utf16.end(), [](char16_t character) {
    return !IsMaterialDecoration(character);
  });
}

std::string SanitizeMaterialText(
    const std::string& text,
    const std::set<std::string>& sensitive_values) {
  std::string sanitized = RedactLinks(text);
  for (const std::string& sensitive : sensitive_values) {
    RedactSensitiveValue(&sanitized, sensitive);
  }
  std::u16string utf16 = base::UTF8ToUTF16(sanitized);
  if (utf16.size() > static_cast<size_t>(
                         WeeklyDreamMaterialCollector::kMaxTextChars)) {
    utf16.resize(WeeklyDreamMaterialCollector::kMaxTextChars);
  }
  return base::UTF16ToUTF8(utf16);
}

bool IsUserQuestion(const std::string& text) {
  return text.find('?') != std::string::npos ||
         text.find("\xEF\xBC\x9F") != std::string::npos;
}

std::string SafeDomain(const std::string& input) {
  const std::string trimmed(
      base::TrimWhitespaceASCII(input, base::TRIM_ALL));
  if (trimmed.empty()) {
    return std::string();
  }
  GURL parsed(trimmed);
  if (!parsed.is_valid() || !parsed.has_host()) {
    parsed = GURL("https://" + trimmed);
  }
  if (!parsed.is_valid() || !parsed.has_host()) {
    return std::string();
  }
  return base::ToLowerASCII(parsed.host());
}

bool IsExcludedSourceDomain(
    const std::string& domain,
    const std::set<std::string>& excluded_domains) {
  const std::string normalized = NormalizeDreamExcludedDomain(domain);
  return !normalized.empty() &&
         IsDreamDomainExcluded(normalized, excluded_domains);
}

struct DomainAggregate {
  std::string domain;
  int visit_count = 0;
  int64_t foreground_seconds = 0;
  int64_t total_seconds = 0;
  std::map<std::string, int> time_buckets;
  std::set<std::string> coverage_dates;
};

struct PageCandidate {
  std::string title;
  std::string domain;
  std::string local_locator;
  int64_t foreground_seconds = 0;
  base::Time last_seen_at;
};

struct FallbackQuestion {
  int64_t message_id = 0;
  std::string session_id;
  std::string text;
  base::Time timestamp;
  std::string domain;
};

struct ConversationCandidate {
  bool has_summary = false;
  int64_t summary_id = 0;
  std::string session_id;
  std::string summary;
  int message_count = 0;
  std::string primary_domain;
  base::Time first_timestamp;
  base::Time last_timestamp;
  std::vector<FallbackQuestion> fallback_questions;
};

bool PreferSummaryCandidate(const ConversationCandidate& candidate,
                            const ConversationCandidate& current) {
  if (candidate.last_timestamp != current.last_timestamp) {
    return candidate.last_timestamp > current.last_timestamp;
  }
  if (candidate.summary_id != current.summary_id) {
    return candidate.summary_id > current.summary_id;
  }
  if (candidate.first_timestamp != current.first_timestamp) {
    return candidate.first_timestamp > current.first_timestamp;
  }
  if (candidate.summary != current.summary) {
    return candidate.summary < current.summary;
  }
  if (candidate.primary_domain != current.primary_domain) {
    return candidate.primary_domain < current.primary_domain;
  }
  return candidate.message_count > current.message_count;
}

void CopySanitizedString(const base::DictValue& input,
                         std::string_view key,
                         const std::set<std::string>& sensitive_values,
                         base::DictValue* output) {
  const std::string* value = input.FindString(key);
  if (value) {
    output->Set(key, SanitizeMaterialText(*value, sensitive_values));
  }
}

base::DictValue RebuildPreviousThread(
    const base::DictValue& input,
    const std::set<std::string>& sensitive_values) {
  base::DictValue thread;
  CopySanitizedString(input, "title", sensitive_values, &thread);
  CopySanitizedString(input, "status_summary", sensitive_values, &thread);
  CopySanitizedString(input, "next_step", sensitive_values, &thread);
  if (std::optional<double> confidence = input.FindDouble("confidence")) {
    thread.Set("confidence", *confidence);
  }
  return thread;
}

base::DictValue RebuildPreviousContent(
    const base::DictValue& input,
    const std::set<std::string>& sensitive_values) {
  base::DictValue content;
  CopySanitizedString(input, "headline", sensitive_values, &content);

  if (const base::DictValue* primary = input.FindDict("primary_thread")) {
    content.Set("primary_thread",
                RebuildPreviousThread(*primary, sensitive_values));
  }

  if (const base::ListValue* secondary =
          input.FindList("secondary_threads")) {
    base::ListValue rebuilt;
    for (const base::Value& value : *secondary) {
      const base::DictValue* thread = value.GetIfDict();
      if (!thread) {
        continue;
      }
      rebuilt.Append(RebuildPreviousThread(*thread, sensitive_values));
      if (rebuilt.size() == 2) {
        break;
      }
    }
    content.Set("secondary_threads", std::move(rebuilt));
  }

  if (const base::ListValue* outcomes =
          input.FindList("retained_outcomes")) {
    base::ListValue rebuilt;
    for (const base::Value& value : *outcomes) {
      const base::DictValue* outcome = value.GetIfDict();
      if (!outcome) {
        continue;
      }
      base::DictValue rebuilt_outcome;
      CopySanitizedString(*outcome, "text", sensitive_values,
                          &rebuilt_outcome);
      if (std::optional<double> confidence =
              outcome->FindDouble("confidence")) {
        rebuilt_outcome.Set("confidence", *confidence);
      }
      rebuilt.Append(std::move(rebuilt_outcome));
      if (rebuilt.size() == 3) {
        break;
      }
    }
    content.Set("retained_outcomes", std::move(rebuilt));
  }

  if (const base::DictValue* footprint =
          input.FindDict("footprint_summary")) {
    base::DictValue rebuilt_footprint;
    if (const base::ListValue* themes = footprint->FindList("themes")) {
      base::ListValue rebuilt_themes;
      for (const base::Value& theme : *themes) {
        if (!theme.is_string()) {
          continue;
        }
        rebuilt_themes.Append(
            SanitizeMaterialText(theme.GetString(), sensitive_values));
        if (rebuilt_themes.size() == 5) {
          break;
        }
      }
      rebuilt_footprint.Set("themes", std::move(rebuilt_themes));
    }
    CopySanitizedString(*footprint, "time_pattern", sensitive_values,
                        &rebuilt_footprint);
    content.Set("footprint_summary", std::move(rebuilt_footprint));
  }

  return content;
}

}  // namespace

WeeklyDreamMaterial::WeeklyDreamMaterial() = default;
WeeklyDreamMaterial::WeeklyDreamMaterial(WeeklyDreamMaterial&&) = default;
WeeklyDreamMaterial& WeeklyDreamMaterial::operator=(WeeklyDreamMaterial&&) =
    default;
WeeklyDreamMaterial::~WeeklyDreamMaterial() = default;

WeeklyDreamMaterialCollector::WeeklyDreamMaterialCollector(
    Profile* profile,
    DaoAgentMemoryService* memory_service)
    : profile_(profile), memory_service_(memory_service) {}

WeeklyDreamMaterialCollector::~WeeklyDreamMaterialCollector() = default;

void WeeklyDreamMaterialCollector::Collect(base::Time window_start,
                                           base::Time window_end,
                                           const std::string& week_start,
                                           const std::string& week_end,
                                           CollectCallback callback) {
  DCHECK(callback_.is_null()) << "Collection already in flight";
  DCHECK(!callback.is_null());
  window_start_ = window_start;
  window_end_ = window_end;
  week_start_ = week_start;
  week_end_ = week_end;
  callback_ = std::move(callback);
  history_visits_.clear();
  conversation_summaries_.clear();
  conversation_messages_.clear();
  daily_reports_.clear();
  previous_weekly_report_.reset();

  // History, summaries, fallback messages, daily reports, and previous week.
  barrier_ = base::BarrierClosure(
      5, base::BindOnce(&WeeklyDreamMaterialCollector::OnAllPartsLoaded,
                        weak_factory_.GetWeakPtr()));

  history::HistoryService* history = HistoryServiceFactory::GetForProfile(
      profile_, ServiceAccessType::EXPLICIT_ACCESS);
  if (!history) {
    barrier_.Run();
  } else {
    history::QueryOptions options;
    options.begin_time = window_start;
    options.end_time = window_end;
    options.max_count = 0;
    options.duplicate_policy = history::QueryOptions::KEEP_ALL_DUPLICATES;
    history->GetAnnotatedVisits(
        options, /*compute_redirect_chain_start_properties=*/false,
        /*get_unclustered_visits_only=*/false,
        base::BindOnce(&WeeklyDreamMaterialCollector::OnHistoryLoaded,
                       weak_factory_.GetWeakPtr()),
        &history_tracker_);
  }

  if (!memory_service_) {
    for (int i = 0; i < 4; ++i) {
      barrier_.Run();
    }
    return;
  }

  memory_service_->LoadConversationSummariesInRange(
      window_start, window_end, kConversationQueryLimit,
      base::BindOnce(
          &WeeklyDreamMaterialCollector::OnConversationSummariesLoaded,
          weak_factory_.GetWeakPtr()));
  memory_service_->LoadConversationMessagesInRange(
      window_start, window_end, kConversationQueryLimit,
      base::BindOnce(
          &WeeklyDreamMaterialCollector::OnConversationMessagesLoaded,
          weak_factory_.GetWeakPtr()));
  memory_service_->GetDreamReportsInDateRange(
      week_start, week_end, kMaxDailyReports,
      base::BindOnce(&WeeklyDreamMaterialCollector::OnDailyReportsLoaded,
                     weak_factory_.GetWeakPtr()));
  memory_service_->GetLatestWeeklyDreamReportBefore(
      week_start,
      base::BindOnce(
          &WeeklyDreamMaterialCollector::OnPreviousWeeklyReportLoaded,
          weak_factory_.GetWeakPtr()));
}

void WeeklyDreamMaterialCollector::OnHistoryLoaded(
    std::vector<history::AnnotatedVisit> visits) {
  history_visits_ = std::move(visits);
  barrier_.Run();
}

void WeeklyDreamMaterialCollector::OnConversationSummariesLoaded(
    std::vector<ConversationSummary> summaries) {
  conversation_summaries_ = std::move(summaries);
  barrier_.Run();
}

void WeeklyDreamMaterialCollector::OnConversationMessagesLoaded(
    std::vector<ConversationMessage> messages) {
  conversation_messages_ = std::move(messages);
  barrier_.Run();
}

void WeeklyDreamMaterialCollector::OnDailyReportsLoaded(
    std::vector<DreamReport> reports) {
  daily_reports_ = std::move(reports);
  barrier_.Run();
}

void WeeklyDreamMaterialCollector::OnPreviousWeeklyReportLoaded(
    std::optional<WeeklyDreamReport> report) {
  previous_weekly_report_ = std::move(report);
  barrier_.Run();
}

void WeeklyDreamMaterialCollector::OnAllPartsLoaded() {
  const std::set<std::string> excluded_domains =
      LoadDreamExcludedDomains(profile_);
  std::set<std::string> sensitive_session_ids;
  for (const ConversationSummary& summary : conversation_summaries_) {
    if (!summary.session_id.empty()) {
      sensitive_session_ids.insert(summary.session_id);
    }
  }
  for (const ConversationMessage& message : conversation_messages_) {
    if (!message.session_id.empty()) {
      sensitive_session_ids.insert(message.session_id);
    }
  }
  WeeklyDreamMaterial material;
  std::set<std::string> coverage_dates;

  std::map<std::string, DomainAggregate> domains_by_name;
  std::map<std::string, PageCandidate> pages_by_locator;
  for (const history::AnnotatedVisit& visit : history_visits_) {
    const GURL& url = visit.url_row.url();
    if (!url.is_valid() || !url.has_host()) {
      continue;
    }
    const std::string domain = base::ToLowerASCII(url.host());
    if (domain.empty() ||
        IsExcludedSourceDomain(domain, excluded_domains)) {
      continue;
    }

    const base::TimeDelta foreground = ForegroundDurationFor(visit);
    const base::TimeDelta total = TotalDurationFor(visit, foreground);
    DomainAggregate& aggregate = domains_by_name[domain];
    aggregate.domain = domain;
    aggregate.visit_count++;
    aggregate.foreground_seconds += MaterialSeconds(foreground);
    aggregate.total_seconds += MaterialSeconds(total);
    aggregate.time_buckets[TimeBucketFor(visit.visit_row.visit_time)]++;
    aggregate.coverage_dates.insert(LocalDate(visit.visit_row.visit_time));

    const std::string title = SanitizeMaterialText(
        base::UTF16ToUTF8(visit.url_row.title()), sensitive_session_ids);
    if (title.empty()) {
      continue;
    }
    PageCandidate& page = pages_by_locator[url.spec()];
    const bool replace_title =
        page.last_seen_at.is_null() ||
        visit.visit_row.visit_time > page.last_seen_at ||
        (visit.visit_row.visit_time == page.last_seen_at &&
         title < page.title);
    if (replace_title) {
      page.title = title;
    }
    page.domain = domain;
    page.local_locator = url.spec();
    page.foreground_seconds += MaterialSeconds(foreground);
    page.last_seen_at =
        std::max(page.last_seen_at, visit.visit_row.visit_time);
  }

  std::vector<DomainAggregate*> sorted_domains;
  sorted_domains.reserve(domains_by_name.size());
  for (auto& entry : domains_by_name) {
    sorted_domains.push_back(&entry.second);
  }
  std::sort(sorted_domains.begin(), sorted_domains.end(),
            [](const DomainAggregate* left, const DomainAggregate* right) {
              if (left->foreground_seconds != right->foreground_seconds) {
                return left->foreground_seconds > right->foreground_seconds;
              }
              if (left->visit_count != right->visit_count) {
                return left->visit_count > right->visit_count;
              }
              return left->domain < right->domain;
            });
  if (sorted_domains.size() > static_cast<size_t>(kMaxDomains)) {
    sorted_domains.resize(kMaxDomains);
  }
  std::set<std::string> retained_domains;
  for (const DomainAggregate* domain : sorted_domains) {
    retained_domains.insert(domain->domain);
  }

  std::vector<PageCandidate*> sorted_pages;
  sorted_pages.reserve(pages_by_locator.size());
  for (auto& entry : pages_by_locator) {
    PageCandidate& page = entry.second;
    if (retained_domains.contains(page.domain)) {
      sorted_pages.push_back(&page);
    }
  }
  std::sort(sorted_pages.begin(), sorted_pages.end(),
            [](const PageCandidate* left, const PageCandidate* right) {
              if (left->foreground_seconds != right->foreground_seconds) {
                return left->foreground_seconds > right->foreground_seconds;
              }
              if (left->last_seen_at != right->last_seen_at) {
                return left->last_seen_at > right->last_seen_at;
              }
              if (left->title != right->title) {
                return left->title < right->title;
              }
              if (left->domain != right->domain) {
                return left->domain < right->domain;
              }
              return left->local_locator < right->local_locator;
            });

  std::set<std::string> seen_page_titles;
  std::map<std::string, int> pages_per_domain;
  std::map<std::string, std::vector<base::DictValue>> model_pages_by_domain;
  int page_ref_number = 0;
  for (PageCandidate* page : sorted_pages) {
    if (page_ref_number == kMaxPageSources) {
      break;
    }
    const std::string title_key = base::ToLowerASCII(page->title);
    if (!seen_page_titles.insert(title_key).second ||
        pages_per_domain[page->domain] == kMaxTitlesPerDomain) {
      continue;
    }
    pages_per_domain[page->domain]++;
    const std::string ref_id =
        base::StringPrintf("page_%d", ++page_ref_number);
    base::DictValue model_page;
    model_page.Set("ref_id", ref_id);
    model_page.Set("title", page->title);
    model_pages_by_domain[page->domain].push_back(std::move(model_page));

    WeeklyDreamSource source;
    source.ref_id = ref_id;
    source.source_kind = "page";
    source.title = page->title;
    source.domain = page->domain;
    source.local_locator = page->local_locator;
    source.last_seen_at = page->last_seen_at;
    material.local_sources.push_back(std::move(source));
  }

  base::ListValue history_material;
  for (DomainAggregate* domain : sorted_domains) {
    base::DictValue entry;
    entry.Set("domain", domain->domain);
    entry.Set("visit_count", domain->visit_count);
    entry.Set("foreground_seconds",
              ClampToInt(domain->foreground_seconds));
    entry.Set("total_seconds", ClampToInt(domain->total_seconds));
    base::DictValue buckets;
    for (const char* name : {"morning", "afternoon", "evening", "night"}) {
      buckets.Set(name, domain->time_buckets[name]);
    }
    entry.Set("time_buckets", std::move(buckets));
    base::ListValue pages;
    for (base::DictValue& page : model_pages_by_domain[domain->domain]) {
      pages.Append(std::move(page));
    }
    entry.Set("pages", std::move(pages));
    history_material.Append(std::move(entry));
    coverage_dates.insert(domain->coverage_dates.begin(),
                          domain->coverage_dates.end());
  }

  std::map<std::string, ConversationCandidate> summaries_by_session;
  for (const ConversationSummary& summary : conversation_summaries_) {
    if (summary.session_id.empty()) {
      continue;
    }
    const std::string domain = SafeDomain(summary.primary_domain);
    if (!domain.empty() &&
        IsExcludedSourceDomain(domain, excluded_domains)) {
      continue;
    }
    const std::string summary_text =
        SanitizeMaterialText(summary.summary, sensitive_session_ids);
    if (!HasSubstantiveMaterialText(summary_text)) {
      continue;
    }
    ConversationCandidate candidate;
    candidate.has_summary = true;
    candidate.summary_id = summary.id;
    candidate.session_id = summary.session_id;
    candidate.summary = summary_text;
    candidate.message_count = summary.message_count;
    candidate.primary_domain = domain;
    candidate.first_timestamp = summary.first_timestamp;
    candidate.last_timestamp = summary.last_timestamp;

    auto existing = summaries_by_session.find(summary.session_id);
    if (existing == summaries_by_session.end() ||
        PreferSummaryCandidate(candidate, existing->second)) {
      summaries_by_session.insert_or_assign(summary.session_id,
                                            std::move(candidate));
    }
  }

  std::set<std::string> sessions_with_summary;
  std::vector<ConversationCandidate> conversation_candidates;
  for (auto& entry : summaries_by_session) {
    sessions_with_summary.insert(entry.first);
    conversation_candidates.push_back(std::move(entry.second));
  }

  std::vector<FallbackQuestion> selected_fallback_questions;
  for (const ConversationMessage& message : conversation_messages_) {
    if (message.role != "user" || message.session_id.empty() ||
        sessions_with_summary.contains(message.session_id)) {
      continue;
    }
    const std::string domain = SafeDomain(message.page_url);
    if (!domain.empty() &&
        IsExcludedSourceDomain(domain, excluded_domains)) {
      continue;
    }
    const std::string text =
        SanitizeMaterialText(message.content, sensitive_session_ids);
    if (!IsUserQuestion(text) || !HasSubstantiveMaterialText(text)) {
      continue;
    }
    FallbackQuestion question;
    question.message_id = message.id;
    question.session_id = message.session_id;
    question.text = text;
    question.timestamp = message.timestamp;
    question.domain = domain;
    selected_fallback_questions.push_back(std::move(question));
  }

  std::sort(selected_fallback_questions.begin(),
            selected_fallback_questions.end(),
            [](const FallbackQuestion& left,
               const FallbackQuestion& right) {
              if (left.timestamp != right.timestamp) {
                return left.timestamp > right.timestamp;
              }
              if (left.session_id != right.session_id) {
                return left.session_id < right.session_id;
              }
              if (left.message_id != right.message_id) {
                return left.message_id > right.message_id;
              }
              if (left.text != right.text) {
                return left.text < right.text;
              }
              return left.domain < right.domain;
            });
  if (selected_fallback_questions.size() >
      static_cast<size_t>(kMaxFallbackMessages)) {
    selected_fallback_questions.resize(kMaxFallbackMessages);
  }

  std::map<std::string, ConversationCandidate> fallback_by_session;
  for (FallbackQuestion& question : selected_fallback_questions) {
    ConversationCandidate& candidate =
        fallback_by_session[question.session_id];
    candidate.session_id = question.session_id;
    candidate.last_timestamp =
        std::max(candidate.last_timestamp, question.timestamp);
    if (candidate.primary_domain.empty() && !question.domain.empty()) {
      candidate.primary_domain = question.domain;
    }
    candidate.fallback_questions.push_back(std::move(question));
  }
  for (auto& entry : fallback_by_session) {
    ConversationCandidate& candidate = entry.second;
    std::sort(candidate.fallback_questions.begin(),
              candidate.fallback_questions.end(),
              [](const FallbackQuestion& left,
                 const FallbackQuestion& right) {
                if (left.timestamp != right.timestamp) {
                  return left.timestamp < right.timestamp;
                }
                if (left.message_id != right.message_id) {
                  return left.message_id < right.message_id;
                }
                return left.text < right.text;
              });
    conversation_candidates.push_back(std::move(candidate));
  }
  std::sort(conversation_candidates.begin(), conversation_candidates.end(),
            [](const ConversationCandidate& left,
               const ConversationCandidate& right) {
              if (left.last_timestamp != right.last_timestamp) {
                return left.last_timestamp > right.last_timestamp;
              }
              return left.session_id < right.session_id;
            });

  base::ListValue conversations_material;
  base::ListValue fallback_material;
  int conversation_ref_number = 0;
  for (const ConversationCandidate& candidate : conversation_candidates) {
    if (conversation_ref_number == kMaxConversationSources) {
      break;
    }
    const std::string ref_id =
        base::StringPrintf("conversation_%d", ++conversation_ref_number);
    std::string source_title;
    if (candidate.has_summary) {
      base::DictValue conversation;
      conversation.Set("ref_id", ref_id);
      conversation.Set("summary", candidate.summary);
      conversation.Set("message_count", candidate.message_count);
      conversation.Set("primary_domain", candidate.primary_domain);
      conversations_material.Append(std::move(conversation));
      source_title = candidate.summary;
    } else {
      for (const FallbackQuestion& question :
           candidate.fallback_questions) {
        base::DictValue fallback;
        fallback.Set("ref_id", ref_id);
        fallback.Set("text", question.text);
        fallback_material.Append(std::move(fallback));
        source_title = question.text;
        coverage_dates.insert(LocalDate(question.timestamp));
      }
    }

    WeeklyDreamSource source;
    source.ref_id = ref_id;
    source.source_kind = "conversation";
    source.title = source_title;
    source.domain = candidate.primary_domain;
    source.local_locator = candidate.session_id;
    source.last_seen_at = candidate.last_timestamp;
    material.local_sources.push_back(std::move(source));
    coverage_dates.insert(LocalDate(candidate.last_timestamp));
  }

  std::sort(daily_reports_.begin(), daily_reports_.end(),
            [](const DreamReport& left, const DreamReport& right) {
              return left.dream_date < right.dream_date;
            });
  base::ListValue daily_material;
  for (const DreamReport& report : daily_reports_) {
    if (daily_material.size() == kMaxDailyReports) {
      break;
    }
    base::DictValue daily;
    daily.Set("dream_date", report.dream_date);
    daily.Set(
        "report_markdown",
        SanitizeMaterialText(report.report_markdown, sensitive_session_ids));
    daily_material.Append(std::move(daily));
    coverage_dates.insert(report.dream_date);
  }
  const int retained_daily_report_count =
      static_cast<int>(daily_material.size());

  base::DictValue pack;
  base::DictValue period;
  period.Set("week_start", week_start_);
  period.Set("week_end", week_end_);
  pack.Set("period", std::move(period));
  pack.Set("history", std::move(history_material));
  pack.Set("daily_reports", std::move(daily_material));
  pack.Set("conversations", std::move(conversations_material));
  pack.Set("fallback_questions", std::move(fallback_material));

  if (previous_weekly_report_) {
    std::optional<base::Value> parsed = base::JSONReader::Read(
        previous_weekly_report_->content_json, base::JSON_PARSE_RFC);
    if (parsed && parsed->is_dict()) {
      base::DictValue previous_week;
      previous_week.Set(
          "content", RebuildPreviousContent(parsed->GetDict(),
                                              sensitive_session_ids));
      pack.Set("previous_week", std::move(previous_week));
    }
  }

  const int conversation_source_count = conversation_ref_number;
  base::DictValue stats;
  stats.Set("history_domains", static_cast<int>(sorted_domains.size()));
  stats.Set("page_sources", page_ref_number);
  stats.Set("conversation_sources", conversation_source_count);
  stats.Set("daily_reports", retained_daily_report_count);
  stats.Set("coverage_days", static_cast<int>(coverage_dates.size()));
  stats.Set("source_count", static_cast<int>(material.local_sources.size()));
  pack.Set("stats", std::move(stats));

  material.model_material = std::move(pack);
  std::move(callback_).Run(std::move(material));
}

}  // namespace dao
