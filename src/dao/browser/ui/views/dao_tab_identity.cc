// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/ui/views/dao_tab_identity.h"

#include <map>
#include <memory>
#include <string>
#include <utility>

#include "base/supports_user_data.h"
#include "base/uuid.h"
#include "content/public/browser/web_contents.h"

namespace dao {
namespace {

const char kDaoSidebarTabIdentityKey = 0;

class DaoSidebarTabIdentityData : public base::SupportsUserData::Data {
 public:
  explicit DaoSidebarTabIdentityData(std::string id) : id_(std::move(id)) {}

  const std::string& id() const { return id_; }

 private:
  std::string id_;
};

}  // namespace

std::string GetSidebarTabId(content::WebContents* contents) {
  if (!contents) {
    return std::string();
  }

  auto* identity = static_cast<DaoSidebarTabIdentityData*>(
      contents->GetUserData(&kDaoSidebarTabIdentityKey));
  if (!identity) {
    contents->SetUserData(&kDaoSidebarTabIdentityKey,
                          std::make_unique<DaoSidebarTabIdentityData>(
                              base::Uuid::GenerateRandomV4()
                                  .AsLowercaseString()));
    identity = static_cast<DaoSidebarTabIdentityData*>(
        contents->GetUserData(&kDaoSidebarTabIdentityKey));
  }

  return identity->id();
}

void SetSidebarTabId(content::WebContents* contents, const std::string& id) {
  if (!contents || id.empty()) {
    return;
  }
  contents->SetUserData(
      &kDaoSidebarTabIdentityKey,
      std::make_unique<DaoSidebarTabIdentityData>(id));
}

void CopySidebarTabId(content::WebContents* old_contents,
                      content::WebContents* new_contents) {
  if (!old_contents || !new_contents) {
    return;
  }

  auto* identity = static_cast<DaoSidebarTabIdentityData*>(
      old_contents->GetUserData(&kDaoSidebarTabIdentityKey));
  if (identity) {
    SetSidebarTabId(new_contents, identity->id());
  }
}

void PopulateSidebarTabIdentityExtraData(
    content::WebContents* contents,
    std::map<std::string, std::string>* extra_data) {
  if (!contents || !extra_data) {
    return;
  }
  (*extra_data)[kSidebarTabIdentitySessionKey] = GetSidebarTabId(contents);
}

void RestoreSidebarTabIdentityFromExtraData(
    content::WebContents* contents,
    const std::map<std::string, std::string>& extra_data) {
  auto it = extra_data.find(kSidebarTabIdentitySessionKey);
  if (it != extra_data.end()) {
    SetSidebarTabId(contents, it->second);
  }
}

}  // namespace dao
