// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_IMPORT_DAO_MIGRATION_JOB_H_
#define DAO_BROWSER_IMPORT_DAO_MIGRATION_JOB_H_

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "dao/browser/import/dao_migration_types.h"

namespace dao::import {

class DaoMigrationJob {
 public:
  DaoMigrationJob(SourceProfile source,
                  std::vector<DataCategory> selected_categories);
  DaoMigrationJob(const DaoMigrationJob&) = delete;
  DaoMigrationJob& operator=(const DaoMigrationJob&) = delete;
  ~DaoMigrationJob();

  bool StartCategory(DataCategory category);
  bool AdvanceCategory(DataCategory category, CategoryPhase next_phase);
  bool CompleteCategory(DataCategory category, CategoryResult result);
  bool FailCategory(DataCategory category,
                    std::string error_code,
                    CategoryResult result = CategoryResult());
  bool UpdateProgress(DataCategory category,
                      uint64_t completed_items,
                      uint64_t total_items);

  void RequestCancel();
  bool CancelRunningCategoryAtBatchBoundary(
      CategoryResult result = CategoryResult());

  const CategoryState& GetCategoryState(DataCategory category) const;
  JobState GetState() const;
  bool IsTerminal() const;

  bool cancel_requested() const { return cancel_requested_; }
  const SourceProfile& source() const { return source_; }

 private:
  CategoryState* GetMutableCategoryState(DataCategory category);
  bool IsRunningCategory(DataCategory category) const;
  void FinishRunningCategory(DataCategory category);

  SourceProfile source_;
  std::vector<DataCategory> selected_categories_;
  std::map<DataCategory, CategoryState> category_states_;
  std::optional<DataCategory> running_category_;
  bool cancel_requested_ = false;
};

}  // namespace dao::import

#endif  // DAO_BROWSER_IMPORT_DAO_MIGRATION_JOB_H_
