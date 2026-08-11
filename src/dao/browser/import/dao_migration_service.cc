// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/import/dao_migration_service.h"

#include <algorithm>
#include <iterator>
#include <type_traits>
#include <utility>

#include "base/functional/bind.h"
#include "base/notreached.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/thread_pool.h"
#include "build/build_config.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/importer/external_process_importer_host.h"
#include "chrome/browser/importer/importer_list.h"
#include "chrome/browser/password_manager/factories/profile_password_store_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "components/keyed_service/core/service_access_type.h"
#include "components/password_manager/core/browser/password_store/password_store_interface.h"
#include "crypto/sha2.h"
#include "dao/browser/import/dao_chromium_migration_target.h"
#include "dao/browser/import/dao_chromium_profile_adapter.h"
#include "dao/browser/import/dao_legacy_profile_writer.h"
#include "dao/browser/strings/grit/dao_strings.h"
#include "ui/base/l10n/l10n_util.h"
#if BUILDFLAG(IS_MAC)
#include "dao/browser/import/dao_chromium_password_decryptor_mac.h"
#endif

namespace dao::import {
namespace {

bool SupportsChromiumAdapter(SourceKind kind) {
  return kind == SourceKind::kChrome || kind == SourceKind::kArc ||
         kind == SourceKind::kEdge;
}

std::string LegacySourceId(const user_data_importer::SourceProfile &profile) {
  const std::string identity = base::UTF16ToUTF8(profile.importer_name) + ":" +
                               profile.source_path.AsUTF8Unsafe() + ":" +
                               base::UTF16ToUTF8(profile.profile);
  return base::HexEncode(crypto::SHA256HashString(identity));
}

std::optional<SourceKind>
LegacySourceKind(user_data_importer::ImporterType type) {
  if (type == user_data_importer::TYPE_FIREFOX) {
    return SourceKind::kFirefox;
  }
#if BUILDFLAG(IS_MAC)
  if (type == user_data_importer::TYPE_SAFARI) {
    return SourceKind::kSafari;
  }
#endif
  return std::nullopt;
}

std::vector<DataCategory> LegacyCategories(uint16_t services) {
  std::vector<DataCategory> categories;
  if (services & user_data_importer::FAVORITES) {
    categories.push_back(DataCategory::kBookmarks);
  }
  if (services & user_data_importer::HISTORY) {
    categories.push_back(DataCategory::kHistory);
  }
  if (services & user_data_importer::PASSWORDS) {
    categories.push_back(DataCategory::kPasswords);
  }
  return categories;
}

uint16_t LegacyImportItem(DataCategory category) {
  switch (category) {
  case DataCategory::kBookmarks:
    return user_data_importer::FAVORITES;
  case DataCategory::kHistory:
    return user_data_importer::HISTORY;
  case DataCategory::kPasswords:
    return user_data_importer::PASSWORDS;
  case DataCategory::kTabs:
  case DataCategory::kExtensions:
    return user_data_importer::NONE;
  }
  NOTREACHED();
}

void AddWriteResult(CategoryResult *totals, const WriteResult &result) {
  totals->imported += result.imported;
  totals->skipped += result.skipped;
  totals->conflicted += result.conflicted;
  totals->failed += result.failed;
}

} // namespace

DaoMigrationService::ReadResult::ReadResult() = default;
DaoMigrationService::ReadResult::ReadResult(const ReadResult &) = default;
DaoMigrationService::ReadResult &
DaoMigrationService::ReadResult::operator=(const ReadResult &) = default;
DaoMigrationService::ReadResult::ReadResult(ReadResult &&) = default;
DaoMigrationService::ReadResult &
DaoMigrationService::ReadResult::operator=(ReadResult &&) = default;
DaoMigrationService::ReadResult::~ReadResult() = default;

DaoMigrationService::DaoMigrationService(Profile *profile) : profile_(profile) {
  scoped_refptr<password_manager::PasswordStoreInterface> password_store =
      ProfilePasswordStoreFactory::GetForProfile(
          profile_, ServiceAccessType::EXPLICIT_ACCESS);
  if (password_store) {
    password_store->GetAllLogins(weak_ptr_factory_.GetWeakPtr());
  } else {
    passwords_ready_ = true;
  }
}

DaoMigrationService::~DaoMigrationService() = default;

void DaoMigrationService::DetectSources(SourcesCallback callback) {
  const uint64_t generation = ++source_detection_generation_;
  pending_sources_callback_ = std::move(callback);
  chromium_detection_done_ = false;
  legacy_detection_done_ = false;
  pending_detected_sources_.clear();
  pending_legacy_sources_.clear();
  detector_.Detect(
      base::BindOnce(&DaoMigrationService::OnChromiumSourcesDetected,
                     weak_ptr_factory_.GetWeakPtr(), generation));
  importer_list_ = std::make_unique<ImporterList>();
  importer_list_->DetectSourceProfiles(
      g_browser_process->GetApplicationLocale(), true,
      base::BindOnce(&DaoMigrationService::OnLegacySourcesDetected,
                     weak_ptr_factory_.GetWeakPtr(), generation));
}

void DaoMigrationService::CountSourceItems(const std::string &source_id,
                                           DataCategory category,
                                           CountCallback callback) {
  const SourceProfile *source = FindDetectedSource(source_id);
  if (!source || !SupportsChromiumAdapter(source->kind) ||
      std::ranges::find(source->supported_categories, category) ==
          source->supported_categories.end()) {
    std::move(callback).Run(std::nullopt);
    return;
  }
  std::optional<base::FilePath> profile_path =
      detector_.ResolveProfilePath(source_id);
  if (!profile_path) {
    std::move(callback).Run(std::nullopt);
    return;
  }

  if (category == DataCategory::kTabs) {
    SnapshotRequest request = BuildSnapshotRequest(category, *profile_path);
    request.cancellation = base::MakeRefCounted<SnapshotCancellationFlag>();
    DaoProfileSnapshot::Create(
        std::move(request),
        base::BindOnce(&DaoMigrationService::OnCountSnapshotReady,
                       weak_ptr_factory_.GetWeakPtr(), category,
                       std::move(callback)));
    return;
  }

  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce(&DaoMigrationService::CountProfileCandidates, category,
                     std::move(*profile_path)),
      std::move(callback));
}

