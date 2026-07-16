// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/ui/webui/dao_pinned_tab_model.h"

#include <algorithm>
#include <cstdint>
#include <ctime>
#include <optional>
#include <string_view>
#include <utility>

#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/string_number_conversions.h"
#include "base/uuid.h"
#include "base/values.h"

namespace dao {

namespace {

constexpr int kPinnedTabModelVersion = 2;

base::Time TimeFromUnixSeconds(int64_t seconds) {
  return base::Time::FromTimeT(static_cast<time_t>(seconds));
}

std::string TimeToUnixSecondsString(base::Time time) {
  return base::NumberToString(static_cast<int64_t>(time.ToTimeT()));
}

base::Time ParseTimeValue(const base::DictValue& dict, std::string_view key) {
  const base::Value* value = dict.Find(key);
  if (!value) {
    return base::Time();
  }

  if (const std::string* string_value = value->GetIfString()) {
    int64_t seconds = 0;
    if (base::StringToInt64(*string_value, &seconds)) {
      return TimeFromUnixSeconds(seconds);
    }
    return base::Time();
  }

  std::optional<int> int_value = dict.FindInt(key);
  if (int_value) {
    return TimeFromUnixSeconds(*int_value);
  }

  return base::Time();
}

std::optional<DaoPinnedTabItem> ParseItem(const base::Value& value) {
  const base::DictValue* dict = value.GetIfDict();
  if (!dict) {
    return std::nullopt;
  }

  const std::string* id = dict->FindString("id");
  const std::string* title = dict->FindString("title");
  const std::string* url = dict->FindString("url");
  const std::string* favicon_url = dict->FindString("faviconUrl");
  if (!id || !title || !url) {
    return std::nullopt;
  }

  DaoPinnedTabItem item;
  item.id = *id;
  item.title = *title;
  item.url = *url;
  item.favicon_url = favicon_url ? *favicon_url : std::string();
  const std::string* backing_tab_id = dict->FindString("backingTabId");
  item.backing_tab_id =
      backing_tab_id ? *backing_tab_id : std::string();
  item.state = DaoPinnedTabState::kReconciling;
  item.created_at = ParseTimeValue(*dict, "createdAt");
  item.updated_at = ParseTimeValue(*dict, "updatedAt");
  return item;
}

base::DictValue ItemToValue(const DaoPinnedTabItem& item) {
  base::DictValue dict;
  dict.Set("id", item.id);
  dict.Set("title", item.title);
  dict.Set("url", item.url);
  dict.Set("faviconUrl", item.favicon_url);
  if (!item.backing_tab_id.empty()) {
    dict.Set("backingTabId", item.backing_tab_id);
  }
  dict.Set("state", DaoPinnedTabStateToString(item.state));
  dict.Set("createdAt", TimeToUnixSecondsString(item.created_at));
  dict.Set("updatedAt", TimeToUnixSecondsString(item.updated_at));
  return dict;
}

}  // namespace

const char* DaoPinnedTabStateToString(DaoPinnedTabState state) {
  switch (state) {
    case DaoPinnedTabState::kOpen:
      return "open";
    case DaoPinnedTabState::kDormant:
      return "dormant";
    case DaoPinnedTabState::kReconciling:
      return "reconciling";
  }
  return "reconciling";
}

DaoPinnedTabItem::DaoPinnedTabItem() = default;

DaoPinnedTabItem::DaoPinnedTabItem(const DaoPinnedTabItem&) = default;

DaoPinnedTabItem& DaoPinnedTabItem::operator=(const DaoPinnedTabItem&) =
    default;

DaoPinnedTabItem::~DaoPinnedTabItem() = default;

DaoPinnedTabModel::DaoPinnedTabModel() = default;

DaoPinnedTabModel::~DaoPinnedTabModel() = default;

bool DaoPinnedTabModel::LoadFromJson(const std::string& json) {
  if (json.empty()) {
    items_.clear();
    return true;
  }

  std::optional<base::Value> root =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!root || !root->is_dict()) {
    return false;
  }

  std::vector<DaoPinnedTabItem> parsed_items;
  const base::DictValue& dict = root->GetDict();
  const base::ListValue* items = dict.FindList("items");
  if (!items) {
    return false;
  }

  for (const base::Value& value : *items) {
    std::optional<DaoPinnedTabItem> item = ParseItem(value);
    if (!item) {
      return false;
    }
    parsed_items.push_back(std::move(*item));
  }

  items_ = std::move(parsed_items);
  return true;
}

std::string DaoPinnedTabModel::ToJson() const {
  base::DictValue root;
  root.Set("version", kPinnedTabModelVersion);

  base::ListValue items;
  for (const DaoPinnedTabItem& item : items_) {
    items.Append(ItemToValue(item));
  }
  root.Set("items", std::move(items));

  std::string json;
  base::JSONWriter::Write(root, &json);
  return json;
}

