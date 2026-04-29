// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_UI_VIEWS_DAO_CROSS_WINDOW_DRAG_H_
#define DAO_BROWSER_UI_VIEWS_DAO_CROSS_WINDOW_DRAG_H_

#include <string>

class Browser;

namespace gfx {
class Point;
}

namespace dao {

// Pasteboard/dataTransfer payload format used by the sidebar WebUI to
// encode a tab identity for cross-window drag-and-drop. Lives at this
// single definition site so that the source side (dao_tab_item.ts) and
// the drop handler (dao_native_util_mac.mm) stay in sync.
inline constexpr char kDaoTabDragPrefix[] = "dao-tab-drag:";

// Parse a "dao-tab-drag:<session_id>:<tab_index>" payload into components.
// Returns true on success; false if the prefix is missing, the body is
// malformed, or either integer fails to parse. On failure, the out
// parameters are left unchanged.
bool ParseDaoTabDragPayload(const std::string& payload,
                            int* source_session_id,
                            int* tab_index);

// Called from macOS DaoEventInterceptor's draggingUpdated: to refresh the
// native split-drop indicator overlay. |point_in_browser_view| is in the
// browser's BrowserView-local coordinates (top-left origin, Y down). No-op
// if the point is outside the split view's bounds or the browser doesn't
// have one.
void UpdateSplitDropIndicator(Browser* target_browser,
                              const gfx::Point& point_in_browser_view);

// Called from draggingExited:/draggingEnded: to clear the overlay.
void HideSplitDropIndicator(Browser* target_browser);

// Called from performDragOperation:. Tries to interpret |payload| as a
// split-drop on |target_browser|'s DaoSplitView at |point_in_browser_view|.
// Returns true if the drop was applied (split created, pane swapped, or
// tab activated). False means the caller should fall back to a sidebar
// tab move.
bool PerformSplitTabDrop(Browser* target_browser,
                         const gfx::Point& point_in_browser_view,
                         const std::string& payload);

// Execute a cross-window tab move.
//
// Given a target Browser and the source's session_id + tab_index, detaches
// the WebContents from the source browser's TabStripModel and inserts it
// into the target browser's TabStripModel at target_insert_index. Clamps
// target_insert_index into [0, target.tab_count]. Closes the source
// browser window when its tab strip becomes empty.
//
// Returns true on success. Returns false if:
//   - target_browser is null
//   - no browser with session_id source_session_id is found in the same
//     profile
//   - source is the same as target
//   - source_tab_index is out of range
//   - detach fails
bool ExecuteCrossWindowTabMove(Browser* target_browser,
                               int source_session_id,
                               int source_tab_index,
                               int target_insert_index);

}  // namespace dao

#endif  // DAO_BROWSER_UI_VIEWS_DAO_CROSS_WINDOW_DRAG_H_
