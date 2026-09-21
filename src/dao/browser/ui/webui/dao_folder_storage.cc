// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/ui/webui/dao_folder_storage.h"

#include <map>
#include <set>
#include <utility>
#include <vector>

#include "base/files/file_util.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/memory/raw_ptr.h"
#include "base/no_destructor.h"
#include "base/strings/string_number_conversions.h"
#include "base/synchronization/lock.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/thread_pool.h"
#include "base/values.h"
#include "dao/browser/ui/webui/dao_pinned_tab_storage.h"

namespace dao {
namespace {

base::Lock& FolderFileLock() {
  // ponytail: serialize the small folder files and synchronous importer writes;
  // use per-profile locks only if cross-profile disk contention becomes real.
  static base::NoDestructor<base::Lock> lock;
  return *lock;
}

std::optional<std::string> ReadFile(const base::FilePath& path) {
  std::string json;
  const bool read = base::ReadFileToString(path, &json);
  if ((!read || json.empty()) && base::PathExists(path)) {
    return std::nullopt;
  }
  return json;
}

bool ValidItems(const base::ListValue& items, bool allow_folders) {
  for (const base::Value& value : items) {
    const auto* item = value.GetIfDict();
    const auto* type = item ? item->FindString("type") : nullptr;
    if (!type) {
      return false;
    }
    if (*type == "tab") {
      if (!item->FindString("url") || !item->FindString("title") ||
          (item->contains("tabId") && !item->FindString("tabId"))) {
        return false;
      }
    } else if (*type == "folder" && allow_folders) {
      const auto* children = item->FindList("children");
      if (!item->FindString("id") || !item->FindString("name") ||
          !item->FindBool("collapsed").has_value() || !children ||
          !ValidItems(*children, false)) {
        return false;
      }
    } else {
      return false;
    }
  }
  return true;
}

std::optional<base::DictValue> ParseFolders(const std::string& json) {
  if (json.empty()) {
    base::DictValue data;
    data.Set("version", 1);
    data.Set("items", base::ListValue());
    return data;
  }
  auto data = base::JSONReader::ReadDict(json, base::JSON_PARSE_RFC);
  if (!data) {
    return std::nullopt;
  }
  if (data->FindInt("version") == 2) {
    const auto* windows = data->FindList("windows");
    if (!windows) {
      return std::nullopt;
    }
    base::ListValue combined;
    std::map<std::string, size_t> folder_positions;
    std::set<std::string> window_ids;
    for (const auto& window : *windows) {
      const auto* snapshot = window.GetIfDict();
      const auto* id = snapshot ? snapshot->FindString("id") : nullptr;
      const auto* snapshot_json =
          snapshot ? snapshot->FindString("data") : nullptr;
      auto contents = snapshot_json
                          ? base::JSONReader::ReadDict(*snapshot_json,
                                                      base::JSON_PARSE_RFC)
                          : std::nullopt;
      const auto* items = contents ? contents->FindList("items") : nullptr;
      if (!id || id->empty() || !window_ids.insert(*id).second || !contents ||
          contents->FindInt("version") != 1 || !items ||
          !ValidItems(*items, true)) {
        return std::nullopt;
      }
      for (const auto& value : *items) {
        const auto& item = value.GetDict();
        if (*item.FindString("type") == "folder") {
          const auto [position, inserted] = folder_positions.emplace(
              *item.FindString("id"), combined.size());
          if (!inserted) {
            auto* children =
                combined[position->second].GetDict().FindList("children");
            for (const auto& child : *item.FindList("children")) {
              children->Append(child.Clone());
            }
            continue;
          }
        }
        combined.Append(item.Clone());
      }
    }
    data->clear();
    data->Set("version", 1);
    data->Set("items", std::move(combined));
  }
  if (data->FindInt("version") != 1) {
    return std::nullopt;
  }
  const auto* items = data->FindList("items");
  if (!items || !ValidItems(*items, true)) {
    return std::nullopt;
  }
  return data;
}

using ItemMap = std::map<std::string, const base::DictValue*>;

ItemMap IndexItems(const base::ListValue& items,
                   std::vector<std::string>& order) {
  ItemMap result;
  std::map<std::string, int> occurrences;
  for (const base::Value& value : items) {
    const auto& item = value.GetDict();
    std::string key;
    if (*item.FindString("type") == "folder") {
      key = "folder:" + *item.FindString("id");
    } else if (const auto* id = item.FindString("tabId")) {
      key = "tab:" + *id;
    } else {
      // Legacy files have no identities. Keep duplicate URL/title occurrences.
      key = "legacy:" + *item.FindString("url") + '\0' +
            *item.FindString("title");
    }
    key += '\0' + base::NumberToString(occurrences[key]++);
    order.push_back(key);
    result.emplace(key, &item);
  }
  return result;
}

base::ListValue MergeItems(const base::ListValue& before,
                           const base::ListValue& after,
                           const base::ListValue& current) {
  std::vector<std::string> before_order, after_order, current_order;
  const auto old_items = IndexItems(before, before_order);
  const auto new_items = IndexItems(after, after_order);
  const auto current_items = IndexItems(current, current_order);
  std::map<std::string, base::DictValue> merged;
  for (const auto& [id, item] : current_items) {
    if (!old_items.contains(id) || new_items.contains(id)) {
      merged.emplace(id, item->Clone());
    }
  }
  for (const auto& [id, item] : new_items) {
    auto old = old_items.find(id);
    if (old == old_items.end()) {
      merged.insert_or_assign(id, item->Clone());
      continue;
    }
    if (*old->second == *item || !merged.contains(id)) {
      // Unchanged stale snapshots cannot undo another window's edit/deletion.
      continue;
    }
    auto& target = merged.at(id);
    if (*item->FindString("type") == "folder") {
      for (const char* field : {"name", "collapsed"}) {
        if (*old->second->Find(field) != *item->Find(field)) {
          target.Set(field, item->Find(field)->Clone());
        }
      }
      target.Set("children", MergeItems(*old->second->FindList("children"),
                                       *item->FindList("children"),
                                       *target.FindList("children")));
    } else {
      target = item->Clone();
    }
  }
  // Only submit ordering when the caller changed it. Preserve newer entries
  // unknown to that caller, including folders created in another window.
  auto order = before_order == after_order ? current_order : after_order;
  order.insert(order.end(), current_order.begin(), current_order.end());
  base::ListValue result;
  for (const auto& id : order) {
    auto it = merged.find(id);
    if (it != merged.end()) {
      result.Append(std::move(it->second));
      merged.erase(it);
    }
  }
  return result;
}

struct TabLocation {
  std::string folder_id;
  raw_ptr<const base::DictValue> tab;
};
using TabLocations = std::map<std::string, TabLocation>;

void IndexTabLocations(const base::ListValue& items,
                       const std::string& folder_id,
                       TabLocations& locations,
                       std::vector<std::string>& order) {
  for (const auto& value : items) {
    const auto& item = value.GetDict();
    if (*item.FindString("type") == "folder") {
      IndexTabLocations(*item.FindList("children"), *item.FindString("id"),
                        locations, order);
    } else if (const auto* id = item.FindString("tabId")) {
      locations.emplace(*id, TabLocation{folder_id, &item});
      order.push_back(*id);
    }
  }
}

base::ListValue FilterTabLocations(const base::ListValue& items,
                                   const std::string& folder_id,
                                   const TabLocations& locations,
                                   std::set<std::string>& seen) {
  base::ListValue result;
  for (const auto& value : items) {
    const auto& item = value.GetDict();
    if (*item.FindString("type") == "folder") {
      auto folder = item.Clone();
      folder.Set("children",
                 FilterTabLocations(*item.FindList("children"),
                                    *item.FindString("id"), locations, seen));
      result.Append(std::move(folder));
    } else if (const auto* id = item.FindString("tabId")) {
      const auto location = locations.find(*id);
      if (location != locations.end() &&
          location->second.folder_id == folder_id && seen.insert(*id).second) {
        result.Append(location->second.tab->Clone());
      }
    } else {
      result.Append(item.Clone());
    }
  }
  return result;
}

base::ListValue MergeTabLocations(const base::ListValue& before,
                                  const base::ListValue& after,
                                  const base::ListValue& current) {
  auto merged = MergeItems(before, after, current);
  TabLocations old_tabs, new_tabs, locations;
  std::vector<std::string> unused_order, order;
  IndexTabLocations(before, "", old_tabs, unused_order);
  IndexTabLocations(current, "", locations, order);
  IndexTabLocations(after, "", new_tabs, order);
  for (const auto& [id, old] : old_tabs) {
    if (!new_tabs.contains(id)) {
      locations.erase(id);
    }
  }
  for (const auto& [id, next] : new_tabs) {
    const auto old = old_tabs.find(id);
    if (old == old_tabs.end()) {
      locations.insert_or_assign(id, next);
    } else if (auto target = locations.find(id); target != locations.end()) {
      // Membership is global: an explicit move wins across sibling lists.
      // A stale edit cannot resurrect a tab already deleted from the file.
      if (old->second.folder_id != next.folder_id) {
        target->second.folder_id = next.folder_id;
      }
      if (*old->second.tab != *next.tab) {
        target->second.tab = next.tab;
      }
    }
  }
  std::set<std::string> folder_ids;
  for (const auto& value : merged) {
    const auto& item = value.GetDict();
    if (*item.FindString("type") == "folder") {
      folder_ids.insert(*item.FindString("id"));
    }
  }
  for (auto& [id, location] : locations) {
    if (!folder_ids.contains(location.folder_id)) {
      // Unfoldering also releases children added by another window meanwhile.
      location.folder_id.clear();
    }
  }
  std::set<std::string> seen;
  merged = FilterTabLocations(merged, "", locations, seen);
  for (const auto& id : order) {
    const auto location = locations.find(id);
    if (location == locations.end() || !seen.insert(id).second) {
      continue;
    }
    const auto& [folder_id, tab] = location->second;
    if (folder_id.empty()) {
      merged.Append(tab->Clone());
    } else {
      // ponytail: linear lookup for missing refs in small folder files; index
      // folder positions if profiles grow enough to make this scan measurable.
      for (auto& value : merged) {
        auto& item = value.GetDict();
        if (*item.FindString("type") == "folder" &&
            *item.FindString("id") == folder_id) {
          item.FindList("children")->Append(tab->Clone());
          break;
        }
      }
    }
  }
  return merged;
}

}  // namespace

scoped_refptr<base::SequencedTaskRunner> GetFolderFileTaskRunner() {
  static base::NoDestructor<scoped_refptr<base::SequencedTaskRunner>> runner(
      base::ThreadPool::CreateSequencedTaskRunner(
          {base::MayBlock(), base::TaskPriority::USER_VISIBLE,
           base::TaskShutdownBehavior::BLOCK_SHUTDOWN}));
  return *runner;
}

std::optional<std::string> MergeFolderData(const std::string& base_json,
                                          const std::string& json,
                                          const std::string& current_json) {
  auto before = ParseFolders(base_json);
  auto after = ParseFolders(json);
  auto current = ParseFolders(current_json);
  if (!before || !after || !current) {
    return std::nullopt;
  }
  current->Set("items", MergeTabLocations(*before->FindList("items"),
                                         *after->FindList("items"),
                                         *current->FindList("items")));
  return base::WriteJson(*current);
}

std::optional<std::string> ReadFolderFile(const base::FilePath& path) {
  base::AutoLock lock(FolderFileLock());
  auto contents = ReadFile(path);
  if (!contents || contents->empty()) {
    return contents;
  }
  // Normalize legacy window snapshots without rewriting anything on load.
  return MergeFolderData("", "", *contents);
}

bool UpdateFolderFile(const base::FilePath& path,
                      const std::string& base_json,
                      const std::string& json) {
  base::AutoLock lock(FolderFileLock());
  auto contents = ReadFile(path);
  if (!contents) {
    return false;
  }
  auto merged = MergeFolderData(base_json, json, *contents);
  return merged && WritePinnedTabsFileAtomically(path, *merged);
}

}  // namespace dao
