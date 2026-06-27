// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_UI_VIEWS_DAO_TAB_IDENTITY_H_
#define DAO_BROWSER_UI_VIEWS_DAO_TAB_IDENTITY_H_

#include <string>

namespace content {
class WebContents;
}  // namespace content

namespace dao {

std::string GetSidebarTabId(content::WebContents* contents);

}  // namespace dao

#endif  // DAO_BROWSER_UI_VIEWS_DAO_TAB_IDENTITY_H_