bool DaoMigrationService::Start(const std::string &source_id,
                                std::vector<DataCategory> categories) {
  if (job_ && !job_->IsTerminal()) {
    return false;
  }
  const SourceProfile *source = FindDetectedSource(source_id);
  if (!source || categories.empty()) {
    return false;
  }
  std::erase_if(categories, [source](DataCategory category) {
    return std::ranges::find(source->supported_categories, category) ==
           source->supported_categories.end();
  });
  if (categories.empty()) {
    return false;
  }
  std::optional<base::FilePath> chromium_path;
  std::optional<user_data_importer::SourceProfile> legacy_source;
  if (SupportsChromiumAdapter(source->kind)) {
    chromium_path = detector_.ResolveProfilePath(source_id);
    if (!chromium_path) {
      return false;
    }
  } else {
    const user_data_importer::SourceProfile *found =
        FindLegacySource(source_id);
    if (!found) {
      return false;
    }
    legacy_source = *found;
  }
  job_ = std::make_unique<DaoMigrationJob>(*source, std::move(categories));
  active_chromium_source_path_ = std::move(chromium_path);
  active_legacy_source_ = std::move(legacy_source);
  target_ = std::make_unique<DaoChromiumMigrationTarget>(profile_);
  target_->SetExistingPasswords(existing_passwords_);
  ProcessNextCategory();
  return true;
}

bool DaoMigrationService::Retry(std::vector<DataCategory> categories) {
  if (!job_ || !job_->IsTerminal()) {
    return false;
  }
  const JobState prior = job_->GetState();
  std::erase_if(categories, [&prior](DataCategory category) {
    auto it = std::ranges::find(prior.selected_categories, category);
    if (it == prior.selected_categories.end()) {
      return true;
    }
    const size_t index = std::distance(prior.selected_categories.begin(), it);
    return prior.category_states[index].phase != CategoryPhase::kFailed;
  });
  return !categories.empty() && Start(prior.source_id, std::move(categories));
}

