// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_PIP_DAO_PIP_BOUNDS_PREFS_H_
#define DAO_BROWSER_PIP_DAO_PIP_BOUNDS_PREFS_H_

#include <optional>

#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/size.h"

class Profile;

namespace content {
class WebContents;
}

namespace display {
class Display;
}

namespace dao {

std::optional<gfx::Rect> GetPersistedPipBoundsForSite(
    Profile* profile,
    content::WebContents* web_contents,
    const display::Display& opener_display,
    std::optional<gfx::Size> requested_content_size);

void UpdatePersistedPipBoundsForSite(
    Profile* profile,
    content::WebContents* web_contents,
    const gfx::Rect& most_recent_bounds,
    const display::Display& opener_display,
    const display::Display& pip_display,
    std::optional<gfx::Size> requested_content_size);

}  // namespace dao

#endif  // DAO_BROWSER_PIP_DAO_PIP_BOUNDS_PREFS_H_
