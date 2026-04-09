// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_DAO_PREF_NAMES_H_
#define DAO_BROWSER_DAO_PREF_NAMES_H_

namespace user_prefs {
class PrefRegistrySyncable;
}

namespace dao::prefs {

void RegisterProfilePrefs(user_prefs::PrefRegistrySyncable* registry);

// Boolean pref that controls whether the agent memory system is enabled.
// When false (the default), the memory service is not created, the proactive
// engine does not run, and the agent sidebar WebUI is not loaded.
inline constexpr char kDaoAgentMemoryEnabled[] = "dao.agent_memory_enabled";

// Dictionary pref storing the split view tree layout per window.
// Keyed by window session ID, each entry holds a serialized tree.
inline constexpr char kDaoSplitLayout[] = "dao.split_layout";

// Boolean pref that tracks whether the welcome page has been shown.
// When false (the default), dao://welcome is opened on first launch.
inline constexpr char kDaoWelcomeShown[] = "dao.welcome_shown";

}  // namespace dao::prefs

#endif  // DAO_BROWSER_DAO_PREF_NAMES_H_