void DaoMigrationService::Cancel() {
  if (!job_ || job_->IsTerminal()) {
    return;
  }
  job_->RequestCancel();
  if (snapshot_cancellation_) {
    snapshot_cancellation_->Cancel();
  }
  if (legacy_importer_host_) {
    legacy_importer_host_->Cancel();
    NotifyObservers();
    return;
  }
  if (active_legacy_writer_) {
    NotifyObservers();
    return;
  }
  if (active_write_category_ == DataCategory::kExtensions && target_) {
    if (!waiting_for_extension_installs_) {
      waiting_for_extension_installs_ = true;
      target_->FinishExtensionInstalls(
          base::BindOnce(&DaoMigrationService::OnExtensionInstallsFinished,
                         weak_ptr_factory_.GetWeakPtr()));
    }
    target_->CancelExtensionInstalls();
    NotifyObservers();
    return;
  }
  if (active_write_) {
    NotifyObservers();
    return;
  }
  job_->CancelRunningCategoryAtBatchBoundary(
      active_write_category_ ? active_write_totals_ : CategoryResult());
  NotifyObservers();
}

std::optional<JobState> DaoMigrationService::GetState() const {
  return job_ ? std::optional<JobState>(job_->GetState()) : std::nullopt;
}

base::CallbackListSubscription
DaoMigrationService::AddObserver(StateObserver observer) {
  return observers_.Add(std::move(observer));
}

void DaoMigrationService::Shutdown() {
  if (snapshot_cancellation_) {
    snapshot_cancellation_->Cancel();
  }
  weak_ptr_factory_.InvalidateWeakPtrs();
  importer_list_.reset();
  if (legacy_importer_host_) {
    legacy_importer_host_->set_observer(nullptr);
    legacy_importer_host_->Cancel();
    legacy_importer_host_ = nullptr;
  }
  active_legacy_writer_.reset();
  job_.reset();
  target_.reset();
  profile_ = nullptr;
}

void DaoMigrationService::ImportStarted() {}

void DaoMigrationService::ImportItemStarted(user_data_importer::ImportItem) {
  NotifyObservers();
}

void DaoMigrationService::ImportItemEnded(user_data_importer::ImportItem item) {
  if (!job_ || !active_legacy_category_ || job_->IsTerminal() ||
      LegacyImportItem(*active_legacy_category_) != item) {
    return;
  }
  legacy_item_ended_ =
      job_->AdvanceCategory(*active_legacy_category_, CategoryPhase::kWriting);
  NotifyObservers();
}

void DaoMigrationService::ImportEnded() {
  legacy_importer_host_ = nullptr;
  if (active_legacy_writer_) {
    active_legacy_writer_->FinishWhenIdle(
        base::BindOnce(&DaoMigrationService::OnLegacyWritesFinished,
                       weak_ptr_factory_.GetWeakPtr()));
    return;
  }
  OnLegacyWritesFinished();
}

void DaoMigrationService::OnLegacyWritesFinished() {
  active_legacy_writer_.reset();
  if (!job_ || !active_legacy_category_ || job_->IsTerminal()) {
    active_legacy_category_.reset();
    return;
  }
  if (job_->cancel_requested()) {
    job_->CancelRunningCategoryAtBatchBoundary(active_legacy_result_);
  } else if (legacy_item_ended_) {
    if (active_legacy_result_.failed > 0) {
      job_->FailCategory(*active_legacy_category_, "destination_write_failed",
                         active_legacy_result_);
    } else {
      job_->CompleteCategory(*active_legacy_category_, active_legacy_result_);
    }
  } else {
    job_->FailCategory(*active_legacy_category_, "legacy_import_failed");
  }
  active_legacy_category_.reset();
  NotifyObservers();
  ProcessNextCategory();
}

void DaoMigrationService::OnGetPasswordStoreResultsOrErrorFrom(
    password_manager::PasswordStoreInterface *,
    password_manager::LoginsResultOrError results_or_error) {
  passwords_ready_ = true;
  if (std::holds_alternative<password_manager::LoginsResult>(
          results_or_error)) {
    for (const password_manager::StoredCredential &credential :
         std::get<password_manager::LoginsResult>(results_or_error)) {
      existing_passwords_[{credential.signon_realm,
                           credential.username_value}] =
          credential.password_value;
    }
  }
  if (target_) {
    target_->SetExistingPasswords(existing_passwords_);
  }
  if (waiting_for_passwords_) {
    waiting_for_passwords_ = false;
    ProcessNextCategory();
  }
}

