// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_UI_VIEWS_DAO_EXTENSION_ACTION_ICON_H_
#define DAO_BROWSER_UI_VIEWS_DAO_EXTENSION_ACTION_ICON_H_

#include "ui/gfx/image/image_skia.h"

namespace content {
class WebContents;
}

namespace extensions {
class ExtensionAction;
class ExtensionActionIconFactory;
}  // namespace extensions

namespace gfx {
class Size;
}

namespace dao {

// Returns the current tab's extension action icon with its badge composited
// using Chromium's native extension badge renderer.
gfx::ImageSkia CreateExtensionActionIconWithBadge(
    extensions::ExtensionActionIconFactory& icon_factory,
    extensions::ExtensionAction& action,
    content::WebContents* web_contents,
    const gfx::Size& size);

}  // namespace dao

#endif  // DAO_BROWSER_UI_VIEWS_DAO_EXTENSION_ACTION_ICON_H_
