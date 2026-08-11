// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/ui/webui/dao_import_ui.h"

#include <algorithm>
#include <optional>
#include <string_view>
#include <utility>

#include "base/functional/bind.h"
#include "base/values.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/grit/dao_import_resources.h"
#include "chrome/grit/dao_import_resources_map.h"
#include "content/public/browser/web_ui_data_source.h"
#include "content/public/common/url_constants.h"
#include "dao/browser/import/dao_migration_service.h"
#include "dao/browser/import/dao_migration_service_factory.h"
#include "dao/browser/strings/grit/dao_strings.h"
#include "services/network/public/mojom/content_security_policy.mojom.h"

namespace dao {
namespace {

base::DictValue SourceToValue(const import::SourceProfile &source) {
  base::DictValue value;
  value.Set("id", source.id);
  value.Set("kind", import::SourceKindToString(source.kind));
  value.Set("browserName", source.browser_name);
  value.Set("profileName", source.profile_name);
  base::ListValue categories;
  for (import::DataCategory category : source.supported_categories) {
    categories.Append(import::DataCategoryToString(category));
  }
  value.Set("supportedCategories", std::move(categories));
  return value;
}

base::DictValue StateToValue(const import::JobState &state) {
  base::DictValue value;
  value.Set("sourceId", state.source_id);
  value.Set("cancelRequested", state.cancel_requested);
  base::ListValue categories;
  bool terminal = !state.category_states.empty();
  for (size_t index = 0; index < state.category_states.size(); ++index) {
    const import::CategoryState &category_state = state.category_states[index];
    terminal = terminal && import::IsTerminalPhase(category_state.phase);
    base::DictValue category;
    category.Set("category", import::DataCategoryToString(
                                 state.selected_categories[index]));
    category.Set("phase", import::CategoryPhaseToString(category_state.phase));
    category.Set("imported",
                 static_cast<double>(category_state.result.imported));
    category.Set("skipped", static_cast<double>(category_state.result.skipped));
    category.Set("conflicted",
                 static_cast<double>(category_state.result.conflicted));
    category.Set("failed", static_cast<double>(category_state.result.failed));
    category.Set("completedItems",
                 static_cast<double>(category_state.completed_items));
    category.Set("totalItems", static_cast<double>(category_state.total_items));
    category.Set("indeterminate", category_state.indeterminate);
    category.Set("errorCode", category_state.error_code);
    categories.Append(std::move(category));
  }
  value.Set("terminal", terminal);
  value.Set("categories", std::move(categories));
  return value;
}

std::optional<import::DataCategory> ParseCategory(std::string_view value) {
  if (value == "bookmarks") {
    return import::DataCategory::kBookmarks;
  }
  if (value == "history") {
    return import::DataCategory::kHistory;
  }
  if (value == "passwords") {
    return import::DataCategory::kPasswords;
  }
  if (value == "tabs") {
    return import::DataCategory::kTabs;
  }
  if (value == "extensions") {
    return import::DataCategory::kExtensions;
  }
  return std::nullopt;
}

std::vector<import::DataCategory> ParseCategories(const base::Value &value) {
  std::vector<import::DataCategory> categories;
  const base::ListValue *list = value.GetIfList();
  if (!list) {
    return categories;
  }
  for (const base::Value &item : *list) {
    const std::string *name = item.GetIfString();
    std::optional<import::DataCategory> category =
        name ? ParseCategory(*name) : std::nullopt;
    if (category &&
        std::ranges::find(categories, *category) == categories.end()) {
      categories.push_back(*category);
    }
  }
  return categories;
}

} // namespace

DaoImportUIConfig::DaoImportUIConfig()
    : WebUIConfig(content::kChromeUIScheme, "import") {}

std::unique_ptr<content::WebUIController>
DaoImportUIConfig::CreateWebUIController(content::WebUI *web_ui,
                                         const GURL &url) {
  return std::make_unique<DaoImportUI>(web_ui);
}

DaoImportUIHandler::DaoImportUIHandler(import::DaoMigrationService *service)
    : service_(service) {}

DaoImportUIHandler::~DaoImportUIHandler() = default;

void DaoImportUIHandler::RegisterMessages() {
  web_ui()->RegisterMessageCallback(
      "detectImportSources",
      base::BindRepeating(&DaoImportUIHandler::HandleDetectSources,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "getImportItemCount",
      base::BindRepeating(&DaoImportUIHandler::HandleCountSourceItems,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "getBrowserMigrationState",
      base::BindRepeating(&DaoImportUIHandler::HandleGetState,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "startBrowserMigration",
      base::BindRepeating(&DaoImportUIHandler::HandleStart,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "cancelBrowserMigration",
      base::BindRepeating(&DaoImportUIHandler::HandleCancel,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "retryBrowserMigrationCategories",
      base::BindRepeating(&DaoImportUIHandler::HandleRetry,
                          base::Unretained(this)));
  state_subscription_ = service_->AddObserver(base::BindRepeating(
      &DaoImportUIHandler::OnStateChanged, base::Unretained(this)));
}

void DaoImportUIHandler::HandleDetectSources(const base::ListValue &args) {
  AllowJavascript();
  if (args.empty() || !args[0].is_string()) {
    return;
  }
  service_->DetectSources(base::BindOnce(&DaoImportUIHandler::ReplyWithSources,
                                         weak_ptr_factory_.GetWeakPtr(),
                                         args[0].GetString()));
}

void DaoImportUIHandler::HandleGetState(const base::ListValue &args) {
  AllowJavascript();
  if (args.empty() || !args[0].is_string()) {
    return;
  }
  std::optional<import::JobState> state = service_->GetState();
  FireWebUIListener(args[0].GetString(),
                    state ? base::Value(StateToValue(*state)) : base::Value());
}

void DaoImportUIHandler::HandleCountSourceItems(const base::ListValue &args) {
  AllowJavascript();
  if (args.empty() || !args[0].is_string()) {
    return;
  }
  if (args.size() < 3 || !args[1].is_string() || !args[2].is_string()) {
    FireWebUIListener(args[0].GetString(), base::Value());
    return;
  }
  std::optional<import::DataCategory> category =
      ParseCategory(args[2].GetString());
  if (!category) {
    FireWebUIListener(args[0].GetString(), base::Value());
    return;
  }
  service_->CountSourceItems(args[1].GetString(), *category,
                             base::BindOnce(&DaoImportUIHandler::ReplyWithCount,
                                            weak_ptr_factory_.GetWeakPtr(),
                                            args[0].GetString()));
}

void DaoImportUIHandler::HandleStart(const base::ListValue &args) {
  AllowJavascript();
  if (args.size() < 2 || !args[0].is_string()) {
    return;
  }
  service_->Start(args[0].GetString(), ParseCategories(args[1]));
}

void DaoImportUIHandler::HandleCancel(const base::ListValue &args) {
  service_->Cancel();
}

void DaoImportUIHandler::HandleRetry(const base::ListValue &args) {
  AllowJavascript();
  if (args.empty()) {
    return;
  }
  service_->Retry(ParseCategories(args[0]));
}

void DaoImportUIHandler::ReplyWithSources(
    std::string callback_id, std::vector<import::SourceProfile> sources) {
  if (!IsJavascriptAllowed()) {
    return;
  }
  base::ListValue values;
  for (const import::SourceProfile &source : sources) {
    values.Append(SourceToValue(source));
  }
  FireWebUIListener(callback_id, values);
  FireWebUIListener("browser-migration-sources-changed", values);
}

void DaoImportUIHandler::ReplyWithCount(std::string callback_id,
                                        std::optional<uint64_t> count) {
  if (!IsJavascriptAllowed()) {
    return;
  }
  FireWebUIListener(callback_id, count
                                     ? base::Value(static_cast<double>(*count))
                                     : base::Value());
}

void DaoImportUIHandler::OnStateChanged(const import::JobState &state) {
  if (IsJavascriptAllowed()) {
    FireWebUIListener("browser-migration-state-changed", StateToValue(state));
  }
}

DaoImportUI::DaoImportUI(content::WebUI *web_ui) : WebUIController(web_ui) {
  Profile *profile = Profile::FromWebUI(web_ui);
  content::WebUIDataSource *source =
      content::WebUIDataSource::CreateAndAdd(profile, "import");
  source->AddResourcePaths(kDaoImportResources);
  source->SetDefaultResource(IDR_DAO_IMPORT_IMPORT_HTML);
  source->AddLocalizedStrings({
      {"daoImportWizardName", IDS_DAO_IMPORT_WIZARD_NAME},
      {"daoImportStepSource", IDS_DAO_IMPORT_STEP_SOURCE},
      {"daoImportSourceTitle", IDS_DAO_IMPORT_SOURCE_TITLE},
      {"daoImportSourceDescription", IDS_DAO_IMPORT_SOURCE_DESCRIPTION},
      {"daoImportDetectingSources", IDS_DAO_IMPORT_DETECTING_SOURCES},
      {"daoImportNoSources", IDS_DAO_IMPORT_NO_SOURCES},
      {"daoImportScanAgain", IDS_DAO_IMPORT_SCAN_AGAIN},
      {"daoImportBrowserChrome", IDS_DAO_IMPORT_BROWSER_CHROME},
      {"daoImportBrowserArc", IDS_DAO_IMPORT_BROWSER_ARC},
      {"daoImportBrowserEdge", IDS_DAO_IMPORT_BROWSER_EDGE},
      {"daoImportBrowserSafari", IDS_DAO_IMPORT_BROWSER_SAFARI},
      {"daoImportBrowserFirefox", IDS_DAO_IMPORT_BROWSER_FIREFOX},
      {"daoImportSourceNotDetected", IDS_DAO_IMPORT_SOURCE_NOT_DETECTED},
      {"daoImportStepData", IDS_DAO_IMPORT_STEP_DATA},
      {"daoImportDataTitle", IDS_DAO_IMPORT_DATA_TITLE},
      {"daoImportDataDescription", IDS_DAO_IMPORT_DATA_DESCRIPTION},
      {"daoImportCategoryBookmarks", IDS_DAO_IMPORT_CATEGORY_BOOKMARKS},
      {"daoImportCategoryHistory", IDS_DAO_IMPORT_CATEGORY_HISTORY},
      {"daoImportCategoryPasswords", IDS_DAO_IMPORT_CATEGORY_PASSWORDS},
      {"daoImportCategoryTabs", IDS_DAO_IMPORT_CATEGORY_TABS},
      {"daoImportCategoryExtensions", IDS_DAO_IMPORT_CATEGORY_EXTENSIONS},
      {"daoImportCountScanning", IDS_DAO_IMPORT_COUNT_SCANNING},
      {"daoImportCountScanned", IDS_DAO_IMPORT_COUNT_SCANNED},
      {"daoImportCountUnavailable", IDS_DAO_IMPORT_COUNT_UNAVAILABLE},
      {"daoImportPasswordTipTitle", IDS_DAO_IMPORT_PASSWORD_TIP_TITLE},
      {"daoImportPasswordTipDescription",
       IDS_DAO_IMPORT_PASSWORD_TIP_DESCRIPTION},
      {"daoImportExtensionTipTitle", IDS_DAO_IMPORT_EXTENSION_TIP_TITLE},
      {"daoImportExtensionTipDescription",
       IDS_DAO_IMPORT_EXTENSION_TIP_DESCRIPTION},
      {"daoImportStepProgress", IDS_DAO_IMPORT_STEP_PROGRESS},
      {"daoImportProgressTitle", IDS_DAO_IMPORT_PROGRESS_TITLE},
      {"daoImportProgressDescription", IDS_DAO_IMPORT_PROGRESS_DESCRIPTION},
      {"daoImportSourceFallback", IDS_DAO_IMPORT_SOURCE_FALLBACK},
      {"daoImportDaoName", IDS_DAO_IMPORT_DAO_NAME},
      {"daoImportPhasePending", IDS_DAO_IMPORT_PHASE_PENDING},
      {"daoImportPhaseSnapshotting", IDS_DAO_IMPORT_PHASE_SNAPSHOTTING},
      {"daoImportPhaseReading", IDS_DAO_IMPORT_PHASE_READING},
      {"daoImportPhaseWriting", IDS_DAO_IMPORT_PHASE_WRITING},
      {"daoImportPhaseSucceeded", IDS_DAO_IMPORT_PHASE_SUCCEEDED},
      {"daoImportPhaseFailed", IDS_DAO_IMPORT_PHASE_FAILED},
      {"daoImportPhaseCancelled", IDS_DAO_IMPORT_PHASE_CANCELLED},
      {"daoImportPartialTitle", IDS_DAO_IMPORT_PARTIAL_TITLE},
      {"daoImportPartialDescription", IDS_DAO_IMPORT_PARTIAL_DESCRIPTION},
      {"daoImportDoneTitle", IDS_DAO_IMPORT_DONE_TITLE},
      {"daoImportDoneDescription", IDS_DAO_IMPORT_DONE_DESCRIPTION},
      {"daoImportCancelledTitle", IDS_DAO_IMPORT_CANCELLED_TITLE},
      {"daoImportCancelledDescription", IDS_DAO_IMPORT_CANCELLED_DESCRIPTION},
      {"daoImportMergeHint", IDS_DAO_IMPORT_MERGE_HINT},
      {"daoImportLocalHint", IDS_DAO_IMPORT_LOCAL_HINT},
      {"daoImportBack", IDS_DAO_IMPORT_BACK},
      {"daoImportStart", IDS_DAO_IMPORT_START},
      {"daoImportCancel", IDS_DAO_IMPORT_CANCEL},
      {"daoImportRetryFailed", IDS_DAO_IMPORT_RETRY_FAILED},
      {"daoImportContinue", IDS_DAO_IMPORT_CONTINUE},
  });
  source->UseStringsJs();
  source->OverrideContentSecurityPolicy(
      network::mojom::CSPDirectiveName::TrustedTypes,
      "trusted-types default lit-html-desktop;");
  web_ui->AddMessageHandler(std::make_unique<DaoImportUIHandler>(
      import::DaoMigrationServiceFactory::GetForProfile(profile)));
}

DaoImportUI::~DaoImportUI() = default;

} // namespace dao