// static
DaoMigrationService::ReadResult
DaoMigrationService::ReadSnapshot(DataCategory category, SourceKind source_kind,
                                  base::FilePath snapshot_path) {
  std::unique_ptr<PasswordDecryptor> decryptor;
#if BUILDFLAG(IS_MAC)
  if (category == DataCategory::kPasswords) {
    decryptor = std::make_unique<DaoChromiumPasswordDecryptorMac>(source_kind);
  }
#endif
  DaoChromiumProfileAdapter adapter(std::move(snapshot_path),
                                    std::move(decryptor));
  ReadResult result;
  auto copy_batch = [&result](auto batch) {
    result.success = batch.success;
    result.error_code = std::move(batch.error_code);
    result.records = std::move(batch.records);
  };
  switch (category) {
  case DataCategory::kBookmarks:
    copy_batch(adapter.ReadBookmarks());
    break;
  case DataCategory::kHistory:
    copy_batch(adapter.ReadHistory());
    break;
  case DataCategory::kPasswords:
    copy_batch(adapter.ReadPasswords());
    break;
  case DataCategory::kTabs:
    copy_batch(adapter.ReadTabs());
    break;
  case DataCategory::kExtensions:
    copy_batch(adapter.ReadExtensions());
    break;
  }
  return result;
}

// static
std::optional<uint64_t>
DaoMigrationService::CountProfileCandidates(DataCategory category,
                                            base::FilePath profile_path) {
  DaoChromiumProfileAdapter adapter(std::move(profile_path));
  return adapter.CountCandidates(category);
}

// static
std::optional<uint64_t>
DaoMigrationService::CountSnapshotCandidates(DataCategory category,
                                             SnapshotResult snapshot) {
  if (!snapshot.success) {
    return std::nullopt;
  }
  return CountProfileCandidates(category, std::move(snapshot.path));
}

SnapshotRequest
DaoMigrationService::BuildSnapshotRequest(DataCategory category,
                                          const base::FilePath &source_path) {
  SnapshotRequest request;
  request.source_profile = source_path;
  request.cancellation = snapshot_cancellation_;
  switch (category) {
  case DataCategory::kBookmarks:
    request.relative_paths = {base::FilePath(FILE_PATH_LITERAL("Bookmarks"))};
    break;
  case DataCategory::kHistory:
    request.relative_paths = {base::FilePath(FILE_PATH_LITERAL("History"))};
    request.include_sqlite_sidecars = true;
    break;
  case DataCategory::kPasswords:
    request.relative_paths = {base::FilePath(FILE_PATH_LITERAL("Login Data"))};
    request.include_sqlite_sidecars = true;
    break;
  case DataCategory::kTabs:
    request.relative_paths = {base::FilePath(FILE_PATH_LITERAL("Sessions"))};
    break;
  case DataCategory::kExtensions:
    request.relative_paths = {base::FilePath(FILE_PATH_LITERAL("Preferences"))};
    break;
  }
  return request;
}

void DaoMigrationService::ProcessNextCategory() {
  if (!job_ || job_->IsTerminal() || job_->cancel_requested()) {
    NotifyObservers();
    return;
  }
  const SourceProfile &selected = job_->source();
  if (!SupportsChromiumAdapter(selected.kind)) {
    ProcessNextLegacyCategory();
    return;
  }
  const JobState state = job_->GetState();
  for (size_t index = 0; index < state.selected_categories.size(); ++index) {
    const DataCategory category = state.selected_categories[index];
    if (state.category_states[index].phase != CategoryPhase::kPending) {
      continue;
    }
    if (category == DataCategory::kPasswords && !passwords_ready_) {
      waiting_for_passwords_ = true;
      NotifyObservers();
      return;
    }
    if (!active_chromium_source_path_) {
      if (job_->StartCategory(category)) {
        job_->FailCategory(category, "source_missing");
        NotifyObservers();
      }
      continue;
    }
    if (!job_->StartCategory(category)) {
      return;
    }
    snapshot_cancellation_ = base::MakeRefCounted<SnapshotCancellationFlag>();
    NotifyObservers();
    DaoProfileSnapshot::Create(
        BuildSnapshotRequest(category, *active_chromium_source_path_),
        base::BindOnce(&DaoMigrationService::OnSnapshotReady,
                       weak_ptr_factory_.GetWeakPtr(), category));
    return;
  }
  NotifyObservers();
}

