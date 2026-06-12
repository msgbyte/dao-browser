// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/dao_pref_names.h"

#include "components/pref_registry/pref_registry_syncable.h"

namespace dao::prefs {

void RegisterProfilePrefs(user_prefs::PrefRegistrySyncable* registry) {
  registry->RegisterBooleanPref(kDaoAgentMemoryEnabled, false);
  registry->RegisterDictionaryPref(kDaoSplitLayout);
  registry->RegisterBooleanPref(kDaoWelcomeShown, false);
  registry->RegisterBooleanPref(kDaoDreamEnabled, false);
  registry->RegisterBooleanPref(kDaoDreamDebug, false);
}

}  // namespace dao::prefs
