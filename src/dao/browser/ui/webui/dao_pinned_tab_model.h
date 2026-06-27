// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_UI_WEBUI_DAO_PINNED_TAB_MODEL_H_
#define DAO_BROWSER_UI_WEBUI_DAO_PINNED_TAB_MODEL_H_

#include <cstddef>
#include <string>
#include <vector>

#include "base/time/time.h"

namespace dao {

struct DaoPinnedTabItem {
  DaoPinnedTabItem();
  DaoPinnedTabItem(const DaoPinnedTabItem&);
  DaoPinnedTabItem& operator=(const DaoPinnedTabItem&);
  ~DaoPinnedTabItem();

  std::string id;
  std::string title;
  std::string url;
  std::string favicon_url;
  std::string tab_id;
  base::Time created_at;
  base::Time updated_at;
};

class DaoPinnedTabModel {
 public:
  DaoPinnedTabModel();
  DaoPinnedTabModel(const DaoPinnedTabModel&) = delete;
  DaoPinnedTabModel& operator=(const DaoPinnedTabModel&) = delete;
  ~DaoPinnedTabModel();

  bool LoadFromJson(const std::string& json);
  std::string ToJson() const;

  const std::vector<DaoPinnedTabItem>& items() const { return items_; }

  DaoPinnedTabItem* FindById(const std::string& id);
  const DaoPinnedTabItem* FindById(const std::string& id) const;
  DaoPinnedTabItem* FindByTabId(const std::string& tab_id);
  const DaoPinnedTabItem* FindByTabId(const std::string& tab_id) const;
  DaoPinnedTabItem* FindByUrl(const std::string& url);
  const DaoPinnedTabItem* FindByUrl(const std::string& url) const;

  DaoPinnedTabItem& AddOrUpdate(const std::string& title,
                                const std::string& url,
                                const std::string& favicon_url,
                                const std::string& tab_id = std::string());
  bool RemoveById(const std::string& id);
  bool Move(const std::string& id, size_t to_index);

 private:
  std::vector<DaoPinnedTabItem> items_;
};

}  // namespace dao

#endif  // DAO_BROWSER_UI_WEBUI_DAO_PINNED_TAB_MODEL_H_