void DaoMigrationService::ProcessNextLegacyCategory() {
  if (!job_ || job_->IsTerminal() || job_->cancel_requested() ||
      legacy_importer_host_) {
    NotifyObservers();
    return;
  }
  const JobState state = job_->GetState();
  if (!active_legacy_source_) {
    for (DataCategory category : state.selected_categories) {
      const CategoryState &category_state = job_->GetCategoryState(category);
      if (category_state.phase == CategoryPhase::kPending &&
          job_->StartCategory(category)) {
        job_->FailCategory(category, "source_missing");
        NotifyObservers();
      }
    }
    return;
  }
  for (size_t index = 0; index < state.selected_categories.size(); ++index) {
    const DataCategory category = state.selected_categories[index];
    if (state.category_states[index].phase != CategoryPhase::kPending) {
      continue;
    }
    const uint16_t item = LegacyImportItem(category);
    if (!item || !job_->StartCategory(category)) {
      return;
    }
    job_->AdvanceCategory(category, CategoryPhase::kReading);
    active_legacy_category_ = category;
    active_legacy_result_ = CategoryResult();
    legacy_item_ended_ = false;
    NotifyObservers();
    legacy_importer_host_ = new ExternalProcessImporterHost();
    legacy_importer_host_->set_observer(this);
    active_legacy_writer_ = base::MakeRefCounted<DaoLegacyProfileWriter>(
        profile_, target_.get(), &active_legacy_result_,
        l10n_util::GetStringUTF16(IDS_DAO_IMPORT_BOOKMARK_ROOT));
    legacy_importer_host_->StartImportSettings(
        *active_legacy_source_, profile_, item, active_legacy_writer_.get());
    return;
  }
  NotifyObservers();
}

void DaoMigrationService::OnChromiumSourcesDetected(
    uint64_t generation, std::vector<SourceProfile> profiles) {
  if (generation != source_detection_generation_) {
    return;
  }
  pending_detected_sources_.insert(pending_detected_sources_.begin(),
                                   std::make_move_iterator(profiles.begin()),
                                   std::make_move_iterator(profiles.end()));
  chromium_detection_done_ = true;
  MaybeFinishSourceDetection();
}

void DaoMigrationService::OnLegacySourcesDetected(uint64_t generation) {
  if (generation != source_detection_generation_) {
    return;
  }
  if (importer_list_) {
    for (size_t index = 0; index < importer_list_->count(); ++index) {
      const user_data_importer::SourceProfile &legacy =
          importer_list_->GetSourceProfileAt(index);
      std::optional<SourceKind> kind = LegacySourceKind(legacy.importer_type);
      if (!kind) {
        continue;
      }
      SourceProfile source;
      source.id = LegacySourceId(legacy);
      source.kind = *kind;
      source.browser_name = base::UTF16ToUTF8(legacy.importer_name);
      source.profile_name = legacy.profile.empty()
                                ? source.browser_name
                                : base::UTF16ToUTF8(legacy.profile);
      source.supported_categories = LegacyCategories(legacy.services_supported);
      if (source.supported_categories.empty()) {
        continue;
      }
      pending_legacy_sources_.emplace(source.id, legacy);
      pending_detected_sources_.push_back(std::move(source));
    }
  }
  legacy_detection_done_ = true;
  MaybeFinishSourceDetection();
}

void DaoMigrationService::MaybeFinishSourceDetection() {
  if (!chromium_detection_done_ || !legacy_detection_done_ ||
      pending_sources_callback_.is_null()) {
    return;
  }
  detected_sources_ = pending_detected_sources_;
  legacy_sources_ = pending_legacy_sources_;
  std::move(pending_sources_callback_).Run(detected_sources_);
}

void DaoMigrationService::OnCountSnapshotReady(DataCategory category,
                                               CountCallback callback,
                                               SnapshotResult snapshot) {
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce(&DaoMigrationService::CountSnapshotCandidates, category,
                     std::move(snapshot)),
      std::move(callback));
}

