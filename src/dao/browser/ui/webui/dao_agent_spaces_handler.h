// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_UI_WEBUI_DAO_AGENT_SPACES_HANDLER_H_
#define DAO_BROWSER_UI_WEBUI_DAO_AGENT_SPACES_HANDLER_H_

#include <cstdint>
#include <memory>
#include <string>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "base/values.h"
#include "content/public/browser/web_ui_message_handler.h"
#include "dao/browser/agent_space/dao_agent_space_service.h"

class BrowserWindowInterface;
class SkBitmap;

namespace dao {

class DaoAgentSpaceFrameCapturer;

// Sidebar side of Agent Spaces. In user windows it lists running tasks and
// streams live previews while the user hovers or opens one; in a task window
// it drives the take over / hand back controls for that task.
class DaoAgentSpacesHandler : public content::WebUIMessageHandler,
                              public DaoAgentSpaceService::Observer {
 public:
  DaoAgentSpacesHandler();
  ~DaoAgentSpacesHandler() override;

  DaoAgentSpacesHandler(const DaoAgentSpacesHandler&) = delete;
  DaoAgentSpacesHandler& operator=(const DaoAgentSpacesHandler&) = delete;

  void SetBrowser(BrowserWindowInterface* browser);

  // content::WebUIMessageHandler:
  void RegisterMessages() override;
  void OnJavascriptAllowed() override;
  void OnJavascriptDisallowed() override;

  // DaoAgentSpaceService::Observer:
  void OnAgentSpacesChanged() override;

 private:
  DaoAgentSpaceService* GetService();
  base::DictValue BuildState();
  void PushState();
  void StopPreview();
  void StopStream();
  void RestartCapturersIfNeeded();
  void OnPreviewFrame(const SkBitmap& frame);
  void OnStreamFrame(const SkBitmap& frame);

  void HandleGetAgentSpaces(const base::ListValue& args);
  void HandleAgentSpaceAction(const base::ListValue& args);
  void HandleShowAgentSpacePreview(const base::ListValue& args);
  void HandleHideAgentSpacePreview(const base::ListValue& args);
  void HandleStartAgentSpaceStream(const base::ListValue& args);
  void HandleStopAgentSpaceStream(const base::ListValue& args);

  raw_ptr<BrowserWindowInterface> browser_ = nullptr;
  base::ScopedObservation<DaoAgentSpaceService, DaoAgentSpaceService::Observer>
      observation_{this};

  std::string preview_space_id_;
  uint64_t preview_id_ = 0;
  std::unique_ptr<DaoAgentSpaceFrameCapturer> preview_capturer_;

  std::string stream_space_id_;
  std::unique_ptr<DaoAgentSpaceFrameCapturer> stream_capturer_;

  base::WeakPtrFactory<DaoAgentSpacesHandler> weak_factory_{this};
};

}  // namespace dao

#endif  // DAO_BROWSER_UI_WEBUI_DAO_AGENT_SPACES_HANDLER_H_
