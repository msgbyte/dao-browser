// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/dao_pref_names.h"

#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface_iterator.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/pref_registry/pref_registry_syncable.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/web_contents.h"
#include "third_party/blink/public/common/web_preferences/web_preferences.h"
#include "third_party/blink/public/mojom/css/preferred_color_scheme.mojom.h"
#include "ui/native_theme/native_theme.h"

namespace dao::prefs {

void RegisterProfilePrefs(user_prefs::PrefRegistrySyncable* registry) {
  registry->RegisterBooleanPref(kDaoAgentMemoryEnabled, false);
  registry->RegisterDictionaryPref(kDaoSplitLayout);
  registry->RegisterDictionaryPref(kDaoPipWindowBoundsByOrigin);
  registry->RegisterBooleanPref(kDaoEnhancedPipEnabled, true);
  registry->RegisterBooleanPref(kDaoWelcomeShown, false);
  registry->RegisterBooleanPref(kDaoLittleDaoEnabled, true);
  registry->RegisterBooleanPref(kDaoForceDarkModeEnabled, false);
  registry->RegisterBooleanPref(kDaoEnhancedCommandBarSuggestionsEnabled,
                                false);
  registry->RegisterDictionaryPref(kDaoLittleDaoWindowSize);
  registry->RegisterBooleanPref(kDaoDreamEnabled, false);
  registry->RegisterBooleanPref(kDaoDreamDebug, false);
  registry->RegisterListPref(kDaoDreamExcludedDomains);
}

}  // namespace dao::prefs

namespace dao {

namespace {

Profile* GetStorageProfile(Profile* profile) {
  return profile ? profile->GetOriginalProfile() : nullptr;
}

bool UsesSameStorageProfile(Profile* lhs, Profile* rhs) {
  return GetStorageProfile(lhs) == GetStorageProfile(rhs);
}

}  // namespace

bool IsSystemDarkMode() {
  ui::NativeTheme* theme = ui::NativeTheme::GetInstanceForNativeUi();
  return theme && theme->preferred_color_scheme() ==
                      ui::NativeTheme::PreferredColorScheme::kDark;
}

bool IsForceDarkModeUserEnabled(Profile* profile) {
  Profile* storage_profile = GetStorageProfile(profile);
  return storage_profile && storage_profile->GetPrefs()->GetBoolean(
                                prefs::kDaoForceDarkModeEnabled);
}

bool IsForceDarkModeAvailable() {
  return IsSystemDarkMode();
}

bool IsForceDarkModeEffective(Profile* profile) {
  return IsForceDarkModeAvailable() && IsForceDarkModeUserEnabled(profile);
}

void SetForceDarkModeUserEnabled(Profile* profile, bool enabled) {
  Profile* storage_profile = GetStorageProfile(profile);
  if (!storage_profile) {
    return;
  }
  storage_profile->GetPrefs()->SetBoolean(prefs::kDaoForceDarkModeEnabled,
                                          enabled);
  NotifyForceDarkModeChanged(storage_profile);
}

void ApplyForceDarkModePreferences(
    Profile* profile,
    blink::web_pref::WebPreferences* web_preferences) {
  if (!web_preferences) {
    return;
  }

  const bool force_dark_mode = IsForceDarkModeEffective(profile);
  web_preferences->force_dark_mode_enabled = force_dark_mode;
  if (!force_dark_mode) {
    return;
  }

  web_preferences->preferred_color_scheme =
      blink::mojom::PreferredColorScheme::kDark;
  web_preferences->preferred_root_scrollbar_color_scheme =
      blink::mojom::PreferredColorScheme::kDark;
}

void NotifyForceDarkModeChanged(Profile* profile) {
  Profile* storage_profile = GetStorageProfile(profile);
  if (!storage_profile) {
    return;
  }

  for (BrowserWindowInterface* browser_window :
       GetAllBrowserWindowInterfaces()) {
    Browser* browser =
        browser_window ? browser_window->GetBrowserForMigrationOnly() : nullptr;
    if (!browser ||
        !UsesSameStorageProfile(browser->profile(), storage_profile)) {
      continue;
    }

    TabStripModel* tab_strip_model = browser->tab_strip_model();
    if (!tab_strip_model) {
      continue;
    }

    for (int i = 0; i < tab_strip_model->count(); ++i) {
      content::WebContents* contents = tab_strip_model->GetWebContentsAt(i);
      if (contents) {
        contents->OnWebPreferencesChanged();
      }
    }
  }
}

}  // namespace dao
