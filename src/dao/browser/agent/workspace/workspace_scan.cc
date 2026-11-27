// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/agent/workspace/workspace_scan.h"

#include <utility>
#include <vector>

#include "build/build_config.h"
#include "dao/browser/agent/workspace/workspace_file.h"

#if BUILDFLAG(IS_WIN)
#include <windows.h>

#include "base/files/file_enumerator.h"
#else
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>

#include <memory>
#endif

namespace dao {
namespace {

bool NameEquals(base::FilePath::StringViewType name,
                base::FilePath::StringViewType expected) {
#if BUILDFLAG(IS_WIN)
  return base::FilePath::CompareEqualIgnoreCase(name, expected);
#else
  return name == expected;
#endif
}

bool IsQuotaExemptRootFile(base::FilePath::StringViewType name) {
  return NameEquals(name, FILE_PATH_LITERAL("WORKSPACE.md")) ||
         NameEquals(name, FILE_PATH_LITERAL(".audit.log"));
}

bool SkipEntry(bool at_root,
               base::FilePath::StringViewType name,
               bool is_directory,
               WorkspaceScanScope scope) {
  const bool index =
      at_root && NameEquals(name, FILE_PATH_LITERAL("WORKSPACE.md"));
  if (scope == WorkspaceScanScope::kVisible) {
    return name.front() == FILE_PATH_LITERAL('.') || index;
  }
  if (!at_root) {
    return false;
  }
  // The audit and index are service-owned files. A directory occupying
  // either name still uses quota, including the files underneath it.
  return NameEquals(name, FILE_PATH_LITERAL(".workspace_tmp")) ||
         (!is_directory && IsQuotaExemptRootFile(name));
}

WorkspaceError ScanDirectory(
    const base::FilePath& root,
    const base::FilePath& directory,
    base::FunctionRef<void(const base::FilePath&, const base::File::Info&)>
        visit) {
  auto handle = OpenWorkspaceDirectory(root, directory);
#if BUILDFLAG(IS_WIN)
  if (handle.empty()) {
    const DWORD error = ::GetLastError();
    if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) {
      return WorkspaceError::kNotFound;
    }
    return error == ERROR_DIRECTORY ? WorkspaceError::kInvalidPath
                                    : WorkspaceError::kIoError;
  }
#else
  if (!handle.is_valid()) {
    if (errno == ENOENT) {
      return WorkspaceError::kNotFound;
    }
    return errno == ENOTDIR || errno == ELOOP ? WorkspaceError::kInvalidPath
                                              : WorkspaceError::kIoError;
  }
#endif
#if BUILDFLAG(IS_WIN)
  // The open directory chain denies replacement through the end of the scan.
  base::FileEnumerator entries(
      root.Append(directory), false,
      base::FileEnumerator::FILES | base::FileEnumerator::DIRECTORIES, {},
      base::FileEnumerator::FolderSearchPolicy::ALL,
      base::FileEnumerator::ErrorPolicy::STOP_ENUMERATION);
  for (auto path = entries.Next(); !path.empty(); path = entries.Next()) {
    auto entry = entries.GetInfo();
    const auto name = entry.GetName();
    if (entry.find_data().dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
      continue;
    }
    base::File::Info info;
    info.is_directory = entry.IsDirectory();
    info.size = entry.GetSize();
    info.last_modified = entry.GetLastModifiedTime();
    visit(directory.Append(name), info);
  }
  // An empty directory may have no matches for FindFirstFile.
  return entries.GetError() == base::File::FILE_OK ||
                 entries.GetError() == base::File::FILE_ERROR_NOT_FOUND
             ? WorkspaceError::kOk
             : WorkspaceError::kIoError;
#else
  std::unique_ptr<DIR, decltype(&closedir)> entries(fdopendir(handle.get()),
                                                    &closedir);
  if (!entries) {
    return WorkspaceError::kIoError;
  }
  (void)handle.release();  // closedir() now owns this descriptor.
  for (;;) {
    errno = 0;
    const auto* entry = readdir(entries.get());
    if (!entry) {
      return errno == 0 ? WorkspaceError::kOk : WorkspaceError::kIoError;
    }
    const base::FilePath::StringViewType name(entry->d_name);
    if (name == "." || name == "..") {
      continue;
    }
    struct stat st;
    if (fstatat(dirfd(entries.get()), entry->d_name, &st,
                AT_SYMLINK_NOFOLLOW) != 0) {
      if (errno != ENOENT) {
        return WorkspaceError::kIoError;
      }
      continue;
    }
    if (!(S_ISDIR(st.st_mode) || S_ISREG(st.st_mode))) {
      continue;
    }
    base::File::Info info;
    info.FromStat(st);
    visit(directory.Append(name), info);
  }
#endif
}

}  // namespace

bool IsWorkspaceQuotaExemptFile(const base::FilePath& relative) {
  const auto parts = relative.GetComponents();
  return !parts.empty() &&
         (NameEquals(parts.front(), FILE_PATH_LITERAL(".workspace_tmp")) ||
          (parts.size() == 1 && IsQuotaExemptRootFile(parts.front())));
}

void RecoverWorkspacePublications(const base::FilePath& root) {
  std::vector<base::FilePath> pending(1);
  while (!pending.empty()) {
    auto current = std::move(pending.back());
    pending.pop_back();
    ScanDirectory(
        root, current,
        [&](const base::FilePath& path, const base::File::Info& info) {
          if (!info.is_directory) {
            return;
          }
          const auto name = path.BaseName();
          const bool legacy =
              current.empty() &&
              name.value() == FILE_PATH_LITERAL(".workspace_tmp");
          if (!legacy && name.value() != kWorkspacePublishDirectory) {
            if (name.value().front() != FILE_PATH_LITERAL('.')) {
              pending.push_back(path);
            }
            return;
          }
          bool owned_directory = false;
          ScanDirectory(root, path,
                        [&](const base::FilePath& file,
                            const base::File::Info& file_info) {
                          if (!file_info.is_directory &&
                              RecoverWorkspacePublication(root, file, legacy)) {
                            owned_directory = true;
                          }
                        });
          if (owned_directory) {
            RemoveEmptyWorkspacePublishDirectory(root, path);
          }
        });
  }
}

WorkspaceError ScanWorkspace(
    const base::FilePath& root,
    const base::FilePath& directory,
    bool recursive,
    WorkspaceScanScope scope,
    base::FunctionRef<void(const base::FilePath&, const base::File::Info&)>
        visit) {
  const auto components = directory.GetComponents();
  for (size_t i = 0; i < components.size(); ++i) {
    if (SkipEntry(i == 0, components[i], /*is_directory=*/true, scope)) {
      return WorkspaceError::kInvalidPath;
    }
  }
  std::vector<base::FilePath> pending = {directory};
  while (!pending.empty()) {
    const auto current = std::move(pending.back());
    pending.pop_back();
    const auto status = ScanDirectory(
        root, current,
        [&](const base::FilePath& path, const base::File::Info& info) {
          if (SkipEntry(current.empty(), path.BaseName().value(),
                        info.is_directory, scope)) {
            return;
          }
          visit(path, info);
          if (recursive && info.is_directory) {
            pending.push_back(path);
          }
        });
    // Descendants may disappear or become links after their parent was read.
    if (status != WorkspaceError::kOk &&
        (current == directory || status == WorkspaceError::kIoError)) {
      return status;
    }
  }
  return WorkspaceError::kOk;
}

}  // namespace dao