DaoPinnedTabItem* DaoPinnedTabModel::FindById(const std::string& id) {
  auto it = std::find_if(items_.begin(), items_.end(),
                         [&id](const DaoPinnedTabItem& item) {
                           return item.id == id;
                         });
  return it == items_.end() ? nullptr : &*it;
}

const DaoPinnedTabItem* DaoPinnedTabModel::FindById(
    const std::string& id) const {
  auto it = std::find_if(items_.begin(), items_.end(),
                         [&id](const DaoPinnedTabItem& item) {
                           return item.id == id;
                         });
  return it == items_.end() ? nullptr : &*it;
}

DaoPinnedTabItem* DaoPinnedTabModel::FindByBackingTabId(
    const std::string& backing_tab_id) {
  if (backing_tab_id.empty()) {
    return nullptr;
  }
  auto it = std::find_if(items_.begin(), items_.end(),
                         [&backing_tab_id](const DaoPinnedTabItem& item) {
                           return item.backing_tab_id == backing_tab_id;
                         });
  return it == items_.end() ? nullptr : &*it;
}

const DaoPinnedTabItem* DaoPinnedTabModel::FindByBackingTabId(
    const std::string& backing_tab_id) const {
  if (backing_tab_id.empty()) {
    return nullptr;
  }
  auto it = std::find_if(items_.begin(), items_.end(),
                         [&backing_tab_id](const DaoPinnedTabItem& item) {
                           return item.backing_tab_id == backing_tab_id;
                         });
  return it == items_.end() ? nullptr : &*it;
}

DaoPinnedTabItem* DaoPinnedTabModel::FindByUrl(const std::string& url) {
  auto it = std::find_if(items_.begin(), items_.end(),
                         [&url](const DaoPinnedTabItem& item) {
                           return item.url == url;
                         });
  return it == items_.end() ? nullptr : &*it;
}

const DaoPinnedTabItem* DaoPinnedTabModel::FindByUrl(
    const std::string& url) const {
  auto it = std::find_if(items_.begin(), items_.end(),
                         [&url](const DaoPinnedTabItem& item) {
                           return item.url == url;
                         });
  return it == items_.end() ? nullptr : &*it;
}

DaoPinnedTabItem& DaoPinnedTabModel::AddOrUpdate(
    const std::string& title,
    const std::string& url,
    const std::string& favicon_url,
    const std::string& backing_tab_id) {
  const base::Time now = base::Time::Now();

  DaoPinnedTabItem* item = FindByBackingTabId(backing_tab_id);
  if (item) {
    item->title = title;
    item->url = url;
    item->favicon_url = favicon_url;
    if (!backing_tab_id.empty()) {
      item->backing_tab_id = backing_tab_id;
    }
    item->state = DaoPinnedTabState::kOpen;
    item->updated_at = now;
    return *item;
  }

  DaoPinnedTabItem new_item;
  new_item.id = base::Uuid::GenerateRandomV4().AsLowercaseString();
  new_item.title = title;
  new_item.url = url;
  new_item.favicon_url = favicon_url;
  new_item.backing_tab_id = backing_tab_id;
  new_item.state = backing_tab_id.empty() ? DaoPinnedTabState::kReconciling
                                         : DaoPinnedTabState::kOpen;
  new_item.created_at = now;
  new_item.updated_at = now;

  items_.push_back(std::move(new_item));
  return items_.back();
}

bool DaoPinnedTabModel::Bind(const std::string& id,
                             const std::string& backing_tab_id) {
  if (backing_tab_id.empty()) {
    return false;
  }
  DaoPinnedTabItem* item = FindById(id);
  if (!item) {
    return false;
  }
  item->backing_tab_id = backing_tab_id;
  item->state = DaoPinnedTabState::kOpen;
  item->updated_at = base::Time::Now();
  return true;
}

bool DaoPinnedTabModel::SetState(const std::string& id,
                                 DaoPinnedTabState state) {
  DaoPinnedTabItem* item = FindById(id);
  if (!item || item->state == state) {
    return false;
  }
  item->state = state;
  item->updated_at = base::Time::Now();
  return true;
}

bool DaoPinnedTabModel::RemoveById(const std::string& id) {
  auto it = std::find_if(items_.begin(), items_.end(),
                         [&id](const DaoPinnedTabItem& item) {
                           return item.id == id;
                         });
  if (it == items_.end()) {
    return false;
  }

  items_.erase(it);
  return true;
}

bool DaoPinnedTabModel::Move(const std::string& id, size_t to_index) {
  auto it = std::find_if(items_.begin(), items_.end(),
                         [&id](const DaoPinnedTabItem& item) {
                           return item.id == id;
                         });
  if (it == items_.end()) {
    return false;
  }

  const size_t from_index = static_cast<size_t>(it - items_.begin());
  to_index = std::min(to_index, items_.size() - 1);
  if (from_index == to_index) {
    return false;
  }

  DaoPinnedTabItem item = std::move(*it);
  items_.erase(it);
  items_.insert(items_.begin() + to_index, std::move(item));
  return true;
}

}  // namespace dao
