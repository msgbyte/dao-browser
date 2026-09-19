// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/ui/webui/dao_folder_storage.h"

#include <algorithm>
#include <map>
#include <optional>
#include <utility>

#include "base/files/file_util.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/no_destructor.h"
#include "base/synchronization/lock.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/thread_pool.h"
#include "base/values.h"

namespace dao {
namespace {

constexpr int kFolderStorageVersion = 2;
constexpr char kLegacySnapshotId[] = "legacy";

struct FolderFileWriteState {
  base::Lock lock;
  uint64_t next_generation = 0;
  std::map<base::FilePath::StringType, uint64_t> generations;
  std::map<base::FilePath::StringType, std::string> pending_writes;
};

FolderFileWriteState& GetFolderFileWriteState() {
  static base::NoDestructor<FolderFileWriteState> state;
  return *state;
}

bool ReplaceFolderFileAtomically(const base::FilePath& file_path,
                                 const std::string& data) {
  base::FilePath temporary_path;
  if (!base::CreateTemporaryFileInDir(file_path.DirName(), &temporary_path)) {
    return false;
  }
  if (!base::WriteFile(temporary_path, data) ||
      !base::ReplaceFile(temporary_path, file_path, nullptr)) {
    base::DeleteFile(temporary_path);
    return false;
  }
  return true;
}

void CollectLegacyTabIds(const base::ListValue& items,
                         std::set<std::string>* tab_ids) {
  for (const base::Value& item_value : items) {
    const base::DictValue* item = item_value.GetIfDict();
    if (!item) {
      continue;
    }
    if (const std::string* tab_id = item->FindString("tabId")) {
      tab_ids->insert(*tab_id);
    }
    if (const base::ListValue* children = item->FindList("children")) {
      CollectLegacyTabIds(*children, tab_ids);
    }
  }
}

}  // namespace

DaoFolderWindowSnapshot::DaoFolderWindowSnapshot() = default;
DaoFolderWindowSnapshot::~DaoFolderWindowSnapshot() = default;
DaoFolderWindowSnapshot::DaoFolderWindowSnapshot(
    const DaoFolderWindowSnapshot&) = default;
DaoFolderWindowSnapshot& DaoFolderWindowSnapshot::operator=(
    const DaoFolderWindowSnapshot&) = default;
DaoFolderWindowSnapshot::DaoFolderWindowSnapshot(DaoFolderWindowSnapshot&&) =
    default;
DaoFolderWindowSnapshot& DaoFolderWindowSnapshot::operator=(
    DaoFolderWindowSnapshot&&) = default;

DaoFolderStorage::DaoFolderStorage() = default;
DaoFolderStorage::~DaoFolderStorage() = default;
DaoFolderStorage::DaoFolderStorage(const DaoFolderStorage&) = default;
DaoFolderStorage& DaoFolderStorage::operator=(const DaoFolderStorage&) =
    default;
DaoFolderStorage::DaoFolderStorage(DaoFolderStorage&&) = default;
DaoFolderStorage& DaoFolderStorage::operator=(DaoFolderStorage&&) = default;

bool DaoFolderStorage::LoadFromJson(const std::string& json) {
  snapshots_.clear();
  if (json.empty()) {
    return true;
  }

  std::optional<base::DictValue> root =
      base::JSONReader::ReadDict(json, base::JSON_PARSE_RFC);
  if (!root) {
    return false;
  }

  if (root->FindInt("version").value_or(0) != kFolderStorageVersion) {
    const base::ListValue* items = root->FindList("items");
    if (!items) {
      return false;
    }
    DaoFolderWindowSnapshot snapshot;
    snapshot.id = kLegacySnapshotId;
    snapshot.json = json;
    snapshot.legacy = true;
    CollectLegacyTabIds(*items, &snapshot.tab_ids);
    snapshots_.push_back(std::move(snapshot));
    return true;
  }

  const base::ListValue* windows = root->FindList("windows");
  if (!windows) {
    return false;
  }
  std::set<std::string> ids;
  for (const base::Value& window_value : *windows) {
    const base::DictValue* window = window_value.GetIfDict();
    const std::string* id = window ? window->FindString("id") : nullptr;
    const std::string* data = window ? window->FindString("data") : nullptr;
    const base::ListValue* tab_ids =
        window ? window->FindList("tabIds") : nullptr;
    if (!id || id->empty() || !data || !tab_ids || !ids.insert(*id).second) {
      snapshots_.clear();
      return false;
    }

    DaoFolderWindowSnapshot snapshot;
    snapshot.id = *id;
    snapshot.json = *data;
    snapshot.legacy = window->FindBool("legacy").value_or(false);
    for (const base::Value& tab_id_value : *tab_ids) {
      const std::string* tab_id = tab_id_value.GetIfString();
      if (!tab_id) {
        snapshots_.clear();
        return false;
      }
      snapshot.tab_ids.insert(*tab_id);
    }
    snapshots_.push_back(std::move(snapshot));
  }
  return true;
}

std::string DaoFolderStorage::ToJson() const {
  base::DictValue root;
  root.Set("version", kFolderStorageVersion);
  base::ListValue windows;
  for (const DaoFolderWindowSnapshot& snapshot : snapshots_) {
    base::DictValue window;
    window.Set("id", snapshot.id);
    base::ListValue tab_ids;
    for (const std::string& tab_id : snapshot.tab_ids) {
      tab_ids.Append(tab_id);
    }
    window.Set("tabIds", std::move(tab_ids));
    window.Set("data", snapshot.json);
    window.Set("legacy", snapshot.legacy);
    windows.Append(std::move(window));
  }
  root.Set("windows", std::move(windows));

  std::string json;
  base::JSONWriter::WriteWithOptions(
      root, base::JSONWriter::OPTIONS_PRETTY_PRINT, &json);
  return json;
}

const DaoFolderWindowSnapshot* DaoFolderStorage::FindById(
    const std::string& id) const {
  auto it = std::ranges::find(snapshots_, id, &DaoFolderWindowSnapshot::id);
  return it == snapshots_.end() ? nullptr : &*it;
}

const DaoFolderWindowSnapshot* DaoFolderStorage::FindClaimableSnapshot(
    const std::vector<std::string>& preferred_ids,
    const std::set<std::string>& current_tab_ids,
    const std::set<std::string>& claimed_ids) const {
  for (const std::string& id : preferred_ids) {
    const DaoFolderWindowSnapshot* snapshot = FindById(id);
    if (snapshot && !claimed_ids.contains(snapshot->id)) {
      return snapshot;
    }
  }

  if (preferred_ids.empty()) {
    const DaoFolderWindowSnapshot* best = nullptr;
    size_t best_overlap = 0;
    for (const DaoFolderWindowSnapshot& snapshot : snapshots_) {
      if (snapshot.legacy || claimed_ids.contains(snapshot.id)) {
        continue;
      }
      size_t overlap = 0;
      for (const std::string& tab_id : current_tab_ids) {
        overlap += snapshot.tab_ids.contains(tab_id) ? 1 : 0;
      }
      if (overlap > best_overlap) {
        best = &snapshot;
        best_overlap = overlap;
      }
    }
    if (best) {
      return best;
    }
  }

  for (const DaoFolderWindowSnapshot& snapshot : snapshots_) {
    if (snapshot.legacy && !claimed_ids.contains(snapshot.id)) {
      return &snapshot;
    }
  }
  return nullptr;
}

void DaoFolderStorage::UpsertSnapshot(const std::string& id,
                                      std::set<std::string> tab_ids,
                                      std::string json) {
  auto it = std::ranges::find(snapshots_, id, &DaoFolderWindowSnapshot::id);
  if (it == snapshots_.end()) {
    DaoFolderWindowSnapshot snapshot;
    snapshot.id = id;
    snapshot.tab_ids = std::move(tab_ids);
    snapshot.json = std::move(json);
    snapshots_.push_back(std::move(snapshot));
    return;
  }
  it->tab_ids = std::move(tab_ids);
  it->json = std::move(json);
  it->legacy = false;
}

void DaoFolderStorage::RemoveSnapshot(const std::string& id) {
  std::erase_if(snapshots_, [&id](const DaoFolderWindowSnapshot& snapshot) {
    return snapshot.id == id;
  });
}

bool WriteFolderFileAtomically(const base::FilePath& file_path,
                               const std::string& data) {
  FolderFileWriteState& state = GetFolderFileWriteState();
  base::AutoLock lock(state.lock);
  if (!ReplaceFolderFileAtomically(file_path, data)) {
    return false;
  }
  state.generations[file_path.value()] = ++state.next_generation;
  state.pending_writes.erase(file_path.value());
  return true;
}

bool ReadFolderFileWithPendingWrite(const base::FilePath& file_path,
                                    std::string* data) {
  FolderFileWriteState& state = GetFolderFileWriteState();
  base::AutoLock lock(state.lock);
  auto pending_it = state.pending_writes.find(file_path.value());
  if (pending_it != state.pending_writes.end()) {
    *data = pending_it->second;
    return true;
  }
  if (!base::PathExists(file_path)) {
    data->clear();
    return true;
  }
  return base::ReadFileToString(file_path, data);
}

uint64_t ReserveFolderFileWrite(const base::FilePath& file_path,
                                const std::string& data) {
  FolderFileWriteState& state = GetFolderFileWriteState();
  base::AutoLock lock(state.lock);
  const uint64_t generation = ++state.next_generation;
  state.generations[file_path.value()] = generation;
  state.pending_writes[file_path.value()] = data;
  return generation;
}

bool WriteReservedFolderFileAtomically(const base::FilePath& file_path,
                                       const std::string& data,
                                       uint64_t generation) {
  // ponytail: Folder snapshots are small profile-local writes. Keep the
  // generation check and replace under one lock; use per-path locks if this
  // ever becomes a measurable contention point.
  FolderFileWriteState& state = GetFolderFileWriteState();
  base::AutoLock lock(state.lock);
  auto generation_it = state.generations.find(file_path.value());
  if (generation_it == state.generations.end() ||
      generation_it->second != generation) {
    return true;
  }

  if (!ReplaceFolderFileAtomically(file_path, data)) {
    return false;
  }
  state.pending_writes.erase(file_path.value());
  return true;
}

scoped_refptr<base::SequencedTaskRunner> GetFolderFileTaskRunner() {
  static base::NoDestructor<scoped_refptr<base::SequencedTaskRunner>>
      task_runner(base::ThreadPool::CreateSequencedTaskRunner(
          {base::MayBlock(), base::TaskPriority::USER_VISIBLE}));
  return *task_runner;
}

}  // namespace dao
