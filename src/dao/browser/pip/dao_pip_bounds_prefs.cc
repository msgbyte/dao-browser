// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/pip/dao_pip_bounds_prefs.h"

#include <string>
#include <utility>

#include "base/strings/string_number_conversions.h"
#include "base/values.h"
#include "chrome/browser/profiles/profile.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/scoped_user_pref_update.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "dao/browser/dao_pref_names.h"
#include "ui/display/display.h"
#include "ui/display/screen.h"
#include "url/origin.h"
#include "url/scheme_host_port.h"

namespace dao {

namespace {

constexpr char kXKey[] = "x";
constexpr char kYKey[] = "y";
constexpr char kWidthKey[] = "width";
constexpr char kHeightKey[] = "height";
constexpr char kOpenerDisplayIdKey[] = "opener_display_id";
constexpr char kPipDisplayIdKey[] = "pip_display_id";
constexpr char kRequestedWidthKey[] = "requested_width";
constexpr char kRequestedHeightKey[] = "requested_height";

PrefService* GetValidPrefs(Profile* profile) {
  if (!profile || profile->IsOffTheRecord()) {
    return nullptr;
  }

  PrefService* prefs = profile->GetPrefs();
  if (!prefs ||
      !prefs->FindPreference(dao::prefs::kDaoPipWindowBoundsByOrigin)) {
    return nullptr;
  }

  return prefs;
}

std::optional<std::string> GetSerializedSiteOrigin(
    content::WebContents* web_contents) {
  if (!web_contents) {
    return std::nullopt;
  }

  content::RenderFrameHost* main_frame = web_contents->GetPrimaryMainFrame();
  if (!main_frame) {
    return std::nullopt;
  }

  const url::SchemeHostPort& site_origin =
      main_frame->GetLastCommittedOrigin().GetTupleOrPrecursorTupleIfOpaque();
  if (!site_origin.IsValid()) {
    return std::nullopt;
  }

  std::string serialized_origin = site_origin.Serialize();
  if (serialized_origin.empty()) {
    return std::nullopt;
  }

  return serialized_origin;
}

std::optional<int64_t> ReadDisplayId(const base::DictValue& entry,
                                     const char* key) {
  const std::string* stored_display_id = entry.FindString(key);
  if (!stored_display_id) {
    return std::nullopt;
  }

  int64_t display_id = display::kInvalidDisplayId;
  if (!base::StringToInt64(*stored_display_id, &display_id)) {
    return std::nullopt;
  }

  return display_id;
}

bool RequestedSizeMatches(const base::DictValue& entry,
                          std::optional<gfx::Size> requested_content_size) {
  const std::optional<int> requested_width = entry.FindInt(kRequestedWidthKey);
  const std::optional<int> requested_height =
      entry.FindInt(kRequestedHeightKey);

  if (!requested_content_size) {
    return true;
  }

  return requested_width == requested_content_size->width() &&
         requested_height == requested_content_size->height();
}

bool DisplayMatchesCurrentOpener(const base::DictValue& entry,
                                 const display::Display& opener_display) {
  const std::optional<int64_t> stored_opener_display_id =
      ReadDisplayId(entry, kOpenerDisplayIdKey);
  const std::optional<int64_t> stored_pip_display_id =
      ReadDisplayId(entry, kPipDisplayIdKey);
  if (!stored_opener_display_id && !stored_pip_display_id) {
    return false;
  }

  return (stored_opener_display_id &&
          *stored_opener_display_id == opener_display.id()) ||
         (stored_pip_display_id && *stored_pip_display_id == opener_display.id());
}

std::optional<gfx::Rect> ReadRectFromEntry(const base::DictValue& entry,
                                           const char* x_key,
                                           const char* y_key,
                                           const char* width_key,
                                           const char* height_key) {
  const std::optional<int> x = entry.FindInt(x_key);
  const std::optional<int> y = entry.FindInt(y_key);
  const std::optional<int> width = entry.FindInt(width_key);
  const std::optional<int> height = entry.FindInt(height_key);
  if (!x || !y || !width || !height || *width <= 0 || *height <= 0) {
    return std::nullopt;
  }

  gfx::Rect bounds(*x, *y, *width, *height);
  if (bounds.IsEmpty()) {
    return std::nullopt;
  }

  return bounds;
}

std::optional<gfx::Rect> ReadBoundsFromEntry(const base::DictValue& entry) {
  return ReadRectFromEntry(entry, kXKey, kYKey, kWidthKey, kHeightKey);
}

bool BoundsIntersectDisplayWorkArea(const gfx::Rect& bounds,
                                    const display::Display& display) {
  return !bounds.IsEmpty() && bounds.width() > 0 && bounds.height() > 0 &&
         bounds.Intersects(display.work_area());
}

bool BoundsIntersectCurrentScreenWorkArea(const gfx::Rect& bounds,
                                          const base::DictValue& entry) {
  display::Screen* screen = display::Screen::Get();
  if (!screen) {
    return false;
  }

  const std::optional<int64_t> stored_pip_display_id =
      ReadDisplayId(entry, kPipDisplayIdKey);
  display::Display stored_pip_display;
  if (stored_pip_display_id &&
      screen->GetDisplayWithDisplayId(*stored_pip_display_id,
                                      &stored_pip_display)) {
    return BoundsIntersectDisplayWorkArea(bounds, stored_pip_display);
  }

  for (const display::Display& display : screen->GetAllDisplays()) {
    if (BoundsIntersectDisplayWorkArea(bounds, display)) {
      return true;
    }
  }

  return false;
}

}  // namespace

std::optional<gfx::Rect> GetPersistedPipBoundsForSite(
    Profile* profile,
    content::WebContents* web_contents,
    const display::Display& opener_display,
    std::optional<gfx::Size> requested_content_size) {
  PrefService* prefs = GetValidPrefs(profile);
  const std::optional<std::string> serialized_origin =
      GetSerializedSiteOrigin(web_contents);
  if (!prefs || !serialized_origin) {
    return std::nullopt;
  }

  const base::DictValue* entry =
      prefs->GetDict(dao::prefs::kDaoPipWindowBoundsByOrigin)
          .FindDict(*serialized_origin);
  if (!entry || !RequestedSizeMatches(*entry, requested_content_size) ||
      !DisplayMatchesCurrentOpener(*entry, opener_display)) {
    return std::nullopt;
  }

  std::optional<gfx::Rect> bounds = ReadBoundsFromEntry(*entry);
  if (!bounds || (!bounds->Intersects(opener_display.work_area()) &&
                  !BoundsIntersectCurrentScreenWorkArea(*bounds, *entry))) {
    return std::nullopt;
  }

  return bounds;
}

void UpdatePersistedPipBoundsForSite(
    Profile* profile,
    content::WebContents* web_contents,
    const gfx::Rect& most_recent_bounds,
    const display::Display& opener_display,
    const display::Display& pip_display,
    std::optional<gfx::Size> requested_content_size) {
  PrefService* prefs = GetValidPrefs(profile);
  const std::optional<std::string> serialized_origin =
      GetSerializedSiteOrigin(web_contents);
  if (!prefs || !serialized_origin ||
      !BoundsIntersectDisplayWorkArea(most_recent_bounds, pip_display)) {
    return;
  }

  base::DictValue entry;
  entry.Set(kXKey, most_recent_bounds.x());
  entry.Set(kYKey, most_recent_bounds.y());
  entry.Set(kWidthKey, most_recent_bounds.width());
  entry.Set(kHeightKey, most_recent_bounds.height());
  entry.Set(kOpenerDisplayIdKey, base::NumberToString(opener_display.id()));
  entry.Set(kPipDisplayIdKey, base::NumberToString(pip_display.id()));
  if (requested_content_size) {
    entry.Set(kRequestedWidthKey, requested_content_size->width());
    entry.Set(kRequestedHeightKey, requested_content_size->height());
  }

  ScopedDictPrefUpdate update(prefs, dao::prefs::kDaoPipWindowBoundsByOrigin);
  update->Set(*serialized_origin, std::move(entry));
}

}  // namespace dao