void DaoMigrationService::OnSnapshotReady(DataCategory category,
                                          SnapshotResult snapshot) {
  snapshot_cancellation_.reset();
  if (!job_ || job_->IsTerminal()) {
    return;
  }
  if (!snapshot.success) {
    if (category == DataCategory::kTabs &&
        snapshot.error_code == "source_missing") {
      job_->AdvanceCategory(category, CategoryPhase::kReading);
      job_->AdvanceCategory(category, CategoryPhase::kWriting);
      job_->CompleteCategory(category, CategoryResult());
    } else {
      job_->FailCategory(category, std::move(snapshot.error_code));
    }
    NotifyObservers();
    ProcessNextCategory();
    return;
  }
  if (!job_->AdvanceCategory(category, CategoryPhase::kReading)) {
    return;
  }
  NotifyObservers();
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE,
      {base::MayBlock(), base::TaskPriority::USER_VISIBLE,
       base::TaskShutdownBehavior::CONTINUE_ON_SHUTDOWN},
      base::BindOnce(&DaoMigrationService::ReadSnapshot, category,
                     job_->source().kind, snapshot.path),
      base::BindOnce(&DaoMigrationService::OnReadComplete,
                     weak_ptr_factory_.GetWeakPtr(), category,
                     std::move(snapshot)));
}

void DaoMigrationService::OnReadComplete(DataCategory category, SnapshotResult,
                                         ReadResult result) {
  if (!job_ || job_->IsTerminal()) {
    return;
  }
  if (!result.success) {
    job_->FailCategory(category, std::move(result.error_code));
    NotifyObservers();
    ProcessNextCategory();
    return;
  }
  BeginWriting(category, std::move(result));
}

void DaoMigrationService::BeginWriting(DataCategory category,
                                       ReadResult result) {
  if (!job_->AdvanceCategory(category, CategoryPhase::kWriting)) {
    return;
  }
  const size_t total = std::visit(
      [](const auto &records) { return records.size(); }, result.records);
  job_->UpdateProgress(category, 0, total);
  active_write_ = std::move(result);
  active_write_category_ = category;
  active_write_index_ = 0;
  active_write_totals_ = CategoryResult();
  active_tab_folder_id_.clear();
  waiting_for_extension_installs_ = false;
  NotifyObservers();
  WriteNextBatch();
}

void DaoMigrationService::WriteNextBatch() {
  if (!job_ || !active_write_ || !active_write_category_) {
    return;
  }
  const DataCategory category = *active_write_category_;
  if (job_->cancel_requested()) {
    FinishActiveTabFolder();
    job_->CancelRunningCategoryAtBatchBoundary(active_write_totals_);
    active_write_.reset();
    active_write_category_.reset();
    NotifyObservers();
    return;
  }

  const size_t total =
      std::visit([](const auto &records) { return records.size(); },
                 active_write_->records);
  if (active_write_index_ >= total) {
    if (category == DataCategory::kExtensions) {
      if (!waiting_for_extension_installs_) {
        waiting_for_extension_installs_ = true;
        target_->FinishExtensionInstalls(
            base::BindOnce(&DaoMigrationService::OnExtensionInstallsFinished,
                           weak_ptr_factory_.GetWeakPtr()));
      }
      return;
    }
    FinishActiveTabFolder();
    if (active_write_totals_.failed > 0) {
      job_->FailCategory(category, "destination_write_failed",
                         active_write_totals_);
    } else {
      job_->CompleteCategory(category, active_write_totals_);
    }
    active_write_.reset();
    active_write_category_.reset();
    NotifyObservers();
    ProcessNextCategory();
    return;
  }

  DaoMigrationWriter writer(target_.get());
  WriteResult write_result;
  constexpr size_t kBatchSize = 200;
  const size_t batch_size = std::min(kBatchSize, total - active_write_index_);
  const size_t end = active_write_index_ + batch_size;
  auto batch = [this, end](const auto &records) {
    using Record = typename std::decay_t<decltype(records)>::value_type;
    return std::vector<Record>(records.begin() + active_write_index_,
                               records.begin() + end);
  };
  switch (category) {
  case DataCategory::kBookmarks:
    write_result = writer.WriteBookmarks(
        batch(std::get<std::vector<BookmarkEntry>>(active_write_->records)),
        l10n_util::GetStringUTF16(IDS_DAO_IMPORT_BOOKMARK_ROOT));
    break;
  case DataCategory::kHistory:
    writer.WriteHistory(
        batch(std::get<std::vector<HistoryVisit>>(active_write_->records)),
        base::BindOnce(&DaoMigrationService::OnWriteBatchFinished,
                       weak_ptr_factory_.GetWeakPtr(), end));
    return;
  case DataCategory::kPasswords:
    writer.WritePasswords(
        batch(std::get<std::vector<PasswordEntry>>(active_write_->records)),
        base::BindOnce(&DaoMigrationService::OnWriteBatchFinished,
                       weak_ptr_factory_.GetWeakPtr(), end));
    return;
  case DataCategory::kTabs:
    write_result = writer.WriteTabsBatch(
        batch(std::get<std::vector<TabEntry>>(active_write_->records)),
        l10n_util::GetStringUTF16(IDS_DAO_IMPORT_TAB_FOLDER),
        &active_tab_folder_id_);
    break;
  case DataCategory::kExtensions:
    write_result = writer.WriteExtensions(
        batch(std::get<std::vector<ExtensionEntry>>(active_write_->records)));
    break;
  }
  OnWriteBatchFinished(end, write_result);
}

