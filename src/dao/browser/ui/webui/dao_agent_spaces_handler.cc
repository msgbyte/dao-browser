// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/ui/webui/dao_agent_spaces_handler.h"

#include <algorithm>
#include <optional>
#include <utility>
#include <vector>

#include "base/base64.h"
#include "base/functional/bind.h"
#include "base/strings/strcat.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "content/public/browser/web_ui.h"
#include "dao/browser/agent_space/dao_agent_space.h"
#include "dao/browser/agent_space/dao_agent_space_frame_capturer.h"
#include "dao/browser/ui/views/sidebar/dao_tab_tooltip_view.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/skia/include/core/SkCanvas.h"
#include "third_party/skia/include/core/SkPaint.h"
#include "ui/gfx/codec/jpeg_codec.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/views/view.h"

namespace dao {
namespace {

// Hover previews match the 240px tooltip at 2x; the opened panel streams a
// larger frame at a higher rate since the user is actively watching.
constexpr gfx::Size kPreviewSize(480, 300);
constexpr int kPreviewFramesPerSecond = 4;
constexpr gfx::Size kStreamSize(640, 400);
constexpr int kStreamFramesPerSecond = 12;
constexpr int kStreamJpegQuality = 70;

// Copies a captured frame, which is only valid during the capture callback,
// and marks where the agent last pointed.
SkBitmap CopyWithCursor(const SkBitmap& frame,
                        const std::optional<gfx::PointF>& cursor) {
  SkBitmap copy;
  if (!copy.tryAllocPixels(
          SkImageInfo::MakeN32Premul(frame.width(), frame.height())) ||
      !frame.readPixels(copy.pixmap())) {
    return SkBitmap();
  }
  if (cursor) {
    SkCanvas canvas(copy);
    const float radius = std::max(5.0f, frame.width() / 80.0f);
    const SkPoint center = SkPoint::Make(cursor->x() * frame.width(),
                                         cursor->y() * frame.height());
    SkPaint ring;
    ring.setAntiAlias(true);
    ring.setColor(SK_ColorWHITE);
    canvas.drawCircle(center, radius + 2, ring);
    SkPaint dot;
    dot.setAntiAlias(true);
    dot.setColor(SkColorSetRGB(70, 120, 190));
    canvas.drawCircle(center, radius, dot);
  }
  copy.setImmutable();
  return copy;
}

}  // namespace

DaoAgentSpacesHandler::DaoAgentSpacesHandler() = default;

DaoAgentSpacesHandler::~DaoAgentSpacesHandler() {
  StopPreview();
  StopStream();
}

void DaoAgentSpacesHandler::SetBrowser(BrowserWindowInterface* browser) {
  if (browser_ == browser) {
    return;
  }
  StopPreview();
  StopStream();
  observation_.Reset();
  browser_ = browser;
  if (DaoAgentSpaceService* service = GetService()) {
    observation_.Observe(service);
  }
  if (IsJavascriptAllowed()) {
    PushState();
  }
}

void DaoAgentSpacesHandler::RegisterMessages() {
  web_ui()->RegisterMessageCallback(
      "getAgentSpaces",
      base::BindRepeating(&DaoAgentSpacesHandler::HandleGetAgentSpaces,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "agentSpaceAction",
      base::BindRepeating(&DaoAgentSpacesHandler::HandleAgentSpaceAction,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "showAgentSpacePreview",
      base::BindRepeating(&DaoAgentSpacesHandler::HandleShowAgentSpacePreview,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "hideAgentSpacePreview",
      base::BindRepeating(&DaoAgentSpacesHandler::HandleHideAgentSpacePreview,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "startAgentSpaceStream",
      base::BindRepeating(&DaoAgentSpacesHandler::HandleStartAgentSpaceStream,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "stopAgentSpaceStream",
      base::BindRepeating(&DaoAgentSpacesHandler::HandleStopAgentSpaceStream,
                          base::Unretained(this)));
}

void DaoAgentSpacesHandler::OnJavascriptAllowed() {
  PushState();
}

void DaoAgentSpacesHandler::OnJavascriptDisallowed() {
  StopPreview();
  StopStream();
}

void DaoAgentSpacesHandler::OnAgentSpacesChanged() {
  RestartCapturersIfNeeded();
  if (IsJavascriptAllowed()) {
    PushState();
  }
}

DaoAgentSpaceService* DaoAgentSpacesHandler::GetService() {
  if (!browser_) {
    return nullptr;
  }
  // A clean space's window has a temporary profile; its tasks belong to the
  // user's profile.
  Profile* profile = browser_->GetProfile();
  if (DaoAgentSpaceService::IsAgentSpaceBrowser(browser_)) {
    profile = profile->GetOriginalProfile();
  }
  return DaoAgentSpaceService::GetForProfile(profile);
}

base::DictValue DaoAgentSpacesHandler::BuildState() {
  base::DictValue state;
  DaoAgentSpaceService* service = GetService();
  state.Set("spaces", service ? service->SerializeSpaces() : base::ListValue());
  DaoAgentSpace* current = DaoAgentSpaceService::FindSpaceForBrowser(browser_);
  state.Set("currentSpaceId", current ? current->id() : std::string());
  return state;
}

void DaoAgentSpacesHandler::PushState() {
  if (!IsJavascriptAllowed()) {
    return;
  }
  FireWebUIListener("agentSpacesChanged", BuildState());
}

void DaoAgentSpacesHandler::StopPreview() {
  if (preview_capturer_) {
    // A rejected frame can stop its own capture callback; keep the object alive.
    preview_capturer_->Stop();
  }
  if (preview_id_ && browser_) {
    BrowserView* view = BrowserView::GetBrowserViewForBrowser(browser_);
    if (view && view->dao_tab_tooltip()) {
      view->dao_tab_tooltip()->HidePreview(preview_id_);
    }
  }
  preview_space_id_.clear();
  preview_id_ = 0;
}

void DaoAgentSpacesHandler::StopStream() {
  stream_capturer_.reset();
  stream_space_id_.clear();
}

void DaoAgentSpacesHandler::RestartCapturersIfNeeded() {
  DaoAgentSpaceService* service = GetService();
  DaoAgentSpace* preview_space =
      service ? service->FindSpace(preview_space_id_) : nullptr;
  if (!preview_space || preview_space->is_closed() ||
      !preview_space->GetActiveContents()) {
    StopPreview();
  }
  auto restart = [&](const std::string& space_id,
                     std::unique_ptr<DaoAgentSpaceFrameCapturer>& capturer,
                     gfx::Size size, int fps,
                     DaoAgentSpaceFrameCapturer::FrameCallback callback) {
    if (space_id.empty()) {
      return;
    }
    DaoAgentSpace* space = service ? service->FindSpace(space_id) : nullptr;
    content::WebContents* contents =
        space && !space->is_closed() ? space->GetActiveContents() : nullptr;
    if (!contents) {
      capturer.reset();
      return;
    }
    if (capturer && capturer->contents() == contents) {
      return;
    }
    capturer = std::make_unique<DaoAgentSpaceFrameCapturer>(
        contents, size, fps, std::move(callback));
    if (!capturer->Start()) {
      capturer.reset();
    }
  };
  restart(preview_space_id_, preview_capturer_, kPreviewSize,
          kPreviewFramesPerSecond,
          base::BindRepeating(&DaoAgentSpacesHandler::OnPreviewFrame,
                              weak_factory_.GetWeakPtr()));
  restart(stream_space_id_, stream_capturer_, kStreamSize,
          kStreamFramesPerSecond,
          base::BindRepeating(&DaoAgentSpacesHandler::OnStreamFrame,
                              weak_factory_.GetWeakPtr()));
}

void DaoAgentSpacesHandler::OnPreviewFrame(const SkBitmap& frame) {
  DaoAgentSpaceService* service = GetService();
  DaoAgentSpace* space =
      service ? service->FindSpace(preview_space_id_) : nullptr;
  BrowserView* view =
      browser_ ? BrowserView::GetBrowserViewForBrowser(browser_) : nullptr;
  if (!space || space->is_closed() || !view || !view->dao_tab_tooltip()) {
    StopPreview();
    return;
  }
  SkBitmap copy = CopyWithCursor(frame, space->cursor());
  if (copy.drawsNothing()) {
    return;
  }
  if (!view->dao_tab_tooltip()->UpdatePreview(
      preview_id_,
      base::UTF8ToUTF16(space->status_text().empty() ? space->name()
                                                     : space->status_text()),
      gfx::ImageSkia::CreateFrom1xBitmap(copy))) {
    StopPreview();
  }
}

void DaoAgentSpacesHandler::OnStreamFrame(const SkBitmap& frame) {
  if (!IsJavascriptAllowed()) {
    return;
  }
  DaoAgentSpaceService* service = GetService();
  DaoAgentSpace* space =
      service ? service->FindSpace(stream_space_id_) : nullptr;
  if (!space) {
    return;
  }
  // The panel draws the cursor itself so it can animate between frames.
  SkBitmap copy = CopyWithCursor(frame, std::nullopt);
  std::optional<std::vector<uint8_t>> jpeg =
      gfx::JPEGCodec::Encode(copy, kStreamJpegQuality);
  if (!jpeg) {
    return;
  }
  FireWebUIListener(
      "agentSpaceFrame",
      base::DictValue()
          .Set("spaceId", space->id())
          .Set("dataUrl", base::StrCat({"data:image/jpeg;base64,",
                                        base::Base64Encode(*jpeg)})));
}

void DaoAgentSpacesHandler::HandleGetAgentSpaces(const base::ListValue& args) {
  AllowJavascript();
  PushState();
}

void DaoAgentSpacesHandler::HandleAgentSpaceAction(
    const base::ListValue& args) {
  DaoAgentSpaceService* service = GetService();
  if (!service || args.size() < 2 || !args[0].is_string() ||
      !args[1].is_string()) {
    return;
  }
  const std::string& action = args[0].GetString();
  const std::string& space_id = args[1].GetString();
  const std::string tab_id =
      args.size() > 2 && args[2].is_string() ? args[2].GetString() : "";
  if (action == "show") {
    service->ShowWindow(space_id, tab_id);
  } else if (action == "hide") {
    service->HideWindow(space_id);
  } else if (action == "take_over") {
    service->TakeOver(space_id);
  } else if (action == "hand_back") {
    service->HandBack(space_id);
  } else if (action == "stop") {
    service->Stop(space_id);
  } else if (action == "dismiss") {
    service->Dismiss(space_id);
  }
}

void DaoAgentSpacesHandler::HandleShowAgentSpacePreview(
    const base::ListValue& args) {
  if (!browser_ || args.size() != 3 || !args[0].is_int() || !args[1].is_int() ||
      !args[2].is_string()) {
    return;
  }
  BrowserView* view = BrowserView::GetBrowserViewForBrowser(browser_);
  if (!view || !view->dao_tab_tooltip()) {
    return;
  }
  gfx::Point anchor(args[0].GetInt(), args[1].GetInt());
  views::View::ConvertPointFromScreen(view, &anchor);
  StopPreview();
  preview_capturer_.reset();
  preview_space_id_ = args[2].GetString();
  preview_id_ = view->dao_tab_tooltip()->BeginPreview(anchor);
  RestartCapturersIfNeeded();
}

void DaoAgentSpacesHandler::HandleHideAgentSpacePreview(
    const base::ListValue& args) {
  StopPreview();
}

void DaoAgentSpacesHandler::HandleStartAgentSpaceStream(
    const base::ListValue& args) {
  if (args.size() != 1 || !args[0].is_string()) {
    return;
  }
  AllowJavascript();
  if (stream_space_id_ != args[0].GetString()) {
    stream_capturer_.reset();
    stream_space_id_ = args[0].GetString();
  }
  RestartCapturersIfNeeded();
}

void DaoAgentSpacesHandler::HandleStopAgentSpaceStream(
    const base::ListValue& args) {
  StopStream();
}

}  // namespace dao
