// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/import/dao_migration_job.h"

#include <algorithm>
#include <utility>

#include "base/check.h"

namespace dao::import {

DaoMigrationJob::DaoMigrationJob(SourceProfile source,
                                 std::vector<DataCategory> selected_categories)
    : source_(std::move(source)),
      selected_categories_(std::move(selected_categories)) {
  for (DataCategory category : selected_categories_) {
    category_states_.try_emplace(category);
  }
}

DaoMigrationJob::~DaoMigrationJob() = default;

bool DaoMigrationJob::StartCategory(DataCategory category) {
  CategoryState* state = GetMutableCategoryState(category);
  if (!state || state->phase != CategoryPhase::kPending ||
      running_category_.has_value() || cancel_requested_ || IsTerminal()) {
    return false;
  }

  state->phase = CategoryPhase::kSnapshotting;
  running_category_ = category;
  return true;
}

bool DaoMigrationJob::AdvanceCategory(DataCategory category,
                                      CategoryPhase next_phase) {
  CategoryState* state = GetMutableCategoryState(category);
  if (!state || !IsRunningCategory(category)) {
    return false;
  }

  const bool valid_transition = (state->phase == CategoryPhase::kSnapshotting &&
                                 next_phase == CategoryPhase::kReading) ||
                                (state->phase == CategoryPhase::kReading &&
                                 next_phase == CategoryPhase::kWriting);
  if (!valid_transition) {
    return false;
  }

  state->phase = next_phase;
  return true;
}

bool DaoMigrationJob::CompleteCategory(DataCategory category,
                                       CategoryResult result) {
  CategoryState* state = GetMutableCategoryState(category);
  if (!state || !IsRunningCategory(category) ||
      state->phase != CategoryPhase::kWriting) {
    return false;
  }

  state->phase = CategoryPhase::kSucceeded;
  state->result = result;
  state->error_code.clear();
  FinishRunningCategory(category);
  return true;
}

bool DaoMigrationJob::FailCategory(DataCategory category,
                                   std::string error_code,
                                   CategoryResult result) {
  CategoryState* state = GetMutableCategoryState(category);
  if (!state || !IsRunningCategory(category) || IsTerminalPhase(state->phase)) {
    return false;
  }

  state->phase = CategoryPhase::kFailed;
  state->error_code = std::move(error_code);
  state->result = result;
  FinishRunningCategory(category);
  return true;
}

bool DaoMigrationJob::UpdateProgress(DataCategory category,
                                     uint64_t completed_items,
                                     uint64_t total_items) {
  CategoryState* state = GetMutableCategoryState(category);
  if (!state || !IsRunningCategory(category) || IsTerminalPhase(state->phase)) {
    return false;
  }

  state->total_items = total_items;
  state->indeterminate = total_items == 0;
  state->completed_items = total_items == 0
                               ? completed_items
                               : std::min(completed_items, total_items);
  return true;
}

void DaoMigrationJob::RequestCancel() {
  cancel_requested_ = true;
  for (auto& entry : category_states_) {
    CategoryState& state = entry.second;
    if (state.phase == CategoryPhase::kPending) {
      state.phase = CategoryPhase::kCancelled;
    }
  }
}

bool DaoMigrationJob::CancelRunningCategoryAtBatchBoundary(
    CategoryResult result) {
  if (!cancel_requested_ || !running_category_) {
    return false;
  }

  CategoryState* state = GetMutableCategoryState(*running_category_);
  CHECK(state);
  state->phase = CategoryPhase::kCancelled;
  state->result = result;
  running_category_.reset();
  return true;
}

const CategoryState& DaoMigrationJob::GetCategoryState(
    DataCategory category) const {
  auto it = category_states_.find(category);
  CHECK(it != category_states_.end());
  return it->second;
}

JobState DaoMigrationJob::GetState() const {
  JobState state;
  state.source_id = source_.id;
  state.cancel_requested = cancel_requested_;
  state.selected_categories = selected_categories_;
  state.category_states.reserve(selected_categories_.size());
  for (DataCategory category : selected_categories_) {
    state.category_states.push_back(GetCategoryState(category));
  }
  return state;
}

bool DaoMigrationJob::IsTerminal() const {
  return std::all_of(
      category_states_.begin(), category_states_.end(),
      [](const auto& entry) { return IsTerminalPhase(entry.second.phase); });
}

CategoryState* DaoMigrationJob::GetMutableCategoryState(DataCategory category) {
  auto it = category_states_.find(category);
  return it == category_states_.end() ? nullptr : &it->second;
}

bool DaoMigrationJob::IsRunningCategory(DataCategory category) const {
  return running_category_ && *running_category_ == category;
}

void DaoMigrationJob::FinishRunningCategory(DataCategory category) {
  CHECK(IsRunningCategory(category));
  running_category_.reset();
}

}  // namespace dao::import
