// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_UI_VIEWS_DAO_TAB_IDENTITY_H_
#define DAO_BROWSER_UI_VIEWS_DAO_TAB_IDENTITY_H_

#include <map>
#include <string>
#include <vector>

namespace content {
class WebContents;
}  // namespace content

namespace dao {

inline constexpr char kSidebarTabIdentitySessionKey[] = "dao.sidebar_tab_id";

std::string GetOrCreateSidebarTabId(content::WebContents* contents);
std::string GetSidebarTabId(content::WebContents* contents);
void SetSidebarTabId(content::WebContents* contents, const std::string& id);
void RepairDuplicateSidebarTabIds(
    const std::vector<content::WebContents*>& contents);
void CopySidebarTabId(content::WebContents* old_contents,
                      content::WebContents* new_contents);
void PopulateSidebarTabIdentityExtraData(
    content::WebContents* contents,
    std::map<std::string, std::string>* extra_data);
void RestoreSidebarTabIdentityFromExtraData(
    content::WebContents* contents,
    const std::map<std::string, std::string>& extra_data);

}  // namespace dao

#endif  // DAO_BROWSER_UI_VIEWS_DAO_TAB_IDENTITY_H_
