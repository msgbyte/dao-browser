// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/ui/views/dao_extension_action_icon.h"

#include <memory>
#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/extensions/extension_tab_util.h"
#include "chrome/browser/ui/extensions/icon_with_badge_image_source.h"
#include "content/public/browser/web_contents.h"
#include "extensions/browser/extension_action.h"
#include "extensions/browser/extension_action_icon_factory.h"
#include "ui/color/color_provider_manager.h"
#include "ui/gfx/geometry/size.h"
#include "ui/native_theme/native_theme.h"

namespace dao {

gfx::ImageSkia CreateExtensionActionIconWithBadge(
    extensions::ExtensionActionIconFactory& icon_factory,
    extensions::ExtensionAction& action,
    content::WebContents* web_contents,
    const gfx::Size& size) {
  auto get_color_provider = base::BindRepeating(
      [](base::WeakPtr<content::WebContents> weak_web_contents) {
        return weak_web_contents
                   ? &weak_web_contents->GetColorProvider()
                   : ui::ColorProviderManager::Get().GetColorProviderFor(
                         ui::NativeTheme::GetInstanceForNativeUi()
                             ->GetColorProviderKey(nullptr));
      },
      web_contents ? web_contents->GetWeakPtr()
                   : base::WeakPtr<content::WebContents>());
  int tab_id = extensions::ExtensionAction::kDefaultTabId;
  if (web_contents) {
    tab_id = extensions::ExtensionTabUtil::GetTabId(web_contents);
  }
  const std::string badge_text = action.GetDisplayBadgeText(tab_id);

  auto image_source = std::make_unique<IconWithBadgeImageSource>(
      size, std::move(get_color_provider));

  image_source->SetIcon(icon_factory.GetIcon(tab_id));

  if (!badge_text.empty()) {
    image_source->SetBadge(std::make_unique<IconWithBadgeImageSource::Badge>(
        badge_text, action.GetBadgeTextColor(tab_id),
        action.GetBadgeBackgroundColor(tab_id)));
  }

  return gfx::ImageSkia(std::move(image_source), size);
}

}  // namespace dao
