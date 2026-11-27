// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/agent/workspace/workspace_file.h"

#include <memory>
#include <string>
#include <utility>

#include "base/containers/span.h"
#include "base/files/file_util.h"
#include "base/functional/function_ref.h"
#include "base/uuid.h"
#include "third_party/abseil-cpp/absl/cleanup/cleanup.h"

#if BUILDFLAG(IS_WIN)
#include <windows.h>
#else
#include <fcntl.h>
#include <stdio.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

#include "base/posix/eintr_wrapper.h"
#endif

namespace dao {
namespace {

bool SafeRelative(const base::FilePath& relative) {
  if (relative.IsAbsolute() || relative.ReferencesParent()) {
    return false;
  }
#if BUILDFLAG(IS_WIN)
  // Drive-relative names, alternate streams and Win32 device names cannot
  // be treated as ordinary children of a pinned directory.
  for (const auto& part : relative.GetComponents()) {
    if (part.find_first_of(L":/\\") != std::wstring::npos ||
        base::IsReservedNameOnWindows(part)) {
      return false;
    }
  }
#endif
  return true;
}

bool IsFilePath(const base::FilePath& relative) {
  return !relative.empty() && SafeRelative(relative) &&
         relative.BaseName().value() != base::FilePath::kCurrentDirectory;
}

#if BUILDFLAG(IS_WIN)
base::File OpenRegular(const base::FilePath& path, bool append) {
  base::win::ScopedHandle handle(::CreateFileW(
      path.value().c_str(),
      append ? FILE_APPEND_DATA | FILE_READ_ATTRIBUTES : GENERIC_READ,
      FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr,
      append ? OPEN_ALWAYS : OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT,
      nullptr));
  if (!handle.is_valid()) {
    return base::File(base::File::GetLastFileError());
  }
  BY_HANDLE_FILE_INFORMATION info{};
  if (!::GetFileInformationByHandle(handle.get(), &info) ||
      ::GetFileType(handle.get()) != FILE_TYPE_DISK ||
      (info.dwFileAttributes &
       (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) ||
      (append && info.nNumberOfLinks != 1)) {
    return base::File(base::File::FILE_ERROR_SECURITY);
  }
  return base::File(std::move(handle));
}
#else
base::File OpenRegular(int dir, const base::FilePath& name, bool append) {
  const int access = append ? O_WRONLY | O_APPEND | O_CREAT : O_RDONLY;
  base::ScopedFD fd(
      HANDLE_EINTR(openat(dir, name.value().c_str(),
                          access | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC, 0600)));
  if (!fd.is_valid()) {
    return base::File(base::File::GetLastFileError());
  }
  struct stat info{};
  if (fstat(fd.get(), &info) != 0 || !S_ISREG(info.st_mode) ||
      (append && info.st_nlink != 1)) {
    return base::File(base::File::FILE_ERROR_SECURITY);
  }
  return base::File(std::move(fd));
}
#endif

using DirectoryHandle = decltype(OpenWorkspaceDirectory({}, {}));

#if BUILDFLAG(IS_WIN)
base::PlatformFile LastHandle(const DirectoryHandle& directory) {
  return directory.empty() ? INVALID_HANDLE_VALUE
                           : directory.back().GetPlatformFile();
}
#else
base::PlatformFile LastHandle(const DirectoryHandle& directory) {
  return directory.get();
}
#endif

std::string FileId(base::PlatformFile handle) {
#if BUILDFLAG(IS_WIN)
  BY_HANDLE_FILE_INFORMATION info{};
  if (!::GetFileInformationByHandle(handle, &info)) {
    return {};
  }
  return std::to_string(info.dwVolumeSerialNumber) + "-" +
         std::to_string((uint64_t{info.nFileIndexHigh} << 32) |
                        info.nFileIndexLow);
#else
  struct stat info{};
  if (fstat(handle, &info) != 0) {
    return {};
  }
  return std::to_string(static_cast<uint64_t>(info.st_dev)) + "-" +
         std::to_string(info.st_ino);
#endif
}

bool IsV4(std::string_view name) {
  return name.size() == 36 && name[14] == '4' &&
         std::string_view("89ab").find(name[19]) != std::string_view::npos &&
         base::Uuid::ParseLowercase(name).is_valid();
}

#if BUILDFLAG(IS_WIN)
bool RenameOpenFile(base::File& file,
                    const base::FilePath& name,
                    bool replace) {
  const DWORD name_bytes =
      static_cast<DWORD>(name.value().size() * sizeof(wchar_t));
  const size_t size = sizeof(FILE_RENAME_INFO) + name_bytes;
  auto buffer = std::make_unique<char[]>(size);
  auto* info = reinterpret_cast<FILE_RENAME_INFO*>(buffer.get());
  info->ReplaceIfExists = replace;
  info->RootDirectory = nullptr;
  info->FileNameLength = name_bytes;
  UNSAFE_TODO(memcpy(info->FileName, name.value().data(), name_bytes));
  return ::SetFileInformationByHandle(file.GetPlatformFile(), FileRenameInfo,
                                      info, static_cast<DWORD>(size));
}

void DiscardOpenFile(base::File& file) {
  FILE_DISPOSITION_INFO info{TRUE};
  ::SetFileInformationByHandle(file.GetPlatformFile(), FileDispositionInfo,
                               &info, sizeof(info));
}
#else
int RenameExclusive(int from_directory,
                    const base::FilePath& from,
                    int to_directory,
                    const base::FilePath& to) {
#if BUILDFLAG(IS_APPLE)
  return renameatx_np(from_directory, from.value().c_str(), to_directory,
                      to.value().c_str(), RENAME_EXCL);
#else
  return renameat2(from_directory, from.value().c_str(), to_directory,
                   to.value().c_str(), RENAME_NOREPLACE);
#endif
}
#endif

base::expected<void, WorkspaceError> Publish(
    const base::FilePath& root,
    const base::FilePath& relative,
    WorkspacePublishMode mode,
    base::FunctionRef<bool(base::File&)> fill) {
  const auto failed = base::unexpected(WorkspaceError::kIoError);
  if (!IsFilePath(relative)) {
    return failed;
  }
  const auto root_pin = OpenWorkspaceDirectory(root, {});
  const std::string root_id = FileId(LastHandle(root_pin));
  const auto parent_path = relative.DirName();
  const auto parent = OpenWorkspaceDirectory(root, parent_path, true);
  if (root_id.empty() || LastHandle(parent) == base::kInvalidPlatformFile) {
    return failed;
  }
  const auto staging_path = parent_path.Append(kWorkspacePublishDirectory);
#if BUILDFLAG(IS_WIN)
  bool created_directory =
      ::CreateDirectoryW(root.Append(staging_path).value().c_str(), nullptr);
  if (!created_directory && ::GetLastError() != ERROR_ALREADY_EXISTS) {
    return failed;
  }
#else
  bool created_directory =
      mkdirat(parent.get(), kWorkspacePublishDirectory, 0700) == 0;
  if (!created_directory && errno != EEXIST) {
    return failed;
  }
#endif
  absl::Cleanup cleanup_directory = [&] {
    if (created_directory) {
      RemoveEmptyWorkspacePublishDirectory(root, staging_path);
    }
  };
  const auto staging = OpenWorkspaceDirectory(root, staging_path);
  if (LastHandle(staging) == base::kInvalidPlatformFile) {
    return failed;
  }

  // Only the empty bootstrap slot lacks a file identity. A populated slot
  // always names the identity of its open file and the owning workspace.
  const std::string prefix =
      (created_directory ? "dir-" : "slot-") + root_id + ".";
  auto slot = base::FilePath::FromASCII(
      prefix + "new-" + base::Uuid::GenerateRandomV4().AsLowercaseString());
#if BUILDFLAG(IS_WIN)
  const auto full = root.Append(staging_path).Append(slot);
  base::File file(base::win::ScopedHandle(::CreateFileW(
      full.value().c_str(), GENERIC_READ | GENERIC_WRITE | DELETE, 0, nullptr,
      CREATE_NEW, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT,
      nullptr)));
#else
  int flags = O_CREAT | O_EXCL | O_RDWR | O_CLOEXEC | O_NOFOLLOW;
#if BUILDFLAG(IS_APPLE)
  flags |= O_EXLOCK | O_NONBLOCK;
#endif
  base::File file(
      HANDLE_EINTR(openat(staging.get(), slot.value().c_str(), flags, 0600)));
#endif
  if (!file.IsValid()) {
    return failed;
  }
  absl::Cleanup cleanup_slot = [&] {
#if BUILDFLAG(IS_WIN)
    DiscardOpenFile(file);
#else
    unlinkat(staging.get(), slot.value().c_str(), 0);
#endif
  };
#if !BUILDFLAG(IS_WIN) && !BUILDFLAG(IS_APPLE)
  if (flock(file.GetPlatformFile(), LOCK_EX | LOCK_NB) != 0) {
    return failed;
  }
#endif
  const auto id = FileId(file.GetPlatformFile());
  if (id.empty()) {
    return failed;
  }
  const auto registered = base::FilePath::FromASCII(prefix + id);
#if BUILDFLAG(IS_WIN)
  if (!RenameOpenFile(file, root.Append(staging_path).Append(registered),
                      false)) {
#else
  if (RenameExclusive(staging.get(), slot, staging.get(), registered) != 0) {
#endif
    return failed;
  }
  slot = registered;
  if (!fill(file)) {
    return failed;
  }
  const bool replace = mode == WorkspacePublishMode::kReplace;
#if BUILDFLAG(IS_WIN)
  if (!RenameOpenFile(file, root.Append(relative), replace)) {
    const auto error = ::GetLastError();
    if (!replace &&
        (error == ERROR_ALREADY_EXISTS || error == ERROR_FILE_EXISTS)) {
      return base::unexpected(WorkspaceError::kAlreadyExists);
    }
    return failed;
  }
#else
  const int result =
      replace ? renameat(staging.get(), slot.value().c_str(), parent.get(),
                         relative.BaseName().value().c_str())
              : RenameExclusive(staging.get(), slot, parent.get(),
                                relative.BaseName());
  if (result != 0) {
    if (!replace && errno == EEXIST) {
      return base::unexpected(WorkspaceError::kAlreadyExists);
    }
    return failed;
  }
#endif
  std::move(cleanup_slot).Cancel();
  return base::ok();
}

}  // namespace

#if BUILDFLAG(IS_WIN)
std::vector<base::File> OpenWorkspaceDirectory(const base::FilePath& root,
                                               const base::FilePath& relative,
                                               bool create) {
  if (!SafeRelative(relative)) {
    ::SetLastError(ERROR_INVALID_NAME);
    return {};
  }
  auto path = root;
  std::vector<base::File> parents;
  auto pin = [&]() {
    // Attribute-only opens do not participate in Windows share checks.
    base::win::ScopedHandle handle(::CreateFileW(
        path.value().c_str(), FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    if (!handle.is_valid()) {
      return false;
    }
    BY_HANDLE_FILE_INFORMATION info{};
    if (!::GetFileInformationByHandle(handle.get(), &info) ||
        !(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
        (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
      ::SetLastError(ERROR_DIRECTORY);
      return false;
    }
    parents.emplace_back(std::move(handle));
    return true;
  };
  if (!pin()) {
    return {};
  }
  for (const auto& component : relative.GetComponents()) {
    if (component == base::FilePath::kCurrentDirectory) {
      continue;
    }
    path = path.Append(component);
    if (create && !::CreateDirectoryW(path.value().c_str(), nullptr) &&
        ::GetLastError() != ERROR_ALREADY_EXISTS) {
      return {};
    }
    if (!pin()) {
      return {};
    }
  }
  return parents;
}
#else
base::ScopedFD OpenWorkspaceDirectory(const base::FilePath& root,
                                      const base::FilePath& relative,
                                      bool create) {
  if (!SafeRelative(relative)) {
    errno = EINVAL;
    return {};
  }
  constexpr int kDirectory = O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC;
  base::ScopedFD dir(HANDLE_EINTR(open(root.value().c_str(), kDirectory)));
  for (const auto& component : relative.GetComponents()) {
    if (!dir.is_valid()) {
      break;
    }
    if (component == base::FilePath::kCurrentDirectory) {
      continue;
    }
    if (create && mkdirat(dir.get(), component.c_str(), 0700) != 0 &&
        errno != EEXIST) {
      return {};
    }
    dir.reset(HANDLE_EINTR(openat(dir.get(), component.c_str(), kDirectory)));
  }
  return dir;
}
#endif

base::File OpenWorkspaceFile(const base::FilePath& root,
                             const base::FilePath& relative) {
  if (!IsFilePath(relative)) {
    return base::File(base::File::FILE_ERROR_SECURITY);
  }
  auto parent = OpenWorkspaceDirectory(root, relative.DirName());
#if BUILDFLAG(IS_WIN)
  if (!parent.empty()) {
    return OpenRegular(root.Append(relative), false);
  }
#else
  if (parent.is_valid()) {
    return OpenRegular(parent.get(), relative.BaseName(), false);
  }
#endif
  return base::File(base::File::GetLastFileError());
}

base::expected<std::optional<int64_t>, WorkspaceError> WorkspaceFileSize(
    const base::FilePath& root,
    const base::FilePath& relative) {
  auto file = OpenWorkspaceFile(root, relative);
  if (!file.IsValid()) {
    if (file.error_details() == base::File::FILE_ERROR_NOT_FOUND) {
      return std::nullopt;
    }
    return base::unexpected(WorkspaceError::kIoError);
  }
  int64_t length = file.GetLength();
  if (length < 0) {
    return base::unexpected(WorkspaceError::kIoError);
  }
  return length;
}

bool WriteWorkspaceFile(const base::FilePath& root,
                        const base::FilePath& relative,
                        std::string_view content) {
  return Publish(root, relative, WorkspacePublishMode::kReplace,
                 [&](base::File& file) {
                   return file.WriteAtCurrentPosAndCheck(
                       base::as_byte_span(content));
                 })
      .has_value();
}

base::expected<void, WorkspaceError> CopyToWorkspaceFile(
    const base::FilePath& root,
    const base::FilePath& relative,
    base::File& source,
    WorkspacePublishMode mode) {
  return Publish(root, relative, mode, [&](base::File& file) {
    return base::CopyFileContents(source, file);
  });
}

void RemoveEmptyWorkspacePublishDirectory(const base::FilePath& root,
                                          const base::FilePath& relative) {
  if (relative.BaseName().value() != kWorkspacePublishDirectory) {
    return;
  }
  auto parent = OpenWorkspaceDirectory(root, relative.DirName());
#if BUILDFLAG(IS_WIN)
  if (!parent.empty()) {
    base::File directory(base::win::ScopedHandle(::CreateFileW(
        root.Append(relative).value().c_str(), DELETE | FILE_READ_ATTRIBUTES, 0,
        nullptr, OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr)));
    BY_HANDLE_FILE_INFORMATION info{};
    if (directory.IsValid() &&
        ::GetFileInformationByHandle(directory.GetPlatformFile(), &info) &&
        (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
        !(info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
      DiscardOpenFile(directory);
    }
  }
#else
  if (parent.is_valid()) {
    unlinkat(parent.get(), kWorkspacePublishDirectory, AT_REMOVEDIR);
  }
#endif
}

bool RecoverWorkspacePublication(const base::FilePath& root,
                                 const base::FilePath& relative,
                                 bool legacy) {
  const auto basename = relative.BaseName();
  const auto name = basename.MaybeAsASCII();
  const auto parent_path = relative.DirName();
  if (legacy && (parent_path != base::FilePath::FromASCII(".workspace_tmp") ||
                 !IsV4(name))) {
    return false;
  }
  bool created_directory = false;
  std::string_view suffix = name;
  if (!legacy) {
    if (parent_path.BaseName().value() != kWorkspacePublishDirectory) {
      return false;
    }
    const auto root_pin = OpenWorkspaceDirectory(root, {});
    const auto root_id = FileId(LastHandle(root_pin));
    if (root_id.empty()) {
      return false;
    }
    created_directory = suffix.starts_with("dir-");
    const std::string prefix =
        (created_directory ? "dir-" : "slot-") + root_id + ".";
    if (!suffix.starts_with(prefix)) {
      return false;
    }
    suffix.remove_prefix(prefix.size());
  }

  const auto parent = OpenWorkspaceDirectory(root, parent_path);
  if (LastHandle(parent) == base::kInvalidPlatformFile) {
    return false;
  }
#if BUILDFLAG(IS_WIN)
  // Exclusive access binds validation and deletion to the same idle file.
  base::File file(base::win::ScopedHandle(::CreateFileW(
      root.Append(relative).value().c_str(), GENERIC_READ | DELETE, 0, nullptr,
      OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr)));
  BY_HANDLE_FILE_INFORMATION info{};
  if (!file.IsValid() ||
      !::GetFileInformationByHandle(file.GetPlatformFile(), &info) ||
      ::GetFileType(file.GetPlatformFile()) != FILE_TYPE_DISK ||
      info.nNumberOfLinks != 1 ||
      (info.dwFileAttributes &
       (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))) {
    return false;
  }
#else
  auto file = OpenRegular(parent.get(), basename, false);
  struct stat info{};
  if (!file.IsValid() ||
      flock(file.GetPlatformFile(), LOCK_EX | LOCK_NB) != 0 ||
      fstat(file.GetPlatformFile(), &info) != 0 || info.st_nlink != 1) {
    return false;
  }
#endif
  if (!legacy) {
    if (suffix.starts_with("new-")) {
      if (!IsV4(suffix.substr(4)) || file.GetLength() != 0) {
        return false;
      }
    } else if (suffix != FileId(file.GetPlatformFile())) {
      return false;
    }
  }
#if BUILDFLAG(IS_WIN)
  DiscardOpenFile(file);
#else
  struct stat current{};
  if (fstatat(parent.get(), basename.value().c_str(), &current,
              AT_SYMLINK_NOFOLLOW) != 0 ||
      current.st_dev != info.st_dev || current.st_ino != info.st_ino ||
      unlinkat(parent.get(), basename.value().c_str(), 0) != 0) {
    return false;
  }
#endif
  return created_directory;
}

bool DeleteWorkspaceFile(const base::FilePath& root,
                         const base::FilePath& relative) {
  if (!IsFilePath(relative)) {
    return false;
  }
  auto parent = OpenWorkspaceDirectory(root, relative.DirName());
#if BUILDFLAG(IS_WIN)
  return !parent.empty() &&
         ::DeleteFileW(root.Append(relative).value().c_str());
#else
  return parent.is_valid() &&
         unlinkat(parent.get(), relative.BaseName().value().c_str(), 0) == 0;
#endif
}

bool AppendWorkspaceAudit(const base::FilePath& root, std::string_view line) {
  auto parent = OpenWorkspaceDirectory(root, {});
  const base::FilePath name(FILE_PATH_LITERAL(".audit.log"));
#if BUILDFLAG(IS_WIN)
  if (parent.empty()) {
    return false;
  }
  auto file = OpenRegular(root.Append(name), true);
#else
  if (!parent.is_valid()) {
    return false;
  }
  auto file = OpenRegular(parent.get(), name, true);
#endif
  return file.IsValid() &&
         file.WriteAtCurrentPosAndCheck(base::as_byte_span(line));
}

}  // namespace dao
