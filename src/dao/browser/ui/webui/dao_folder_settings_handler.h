// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_UI_WEBUI_DAO_FOLDER_SETTINGS_HANDLER_H_
#define DAO_BROWSER_UI_WEBUI_DAO_FOLDER_SETTINGS_HANDLER_H_

#include <optional>
#include <string>

#include "base/memory/weak_ptr.h"
#include "base/values.h"
#include "content/public/browser/web_ui_message_handler.h"

class Profile;

namespace dao {

class DaoFolderSettingsHandler : public content::WebUIMessageHandler {
 public:
  DaoFolderSettingsHandler();
  DaoFolderSettingsHandler(const DaoFolderSettingsHandler&) = delete;
  DaoFolderSettingsHandler& operator=(const DaoFolderSettingsHandler&) = delete;
  ~DaoFolderSettingsHandler() override;

  static void NotifyFolderDataChanged(Profile* profile);

  void RegisterMessages() override;
  void OnJavascriptAllowed() override;
  void OnJavascriptDisallowed() override;

 private:
  void HandleGetCategories(const base::ListValue& args);
  void HandleEditCategory(const base::ListValue& args);
  void OnFoldersLoaded(std::string callback_id,
                       bool edited,
                       std::optional<std::string> contents);

  base::WeakPtrFactory<DaoFolderSettingsHandler> weak_factory_{this};
};

}  // namespace dao

#endif  // DAO_BROWSER_UI_WEBUI_DAO_FOLDER_SETTINGS_HANDLER_H_
