// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/ui/webui/dao_pinned_tab_storage.h"

#include "base/files/file_util.h"
#include "base/no_destructor.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/thread_pool.h"

namespace dao {

bool WritePinnedTabsFileAtomically(const base::FilePath& file_path,
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

scoped_refptr<base::SequencedTaskRunner> GetPinnedTabsFileTaskRunner() {
  static base::NoDestructor<scoped_refptr<base::SequencedTaskRunner>>
      task_runner(base::ThreadPool::CreateSequencedTaskRunner(
          {base::MayBlock(), base::TaskPriority::USER_VISIBLE}));
  return *task_runner;
}

}  // namespace dao
