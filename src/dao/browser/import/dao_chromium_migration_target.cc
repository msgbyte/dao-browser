// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/import/dao_chromium_migration_target.h"

#include <algorithm>
#include <utility>

#include "base/check.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/utf_string_conversions.h"
#include "base/uuid.h"
#include "chrome/browser/bookmarks/bookmark_model_factory.h"
#include "chrome/browser/extensions/webstore_install_with_prompt.h"
#include "chrome/browser/history/history_service_factory.h"
#include "chrome/browser/password_manager/factories/profile_password_store_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/resource_coordinator/lifecycle_unit_state.mojom.h"
#include "chrome/browser/resource_coordinator/tab_lifecycle_unit_external.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_finder.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/profile_browser_collection.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/bookmarks/browser/bookmark_model.h"
#include "components/bookmarks/browser/bookmark_node.h"
#include "components/history/core/browser/history_service.h"
#include "components/history/core/browser/history_types.h"
#include "components/password_manager/core/browser/password_form.h"
#include "components/password_manager/core/browser/password_form_digest.h"
#include "components/password_manager/core/browser/password_store/actionable_error.h"
#include "components/password_manager/core/browser/password_store/password_store_interface.h"
#include "components/password_manager/core/common/password_manager_pref_names.h"
#include "components/prefs/pref_service.h"
#include "dao/browser/ui/views/dao_tab_identity.h"
#include "dao/browser/ui/webui/dao_pinned_tab_storage.h"
#include "dao/browser/ui/webui/dao_sidebar_ui.h"
#include "extensions/browser/disable_reason.h"
#include "extensions/browser/extension_registrar.h"
#include "extensions/browser/extension_registry.h"
#include "ui/base/base_window.h"
#include "ui/gfx/native_ui_types.h"

