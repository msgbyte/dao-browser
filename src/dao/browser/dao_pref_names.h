// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_DAO_PREF_NAMES_H_
#define DAO_BROWSER_DAO_PREF_NAMES_H_

class Profile;

namespace blink::web_pref {
struct WebPreferences;
}

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

// Dictionary pref storing the most recent Document PiP outer window bounds
// per origin.
inline constexpr char kDaoPipWindowBoundsByOrigin[] =
    "dao.pip_window_bounds_by_origin";

// Boolean pref that controls whether configured sites use Dao's custom
// Document Picture-in-Picture window. When false, supported sites use the
// original browser Picture-in-Picture path.
inline constexpr char kDaoEnhancedPipEnabled[] = "dao.enhanced_pip_enabled";

// Boolean pref that tracks whether the welcome page has been shown.
// When false (the default), dao://welcome is opened on first launch.
inline constexpr char kDaoWelcomeShown[] = "dao.welcome_shown";

// Boolean pref that controls whether external links open in Little Dao.
// When false, external links use the regular full browser window.
inline constexpr char kDaoLittleDaoEnabled[] = "dao.little_dao_enabled";

// Boolean pref that remembers whether Dao should use Chromium's Auto Dark Mode
// pipeline for web contents. It only takes effect while the system appearance
// is dark.
inline constexpr char kDaoForceDarkModeEnabled[] =
    "dao.force_dark_mode_enabled";

// Boolean pref that enables the richer Arc-style command bar suggestion
// pipeline. Kept off by default while the ranking model is still maturing.
inline constexpr char kDaoEnhancedCommandBarSuggestionsEnabled[] =
    "dao.enhanced_command_bar_suggestions_enabled";

// Boolean pref that controls whether the command bar surfaces the "Ask AI"
// suggestion. When true (the default), Ask AI is offered in both default and
// enhanced suggestion modes; when false it is never shown.
inline constexpr char kDaoAskAiEnabled[] = "dao.ask_ai_enabled";

// Dictionary pref storing the most recent Little Dao outer window bounds.
// The key keeps its historical "window_size" name for compatibility with
// profiles that only stored width and height.
inline constexpr char kDaoLittleDaoWindowSize[] =
    "dao.little_dao_window_size";

// Boolean pref that controls the Dream Analysis system. When false (the
// default), the dream scheduler never fires and no browsing data is sent
// to the LLM provider. Requires kDaoAgentMemoryEnabled to also be true.
inline constexpr char kDaoDreamEnabled[] = "dao.dream_enabled";

// Boolean pref that controls the additional weekly Dream report. It is
// independently opt-in and requires both Agent memory and Dream Analysis.
inline constexpr char kDaoDreamWeeklyEnabled[] =
    "dao.dream_weekly_enabled";

// Boolean pref for dream debug mode. When true, each dream run persists
// the full material pack JSON (the exact LLM input) into the report row
// so the user can inspect what was summarized.
inline constexpr char kDaoDreamDebug[] = "dao.dream_debug";

// List pref storing normalized domains excluded from Dream Analysis material
// collection. A value excludes the domain and its subdomains.
inline constexpr char kDaoDreamExcludedDomains[] =
    "dao.dream_excluded_domains";

}  // namespace dao::prefs

namespace dao {

bool IsSystemDarkMode();
bool IsForceDarkModeUserEnabled(Profile* profile);
bool IsForceDarkModeAvailable();
bool IsForceDarkModeEffective(Profile* profile);

void SetForceDarkModeUserEnabled(Profile* profile, bool enabled);
void ApplyForceDarkModePreferences(
    Profile* profile,
    blink::web_pref::WebPreferences* web_preferences);
void NotifyForceDarkModeChanged(Profile* profile);

}  // namespace dao

#endif  // DAO_BROWSER_DAO_PREF_NAMES_H_
