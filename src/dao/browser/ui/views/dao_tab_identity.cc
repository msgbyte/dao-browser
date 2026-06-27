// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/ui/views/dao_tab_identity.h"

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

}  // namespace dao