namespace dao::import {
namespace {

std::string JoinPath(const std::vector<std::u16string>& path) {
  std::string key;
  for (const std::u16string& component : path) {
    key.append(base::UTF16ToUTF8(component));
    key.push_back('\0');
  }
  return key;
}

std::pair<std::string, std::u16string> PasswordKey(const PasswordEntry& entry) {
  return {entry.signon_realm, entry.username};
}

}  // namespace

DaoChromiumMigrationTarget::DaoChromiumMigrationTarget(Profile* profile)
    : profile_(profile),
      bookmark_model_(BookmarkModelFactory::GetForBrowserContext(profile)) {}

DaoChromiumMigrationTarget::~DaoChromiumMigrationTarget() = default;

bool DaoChromiumMigrationTarget::HasBookmark(
    const BookmarkEntry& entry,
    const std::u16string& root_name) const {
  if (added_bookmarks_.contains(BookmarkKey(entry))) {
    return true;
  }
  if (!bookmark_model_ || !bookmark_model_->loaded()) {
    return false;
  }
  const bookmarks::BookmarkNode* bar =
      bookmark_model_->account_bookmark_bar_node()
          ? bookmark_model_->account_bookmark_bar_node()
          : bookmark_model_->bookmark_bar_node();
  const bookmarks::BookmarkNode* parent = nullptr;
  for (const auto& child : bar->children()) {
    if (child->is_folder() && child->GetTitle() == root_name) {
      parent = child.get();
      break;
    }
  }
  if (!parent) {
    return false;
  }
  for (const std::u16string& component : entry.path) {
    const bookmarks::BookmarkNode* next = nullptr;
    for (const auto& child : parent->children()) {
      if (child->is_folder() && child->GetTitle() == component) {
        next = child.get();
        break;
      }
    }
    if (!next) {
      return false;
    }
    parent = next;
  }
  for (const auto& child : parent->children()) {
    if (child->GetTitle() == entry.title &&
        child->is_folder() == entry.is_folder &&
        (entry.is_folder || child->url() == entry.url)) {
      return true;
    }
  }
  return false;
}

bool DaoChromiumMigrationTarget::AddBookmark(const BookmarkEntry& entry,
                                             const std::u16string& root_name) {
  if (!bookmark_model_ || !bookmark_model_->loaded()) {
    return false;
  }
  const bookmarks::BookmarkNode* parent =
      FindOrCreateBookmarkParent(entry, root_name);
  if (!parent) {
    return false;
  }
  const bookmarks::BookmarkNode* added = nullptr;
  if (entry.is_folder) {
    added = bookmark_model_->AddFolder(
        parent, parent->children().size(), entry.title, nullptr,
        entry.creation_time.is_null()
            ? std::nullopt
            : std::optional<base::Time>(entry.creation_time));
    std::vector<std::u16string> path = entry.path;
    path.push_back(entry.title);
    if (added) {
      bookmark_folders_[JoinPath(path)] = added;
    }
  } else {
    added = bookmark_model_->AddURL(
        parent, parent->children().size(), entry.title, entry.url, nullptr,
        entry.creation_time.is_null()
            ? std::nullopt
            : std::optional<base::Time>(entry.creation_time));
  }
  if (!added) {
    return false;
  }
  added_bookmarks_.insert(BookmarkKey(entry));
  return true;
}

bool DaoChromiumMigrationTarget::HasHistoryVisit(
    const HistoryVisit& entry) const {
  return added_history_.contains({entry.url.spec(), entry.visit_time});
}

void DaoChromiumMigrationTarget::AddHistoryVisit(const HistoryVisit& entry,
                                                 ItemWriteCallback callback) {
  history::HistoryService* history = HistoryServiceFactory::GetForProfile(
      profile_, ServiceAccessType::EXPLICIT_ACCESS);
  if (!history) {
    std::move(callback).Run(false);
    return;
  }
  history->AddPage(entry.url, entry.visit_time,
                   history::SOURCE_OS_MIGRATION_IMPORTED);
  history->SetPageTitle(entry.url, entry.title);
  history->QueryURLAndVisits(
      entry.url, history::VisitQuery404sPolicy::kInclude404s,
      base::BindOnce(&DaoChromiumMigrationTarget::OnHistoryWriteVerified,
                     weak_ptr_factory_.GetWeakPtr(), entry,
                     std::move(callback)),
      &history_task_tracker_);
}

void DaoChromiumMigrationTarget::OnHistoryWriteVerified(
    HistoryVisit entry,
    ItemWriteCallback callback,
    history::QueryURLAndVisitsResult result) {
  const bool persisted =
      result.success &&
      std::ranges::any_of(result.visits,
                          [&entry](const history::VisitRow& row) {
                            return row.visit_time == entry.visit_time;
                          });
  if (persisted) {
    added_history_.insert({entry.url.spec(), entry.visit_time});
  }
  std::move(callback).Run(persisted);
}

PasswordMatch DaoChromiumMigrationTarget::MatchPassword(
    const PasswordEntry& entry) const {
  auto it = passwords_.find(PasswordKey(entry));
  if (it == passwords_.end()) {
    return PasswordMatch::kNone;
  }
  return it->second == entry.password ? PasswordMatch::kSame
                                      : PasswordMatch::kConflict;
}

void DaoChromiumMigrationTarget::AddPassword(const PasswordEntry& entry,
                                             ItemWriteCallback callback) {
  if (!profile_->GetPrefs()->GetBoolean(
          password_manager::prefs::kCredentialsEnableService)) {
    std::move(callback).Run(false);
    return;
  }
  scoped_refptr<password_manager::PasswordStoreInterface> password_store =
      ProfilePasswordStoreFactory::GetForProfile(
          profile_, ServiceAccessType::EXPLICIT_ACCESS);
  if (!password_store ||
      password_store->GetError() !=
          password_manager::ActionableError::kNoError ||
      !pending_password_callback_.is_null()) {
    std::move(callback).Run(false);
    return;
  }
  password_manager::PasswordForm form;
  form.url = entry.origin;
  form.signon_realm = entry.signon_realm;
  form.username_value = entry.username;
  form.password_value = entry.password;
  form.date_created = entry.date_created;
  form.type = password_manager::PasswordForm::Type::kImported;
  pending_password_entry_ = entry;
  pending_password_callback_ = std::move(callback);
  password_store->AddLogin(
      form, base::BindOnce(&DaoChromiumMigrationTarget::OnPasswordAdded,
                           weak_ptr_factory_.GetWeakPtr(), password_store));
}

void DaoChromiumMigrationTarget::OnPasswordAdded(
    scoped_refptr<password_manager::PasswordStoreInterface> store) {
  if (!pending_password_entry_ || pending_password_callback_.is_null()) {
    return;
  }
  if (store->GetError() != password_manager::ActionableError::kNoError) {
    pending_password_entry_.reset();
    std::move(pending_password_callback_).Run(false);
    return;
  }
  password_manager::PasswordForm form;
  form.url = pending_password_entry_->origin;
  form.signon_realm = pending_password_entry_->signon_realm;
  store->GetLogins(password_manager::PasswordFormDigest(form),
                   weak_ptr_factory_.GetWeakPtr());
}

void DaoChromiumMigrationTarget::OnGetPasswordStoreResultsOrErrorFrom(
    password_manager::PasswordStoreInterface*,
    password_manager::LoginsResultOrError results_or_error) {
  if (!pending_password_entry_ || pending_password_callback_.is_null()) {
    return;
  }
  const PasswordEntry entry = *pending_password_entry_;
  bool persisted = false;
  if (std::holds_alternative<password_manager::LoginsResult>(
          results_or_error)) {
    persisted = std::ranges::any_of(
        std::get<password_manager::LoginsResult>(results_or_error),
        [&entry](const password_manager::StoredCredential& credential) {
          return credential.signon_realm == entry.signon_realm &&
                 credential.username_value == entry.username &&
                 credential.password_value == entry.password;
        });
  }
  if (persisted) {
    passwords_[PasswordKey(entry)] = entry.password;
  }
  pending_password_entry_.reset();
  std::move(pending_password_callback_).Run(persisted);
}

bool DaoChromiumMigrationTarget::IsTabOpen(const GURL& url) const {
  ProfileBrowserCollection* collection =
      ProfileBrowserCollection::GetForProfile(profile_);
  if (!collection) {
    return false;
  }
  bool found = false;
  collection->ForEach([&](BrowserWindowInterface* browser_window) {
    TabStripModel* model =
        browser_window->GetBrowserForMigrationOnly()->tab_strip_model();
    for (int index = 0; index < model->count(); ++index) {
      content::WebContents* contents = model->GetWebContentsAt(index);
      if (contents && contents->GetVisibleURL() == url) {
        found = true;
        return false;
      }
    }
    return true;
  });
  return found;
}

std::string DaoChromiumMigrationTarget::EnsureImportedTabFolder(
    const std::u16string& folder_name) {
  if (!LoadFolderData()) {
    return std::string();
  }
  const std::string id = base::Uuid::GenerateRandomV4().AsLowercaseString();
  base::DictValue folder;
  folder.Set("type", "folder");
  folder.Set("id", id);
  folder.Set("name", base::UTF16ToUTF8(folder_name));
  folder.Set("collapsed", true);
  folder.Set("children", base::ListValue());
  folder_items_->Append(std::move(folder));
  pending_folder_ = folder_items_->back().GetIfDict();
  return id;
}

bool DaoChromiumMigrationTarget::AddDormantTab(const TabEntry& entry,
                                               const std::string& folder_id) {
  const std::string* pending_id =
      pending_folder_ ? pending_folder_->FindString("id") : nullptr;
  if (!pending_id || *pending_id != folder_id) {
    return false;
  }
  BrowserWindowInterface* browser_window =
      chrome::FindLastActiveWithProfile(profile_);
  if (!browser_window) {
    return false;
  }
  content::WebContents* contents =
      chrome::AddAndReturnTabAt(browser_window, entry.url, -1, false);
  if (!contents) {
    return false;
  }
  const std::string tab_id = GetSidebarTabId(contents);
  base::DictValue child;
  child.Set("type", "tab");
  child.Set("tabId", tab_id);
  child.Set("url", entry.url.spec());
  child.Set("title", base::UTF16ToUTF8(entry.title));
  pending_folder_->FindList("children")->Append(std::move(child));
  pending_folder_tab_ids_.push_back(tab_id);

  if (auto* lifecycle =
          resource_coordinator::TabLifecycleUnitExternal::FromWebContents(
              contents)) {
    lifecycle->DiscardTab(::mojom::LifecycleUnitDiscardReason::EXTERNAL);
  }
  return true;
}

bool DaoChromiumMigrationTarget::FinishImportedTabFolder(
    const std::string& folder_id) {
  const std::string* pending_id =
      pending_folder_ ? pending_folder_->FindString("id") : nullptr;
  if (!pending_id || *pending_id != folder_id) {
    return false;
  }
  bool persisted = false;
  std::string json;
  if (base::JSONWriter::WriteWithOptions(
          folder_data_, base::JSONWriter::OPTIONS_PRETTY_PRINT, &json)) {
    persisted = WritePinnedTabsFileAtomically(
        profile_->GetPath().AppendASCII("dao_folders.json"), json);
    if (persisted) {
      DaoSidebarUIHandler::NotifyFolderDataChanged(profile_);
    }
  }
  if (persisted) {
    pending_folder_ = nullptr;
    pending_folder_tab_ids_.clear();
  }
  return persisted;
}

void DaoChromiumMigrationTarget::AbortImportedTabFolder(
    const std::string& folder_id) {
  if (folder_items_) {
    folder_items_->EraseIf([&folder_id](const base::Value& value) {
      const base::DictValue* folder = value.GetIfDict();
      const std::string* id = folder ? folder->FindString("id") : nullptr;
      return id && *id == folder_id;
    });
  }
  pending_folder_ = nullptr;

  ProfileBrowserCollection* collection =
      ProfileBrowserCollection::GetForProfile(profile_);
  for (const std::string& tab_id : pending_folder_tab_ids_) {
    if (!collection) {
      continue;
    }
    collection->ForEach([&tab_id](BrowserWindowInterface* browser_window) {
      TabStripModel* model =
          browser_window->GetBrowserForMigrationOnly()->tab_strip_model();
      for (int index = 0; index < model->count(); ++index) {
        content::WebContents* contents = model->GetWebContentsAt(index);
        if (contents && GetSidebarTabId(contents) == tab_id) {
          model->DetachAndDeleteWebContentsAt(index);
          return false;
        }
      }
      return true;
    });
  }
  pending_folder_tab_ids_.clear();
}

bool DaoChromiumMigrationTarget::IsExtensionInstalled(
    const std::string& id) const {
  return extensions::ExtensionRegistry::Get(profile_)->GetInstalledExtension(
             id) != nullptr;
}

bool DaoChromiumMigrationTarget::QueueExtensionInstall(
    const ExtensionEntry& entry) {
  if (cancel_extension_installs_) {
    return false;
  }
  extension_queue_.push_back(entry);
  StartNextExtensionInstall();
  return true;
}

void DaoChromiumMigrationTarget::FinishExtensionInstalls(
    base::OnceCallback<void(uint64_t installed, uint64_t failed)> callback) {
  CHECK(extension_completion_callback_.is_null());
  extension_completion_callback_ = std::move(callback);
  MaybeFinishExtensionInstalls();
}

void DaoChromiumMigrationTarget::CancelExtensionInstalls() {
  cancel_extension_installs_ = true;
  if (extension_installer_) {
    ExtensionEntry active = std::move(extension_queue_.front());
    extension_queue_.clear();
    extension_queue_.push_back(std::move(active));
  } else {
    extension_queue_.clear();
  }
  MaybeFinishExtensionInstalls();
}

void DaoChromiumMigrationTarget::StartNextExtensionInstall() {
  if (extension_installer_ || extension_queue_.empty() ||
      cancel_extension_installs_) {
    MaybeFinishExtensionInstalls();
    return;
  }
  const ExtensionEntry& entry = extension_queue_.front();
  BrowserWindowInterface* browser_window =
      chrome::FindLastActiveWithProfile(profile_);
  gfx::NativeWindow parent =
      browser_window ? browser_window->GetWindow()->GetNativeWindow()
                     : gfx::NativeWindow();
  extension_installer_ =
      base::MakeRefCounted<extensions::WebstoreInstallWithPrompt>(
          entry.id, profile_, parent,
          base::BindOnce(
              [](base::WeakPtr<DaoChromiumMigrationTarget> target, bool enabled,
                 std::string extension_id, bool success, const std::string&,
                 extensions::webstore_install::Result) {
                if (target) {
                  target->OnExtensionInstallFinished(
                      enabled, std::move(extension_id), success);
                }
              },
              weak_ptr_factory_.GetWeakPtr(), entry.enabled, entry.id));
  extension_installer_->BeginInstall();
}

void DaoChromiumMigrationTarget::OnExtensionInstallFinished(
    bool enabled,
    std::string extension_id,
    bool success) {
  if (success && !enabled) {
    extensions::ExtensionRegistrar::Get(profile_)->DisableExtension(
        extension_id, {extensions::disable_reason::DISABLE_USER_ACTION});
  }
  if (success) {
    ++installed_extension_count_;
  } else {
    ++failed_extension_count_;
  }
  extension_installer_.reset();
  if (!extension_queue_.empty()) {
    extension_queue_.pop_front();
  }
  StartNextExtensionInstall();
}

void DaoChromiumMigrationTarget::MaybeFinishExtensionInstalls() {
  if (extension_installer_ || !extension_queue_.empty() ||
      extension_completion_callback_.is_null()) {
    return;
  }
  std::move(extension_completion_callback_)
      .Run(installed_extension_count_, failed_extension_count_);
}

void DaoChromiumMigrationTarget::SetExistingPasswords(
    std::map<std::pair<std::string, std::u16string>, std::u16string>
        passwords) {
  passwords_ = std::move(passwords);
}

std::string DaoChromiumMigrationTarget::BookmarkKey(
    const BookmarkEntry& entry) const {
  return JoinPath(entry.path) + base::UTF16ToUTF8(entry.title) + "\0" +
         entry.url.spec();
}

const bookmarks::BookmarkNode*
DaoChromiumMigrationTarget::FindOrCreateBookmarkParent(
    const BookmarkEntry& entry,
    const std::u16string& root_name) {
  const bookmarks::BookmarkNode* bar =
      bookmark_model_->account_bookmark_bar_node()
          ? bookmark_model_->account_bookmark_bar_node()
          : bookmark_model_->bookmark_bar_node();
  const bookmarks::BookmarkNode* parent = nullptr;
  for (const auto& child : bar->children()) {
    if (child->is_folder() && child->GetTitle() == root_name) {
      parent = child.get();
      break;
    }
  }
  if (!parent) {
    parent = bookmark_model_->AddFolder(bar, bar->children().size(), root_name);
  }
  std::vector<std::u16string> resolved_path;
  for (const std::u16string& component : entry.path) {
    resolved_path.push_back(component);
    const std::string key = JoinPath(resolved_path);
    auto known = bookmark_folders_.find(key);
    if (known != bookmark_folders_.end()) {
      parent = known->second;
      continue;
    }
    const bookmarks::BookmarkNode* existing = nullptr;
    for (const auto& child : parent->children()) {
      if (child->is_folder() && child->GetTitle() == component) {
        existing = child.get();
        break;
      }
    }
    parent = existing ? existing
                      : bookmark_model_->AddFolder(
                            parent, parent->children().size(), component);
    bookmark_folders_[key] = parent;
  }
  return parent;
}

bool DaoChromiumMigrationTarget::LoadFolderData() {
  std::string contents;
  const base::FilePath path =
      profile_->GetPath().AppendASCII("dao_folders.json");
  if (base::PathExists(path)) {
    if (!base::ReadFileToString(path, &contents)) {
      return false;
    }
    std::optional<base::DictValue> parsed =
        base::JSONReader::ReadDict(contents, base::JSON_PARSE_RFC);
    if (!parsed) {
      return false;
    }
    folder_data_ = std::move(*parsed);
  } else {
    folder_data_.Set("version", 1);
    folder_data_.Set("items", base::ListValue());
  }
  folder_items_ = folder_data_.FindList("items");
  return folder_items_ != nullptr;
}

}  // namespace dao::import
