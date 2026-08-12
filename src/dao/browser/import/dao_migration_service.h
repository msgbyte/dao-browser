// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_IMPORT_DAO_MIGRATION_SERVICE_H_
#define DAO_BROWSER_IMPORT_DAO_MIGRATION_SERVICE_H_

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "base/callback_list.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/importer/importer_progress_observer.h"
#include "components/keyed_service/core/keyed_service.h"
#include "components/password_manager/core/browser/password_store/password_store_consumer.h"
#include "components/user_data_importer/common/importer_data_types.h"
#include "dao/browser/import/dao_migration_job.h"
#include "dao/browser/import/dao_profile_snapshot.h"
#include "dao/browser/import/dao_source_adapter.h"
#include "dao/browser/import/dao_source_detector.h"

class Profile;
class ExternalProcessImporterHost;
class ImporterList;

namespace dao::import {

class DaoChromiumMigrationTarget;
class DaoLegacyProfileWriter;
struct WriteResult;

class DaoMigrationService : public KeyedService,
                            public password_manager::PasswordStoreConsumer,
                            public importer::ImporterProgressObserver {
public:
  using SourcesCallback = base::OnceCallback<void(std::vector<SourceProfile>)>;
  using CountCallback = base::OnceCallback<void(std::optional<uint64_t>)>;
  using StateObserver = base::RepeatingCallback<void(const JobState &)>;

  explicit DaoMigrationService(Profile *profile);
  DaoMigrationService(const DaoMigrationService &) = delete;
  DaoMigrationService &operator=(const DaoMigrationService &) = delete;
  ~DaoMigrationService() override;

  void DetectSources(SourcesCallback callback);
  void CountSourceItems(const std::string &source_id, DataCategory category,
                        CountCallback callback);
  bool Start(const std::string &source_id,
             std::vector<DataCategory> categories);
  bool Retry(std::vector<DataCategory> categories);
  void Cancel();
  std::optional<JobState> GetState() const;
  base::CallbackListSubscription AddObserver(StateObserver observer);

  void Shutdown() override;

  // password_manager::PasswordStoreConsumer:
  void OnGetPasswordStoreResultsOrErrorFrom(
      password_manager::PasswordStoreInterface *store,
      password_manager::LoginsResultOrError results_or_error) override;

  // importer::ImporterProgressObserver:
  void ImportStarted() override;
  void ImportItemStarted(user_data_importer::ImportItem item) override;
  void ImportItemEnded(user_data_importer::ImportItem item) override;
  void ImportEnded() override;

private:
  using Records =
      std::variant<std::vector<BookmarkEntry>, std::vector<HistoryVisit>,
                   std::vector<PasswordEntry>, std::vector<TabEntry>,
                   std::vector<ExtensionEntry>>;

  struct ReadResult {
    ReadResult();
    ReadResult(const ReadResult &);
    ReadResult &operator=(const ReadResult &);
    ReadResult(ReadResult &&);
    ReadResult &operator=(ReadResult &&);
    ~ReadResult();

    bool success = false;
    std::string error_code;
    Records records = std::vector<BookmarkEntry>();
  };

  static ReadResult ReadSnapshot(DataCategory category, SourceKind source_kind,
                                 base::FilePath snapshot_path);
  static std::optional<uint64_t>
  CountProfileCandidates(DataCategory category, base::FilePath profile_path);
  static std::optional<uint64_t>
  CountSnapshotCandidates(DataCategory category, SnapshotResult snapshot);
  SnapshotRequest BuildSnapshotRequest(DataCategory category,
                                       const base::FilePath &source_path);
  void ProcessNextCategory();
  void ProcessNextLegacyCategory();
  void OnChromiumSourcesDetected(uint64_t generation,
                                 std::vector<SourceProfile> profiles);
  void OnLegacySourcesDetected(uint64_t generation);
  void MaybeFinishSourceDetection();
  void OnSnapshotReady(DataCategory category, SnapshotResult snapshot);
  void OnCountSnapshotReady(DataCategory category, CountCallback callback,
                            SnapshotResult snapshot);
  void OnReadComplete(DataCategory category, SnapshotResult snapshot,
                      ReadResult result);
  void BeginWriting(DataCategory category, ReadResult result);
  void WriteNextBatch();
  void OnWriteBatchFinished(size_t end, WriteResult result);
  void FinishActiveTabFolder();
  void OnLegacyWritesFinished();
  void OnExtensionInstallsFinished(uint64_t installed, uint64_t failed);
  void NotifyObservers();
  const SourceProfile *FindDetectedSource(const std::string &source_id) const;
  const user_data_importer::SourceProfile *
  FindLegacySource(const std::string &source_id) const;

  raw_ptr<Profile> profile_;
  DaoSourceDetector detector_;
  std::unique_ptr<ImporterList> importer_list_;
  std::vector<SourceProfile> detected_sources_;
  std::map<std::string, user_data_importer::SourceProfile> legacy_sources_;
  std::vector<SourceProfile> pending_detected_sources_;
  std::map<std::string, user_data_importer::SourceProfile>
      pending_legacy_sources_;
  SourcesCallback pending_sources_callback_;
  uint64_t source_detection_generation_ = 0;
  bool chromium_detection_done_ = false;
  bool legacy_detection_done_ = false;
  std::unique_ptr<DaoMigrationJob> job_;
  std::optional<base::FilePath> active_chromium_source_path_;
  std::optional<user_data_importer::SourceProfile> active_legacy_source_;
  std::unique_ptr<DaoChromiumMigrationTarget> target_;
  scoped_refptr<SnapshotCancellationFlag> snapshot_cancellation_;
  base::RepeatingCallbackList<void(const JobState &)> observers_;
  std::map<std::pair<std::string, std::u16string>, std::u16string>
      existing_passwords_;
  bool passwords_ready_ = false;
  bool waiting_for_passwords_ = false;
  std::optional<ReadResult> active_write_;
  std::optional<DataCategory> active_write_category_;
  size_t active_write_index_ = 0;
  CategoryResult active_write_totals_;
  std::string active_tab_folder_id_;
  bool waiting_for_extension_installs_ = false;
  raw_ptr<ExternalProcessImporterHost> legacy_importer_host_ = nullptr;
  scoped_refptr<DaoLegacyProfileWriter> active_legacy_writer_;
  std::optional<DataCategory> active_legacy_category_;
  CategoryResult active_legacy_result_;
  bool legacy_item_ended_ = false;
  base::WeakPtrFactory<DaoMigrationService> weak_ptr_factory_{this};
};

} // namespace dao::import

#endif // DAO_BROWSER_IMPORT_DAO_MIGRATION_SERVICE_H_
