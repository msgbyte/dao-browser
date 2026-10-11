// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/ui/webui/dao_folder_settings_handler.h"

#include <set>
#include <utility>

#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/no_destructor.h"
#include "base/task/sequenced_task_runner.h"
#include "base/uuid.h"
#include "chrome/browser/profiles/profile.h"
#include "content/public/browser/web_ui.h"
#include "dao/browser/ui/webui/dao_folder_storage.h"
#include "dao/browser/ui/webui/dao_sidebar_ui.h"

namespace dao {
namespace {

std::set<DaoFolderSettingsHandler*>& SettingsHandlers() {
  static base::NoDestructor<std::set<DaoFolderSettingsHandler*>> handlers;
  return *handlers;
}

}  // namespace

DaoFolderSettingsHandler::DaoFolderSettingsHandler() = default;

DaoFolderSettingsHandler::~DaoFolderSettingsHandler() {
  SettingsHandlers().erase(this);
}

// static
void DaoFolderSettingsHandler::NotifyFolderDataChanged(Profile* profile) {
  for (auto* handler : SettingsHandlers()) {
    if (Profile::FromWebUI(handler->web_ui()) == profile) {
      handler->FireWebUIListener("dao-tab-categories-changed");
    }
  }
}

void DaoFolderSettingsHandler::RegisterMessages() {
  web_ui()->RegisterMessageCallback(
      "getDaoTabCategories",
      base::BindRepeating(&DaoFolderSettingsHandler::HandleGetCategories,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "editDaoTabCategory",
      base::BindRepeating(&DaoFolderSettingsHandler::HandleEditCategory,
                          base::Unretained(this)));
}

void DaoFolderSettingsHandler::OnJavascriptAllowed() {
  SettingsHandlers().insert(this);
}

void DaoFolderSettingsHandler::OnJavascriptDisallowed() {
  SettingsHandlers().erase(this);
  weak_factory_.InvalidateWeakPtrs();
}

void DaoFolderSettingsHandler::HandleGetCategories(const base::ListValue& args) {
  if (args.size() != 1 || !args[0].is_string()) {
    return;
  }
  AllowJavascript();
  Profile* profile = Profile::FromWebUI(web_ui());
  if (profile->IsOffTheRecord()) {
    OnFoldersLoaded(args[0].GetString(), false,
                    DaoSidebarUIHandler::ReadFolderData(profile));
    return;
  }
  GetFolderFileTaskRunner()->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(&ReadFolderFile,
                     profile->GetPath().AppendASCII("dao_folders.json")),
      base::BindOnce(&DaoFolderSettingsHandler::OnFoldersLoaded,
                     weak_factory_.GetWeakPtr(), args[0].GetString(), false));
}

void DaoFolderSettingsHandler::HandleEditCategory(const base::ListValue& args) {
  if (args.empty() || !args[0].is_string()) {
    return;
  }
  AllowJavascript();
  const std::string callback_id = args[0].GetString();
  if (args.size() != 4 || !args[1].is_string() || !args[2].is_string() ||
      !args[3].is_string()) {
    RejectJavascriptCallback(base::Value(callback_id), base::Value());
    return;
  }
  const std::string& operation = args[1].GetString();
  FolderEdit edit;
  if (operation == "add") {
    edit = FolderEdit::kAdd;
  } else if (operation == "rename") {
    edit = FolderEdit::kRename;
  } else if (operation == "remove") {
    edit = FolderEdit::kRemove;
  } else {
    RejectJavascriptCallback(base::Value(callback_id), base::Value());
    return;
  }
  const std::string id = edit == FolderEdit::kAdd
                             ? base::Uuid::GenerateRandomV4().AsLowercaseString()
                             : args[2].GetString();
  Profile* profile = Profile::FromWebUI(web_ui());
  if (profile->IsOffTheRecord()) {
    auto before = DaoSidebarUIHandler::ReadFolderData(profile);
    auto edited = before ? EditFolderData(*before, edit, id, args[3].GetString())
                         : std::nullopt;
    if (edited &&
        !DaoSidebarUIHandler::UpdateFolderData(profile, *before, *edited)) {
      edited.reset();
    }
    OnFoldersLoaded(callback_id, true, std::move(edited));
    return;
  }
  GetFolderFileTaskRunner()->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(&EditFolderFile,
                     profile->GetPath().AppendASCII("dao_folders.json"), edit,
                     id, args[3].GetString()),
      base::BindOnce(
          [](base::WeakPtr<DaoFolderSettingsHandler> self,
             base::WeakPtr<Profile> profile, std::string callback_id,
             std::optional<std::string> contents) {
            const bool saved = contents.has_value();
            if (self) {
              self->OnFoldersLoaded(callback_id, false, std::move(contents));
            }
            // A completed write still refreshes sidebars after settings closes.
            if (saved && profile) {
              DaoSidebarUIHandler::NotifyFolderDataChanged(profile.get());
            }
          },
          weak_factory_.GetWeakPtr(), profile->GetWeakPtr(), callback_id));
}

void DaoFolderSettingsHandler::OnFoldersLoaded(
    std::string callback_id,
    bool edited,
    std::optional<std::string> contents) {
  if (!IsJavascriptAllowed()) {
    return;
  }
  if (!contents) {
    RejectJavascriptCallback(base::Value(callback_id), base::Value());
    return;
  }
  base::ListValue categories;
  if (!contents->empty()) {
    auto data = base::JSONReader::ReadDict(*contents, base::JSON_PARSE_RFC);
    const auto* items = data ? data->FindList("items") : nullptr;
    if (!items) {
      RejectJavascriptCallback(base::Value(callback_id), base::Value());
      return;
    }
    for (const auto& value : *items) {
      const auto& item = value.GetDict();
      if (*item.FindString("type") == "folder") {
        base::DictValue category;
        category.Set("id", *item.FindString("id"));
        category.Set("name", *item.FindString("name"));
        categories.Append(std::move(category));
      }
    }
  }
  ResolveJavascriptCallback(base::Value(callback_id), categories);
  if (edited) {
    DaoSidebarUIHandler::NotifyFolderDataChanged(Profile::FromWebUI(web_ui()));
  }
}

}  // namespace dao