void DaoMigrationService::OnWriteBatchFinished(size_t end,
                                               WriteResult write_result) {
  if (!job_ || job_->IsTerminal() || !active_write_ ||
      !active_write_category_) {
    return;
  }
  AddWriteResult(&active_write_totals_, write_result);
  active_write_index_ = end;
  const size_t total =
      std::visit([](const auto &records) { return records.size(); },
                 active_write_->records);
  job_->UpdateProgress(*active_write_category_, active_write_index_, total);
  NotifyObservers();
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&DaoMigrationService::WriteNextBatch,
                                weak_ptr_factory_.GetWeakPtr()));
}

void DaoMigrationService::FinishActiveTabFolder() {
  if (!target_ || active_tab_folder_id_.empty()) {
    return;
  }
  DaoMigrationWriter writer(target_.get());
  if (active_write_totals_.imported == 0) {
    target_->AbortImportedTabFolder(active_tab_folder_id_);
  } else if (!writer.FinishTabs(active_tab_folder_id_)) {
    active_write_totals_.failed += active_write_totals_.imported;
    active_write_totals_.imported = 0;
  }
  active_tab_folder_id_.clear();
}

void DaoMigrationService::OnExtensionInstallsFinished(uint64_t installed,
                                                      uint64_t failed) {
  waiting_for_extension_installs_ = false;
  if (!job_ || !active_write_category_ ||
      *active_write_category_ != DataCategory::kExtensions ||
      job_->IsTerminal()) {
    return;
  }
  if (job_->cancel_requested()) {
    active_write_totals_.imported = installed;
    active_write_totals_.failed += failed;
    job_->CancelRunningCategoryAtBatchBoundary(active_write_totals_);
  } else {
    active_write_totals_.imported = installed;
    active_write_totals_.failed += failed;
    if (failed > 0) {
      job_->FailCategory(DataCategory::kExtensions, "extension_install_failed",
                         active_write_totals_);
    } else {
      job_->CompleteCategory(DataCategory::kExtensions, active_write_totals_);
    }
  }
  active_write_.reset();
  active_write_category_.reset();
  NotifyObservers();
  ProcessNextCategory();
}

void DaoMigrationService::NotifyObservers() {
  if (job_) {
    observers_.Notify(job_->GetState());
  }
}

const SourceProfile *
DaoMigrationService::FindDetectedSource(const std::string &source_id) const {
  auto it = std::ranges::find(detected_sources_, source_id, &SourceProfile::id);
  return it == detected_sources_.end() ? nullptr : &*it;
}

const user_data_importer::SourceProfile *
DaoMigrationService::FindLegacySource(const std::string &source_id) const {
  auto it = legacy_sources_.find(source_id);
  return it == legacy_sources_.end() ? nullptr : &it->second;
}

} // namespace dao::import
