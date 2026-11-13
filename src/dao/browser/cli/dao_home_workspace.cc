// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/cli/dao_home_workspace.h"

#include <algorithm>
#include <array>
#include <set>
#include <utility>
#include <vector>

#include "base/base64.h"
#include "base/containers/span.h"
#include "base/files/file.h"
#include "base/files/file_enumerator.h"
#include "base/files/file_util.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/uuid.h"
#include "build/build_config.h"

#if BUILDFLAG(IS_WIN)
#include <windows.h>
#include "base/win/scoped_handle.h"
#include "dao/browser/local_ipc/dao_local_ipc_win_util.h"
#else
#include <dirent.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#include "base/files/scoped_file.h"
#include "base/posix/eintr_wrapper.h"
#endif

namespace dao {
namespace {

template <typename T>
using Result = DaoHomeWorkspace::Result<T>;
constexpr size_t kEncodedLimit = 32 * 1024 * 1024;
constexpr size_t kStateLimit = 1024 * 1024;

// All child opens are relative to a pinned directory. Windows disallows delete
// sharing, preventing parent replacement while descendants are accessed.
struct Directory {
  base::FilePath path;
#if BUILDFLAG(IS_WIN)
  base::win::ScopedHandle handle;
#else
  base::ScopedFD handle;
#endif
  std::string Identity() const {
#if BUILDFLAG(IS_WIN)
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(handle.get(), &info))
      return {};
    return base::NumberToString(info.dwVolumeSerialNumber) + ":" +
           base::NumberToString(info.nFileIndexHigh) + ":" +
           base::NumberToString(info.nFileIndexLow);
#else
    struct stat info{};
    if (fstat(handle.get(), &info) != 0)
      return {};
    return base::NumberToString(info.st_dev) + ":" +
           base::NumberToString(info.st_ino);
#endif
  }
};

bool Component(std::string_view name) {
  return !name.empty() && name != "." && name != ".." &&
         name.find_first_of("/\\:") == std::string_view::npos &&
         name.find('\0') == std::string_view::npos;
}

Result<Directory> OpenDirectory(const base::FilePath& path,
                                const Directory* parent = nullptr,
                                bool create = false,
                                bool deleting = false) {
  Directory dir;
  dir.path = path;
  const std::string name = path.BaseName().AsUTF8Unsafe();
  if (parent && !Component(name))
    return base::unexpected("WORKSPACE_INVALID");
#if BUILDFLAG(IS_WIN)
  if (create || !parent) {
    dir.handle = OpenDaoLocalIpcPrivatePath(
        path, true, create, FILE_LIST_DIRECTORY | (deleting ? DELETE : 0));
  } else {
    dir.handle.Set(CreateFileW(
        path.value().c_str(), FILE_LIST_DIRECTORY | (deleting ? DELETE : 0),
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr));
  }
  BY_HANDLE_FILE_INFORMATION info{};
  if (!dir.handle.is_valid() ||
      !GetFileInformationByHandle(dir.handle.get(), &info) ||
      !(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
      (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
    return base::unexpected("WORKSPACE_INVALID");
  }
#else
  const int parent_fd = parent ? parent->handle.get() : AT_FDCWD;
  const char* target = parent ? name.c_str() : path.value().c_str();
  if (create && mkdirat(parent_fd, target, 0700) != 0) {
    return base::unexpected("WORKSPACE_INVALID");
  }
  dir.handle.reset(HANDLE_EINTR(openat(
      parent_fd, target, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC)));
  struct stat info{};
  if (!dir.handle.is_valid() || fstat(dir.handle.get(), &info) != 0 ||
      info.st_uid != getuid() || !S_ISDIR(info.st_mode) ||
      (!parent && (info.st_mode & 0777) != 0700)) {
    return base::unexpected("WORKSPACE_INVALID");
  }
#endif
  return dir;
}

Result<std::vector<std::string>> Children(const Directory& dir) {
  std::vector<std::string> names;
#if BUILDFLAG(IS_WIN)
  base::FileEnumerator entries(dir.path, false,
                               base::FileEnumerator::FILES |
                                   base::FileEnumerator::DIRECTORIES |
                                   base::FileEnumerator::SHOW_SYM_LINKS);
  for (auto path = entries.Next(); !path.empty(); path = entries.Next()) {
    if (names.size() >= 2000)
      return base::unexpected("QUOTA_EXCEEDED");
    names.push_back(path.BaseName().AsUTF8Unsafe());
  }
  if (entries.GetError() != base::File::FILE_OK)
    return base::unexpected("WORKSPACE_INVALID");
#else
  base::ScopedFD descriptor(HANDLE_EINTR(
      openat(dir.handle.get(), ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC)));
  DIR* entries =
      descriptor.is_valid() ? fdopendir(descriptor.release()) : nullptr;
  if (!entries)
    return base::unexpected("WORKSPACE_INVALID");
  errno = 0;
  while (const auto* entry = readdir(entries)) {
    const std::string name(entry->d_name);
    if (name != "." && name != "..") {
      if (names.size() >= 2000) {
        closedir(entries);
        return base::unexpected("QUOTA_EXCEEDED");
      }
      names.push_back(name);
    }
  }
  const int error = errno;
  closedir(entries);
  if (error)
    return base::unexpected("WORKSPACE_INVALID");
#endif
  std::sort(names.begin(), names.end());
  return names;
}

Result<base::File> OpenFile(const Directory& dir,
                            const std::string& name,
                            bool create = false,
                            bool writable = false,
                            bool deleting = false) {
  if (!Component(name))
    return base::unexpected("WORKSPACE_INVALID");
#if BUILDFLAG(IS_WIN)
  const auto path = dir.path.Append(base::FilePath::FromUTF8Unsafe(name));
  auto handle = create ? OpenDaoLocalIpcPrivatePath(
                             path, false, true, GENERIC_READ | GENERIC_WRITE)
                       : base::win::ScopedHandle(CreateFileW(
                             path.value().c_str(),
                             GENERIC_READ | (writable ? GENERIC_WRITE : 0) |
                                 (deleting ? DELETE : 0),
                             FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                             FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
  BY_HANDLE_FILE_INFORMATION info{};
  if (!handle.is_valid() || !GetFileInformationByHandle(handle.get(), &info) ||
      (info.dwFileAttributes &
       (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) ||
      info.nNumberOfLinks != 1)
    return base::unexpected("WORKSPACE_INVALID");
  return base::File(handle.release());
#else
  base::ScopedFD fd(HANDLE_EINTR(
      openat(dir.handle.get(), name.c_str(),
             ((create || writable) ? O_RDWR : O_RDONLY) | O_NOFOLLOW |
                 O_NONBLOCK | O_CLOEXEC | (create ? O_CREAT | O_EXCL : 0),
             0600)));
  struct stat info{};
  if (!fd.is_valid() || fstat(fd.get(), &info) != 0 || !S_ISREG(info.st_mode) ||
      info.st_nlink != 1 || info.st_uid != getuid())
    return base::unexpected("WORKSPACE_INVALID");
  return base::File(fd.release());
#endif
}

Result<std::string> Read(const Directory& dir,
                         const std::string& name,
                         size_t limit) {
  auto file = OpenFile(dir, name);
  if (!file.has_value())
    return base::unexpected(file.error());
  base::File::Info before, after;
  if (!file->GetInfo(&before) || before.size < 0 ||
      static_cast<uint64_t>(before.size) > limit)
    return base::unexpected("QUOTA_EXCEEDED");
  std::string bytes(static_cast<size_t>(before.size), '\0');
  if (!file->ReadAndCheck(0, base::as_writable_byte_span(bytes)) ||
      !file->GetInfo(&after) || before.size != after.size ||
      before.last_modified != after.last_modified)
    return base::unexpected("FILES_CHANGED");
  return base::ok(std::move(bytes));
}

Result<void> Write(const Directory& dir,
                   const std::string& name,
                   std::string_view bytes) {
  const std::string temporary =
      ".write-" + base::Uuid::GenerateRandomV4().AsLowercaseString();
  auto file = OpenFile(dir, temporary, true);
  if (!file.has_value() || !file->WriteAndCheck(0, base::as_byte_span(bytes)) ||
      !file->Flush())
    return base::unexpected("LOCAL_IO_ERROR");
  file->Close();
#if BUILDFLAG(IS_WIN)
  if (!MoveFileExW(
          dir.path.AppendASCII(temporary).value().c_str(),
          dir.path.Append(base::FilePath::FromUTF8Unsafe(name)).value().c_str(),
          MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    return base::unexpected("LOCAL_IO_ERROR");
#else
  if (renameat(dir.handle.get(), temporary.c_str(), dir.handle.get(),
               name.c_str()) != 0 ||
      fsync(dir.handle.get()) != 0)
    return base::unexpected("LOCAL_IO_ERROR");
#endif
  return base::ok();
}

bool DevelopmentArtifact(std::string_view path) {
  for (auto part : base::SplitStringPiece(path, "/", base::KEEP_WHITESPACE,
                                          base::SPLIT_WANT_ALL)) {
    if (part.starts_with('.') || part == "node_modules" ||
        part == "package.json" || part == "package-lock.json" ||
        part == "yarn.lock" || part == "pnpm-lock.yaml" ||
        part.ends_with('~') || part.ends_with(".swp") || part.ends_with(".tmp"))
      return true;
  }
  return false;
}

Result<void> Scan(const Directory& dir,
                  const std::string& prefix,
                  HomeFileSnapshot* files,
                  size_t* total,
                  size_t* entries,
                  size_t depth = 0) {
  if (depth > 32)
    return base::unexpected("QUOTA_EXCEEDED");
  auto names = Children(dir);
  if (!names.has_value())
    return base::unexpected(names.error());
  for (const auto& name : *names) {
    if (++*entries > 2000)
      return base::unexpected("QUOTA_EXCEEDED");
    const std::string relative = prefix + name;
    if (!IsValidHomeSnapshotPath(relative) || DevelopmentArtifact(relative))
      return base::unexpected("WORKSPACE_INVALID");
    auto child = OpenDirectory(
        dir.path.Append(base::FilePath::FromUTF8Unsafe(name)), &dir);
    if (child.has_value()) {
      auto result =
          Scan(*child, relative + "/", files, total, entries, depth + 1);
      if (!result.has_value())
        return result;
    } else {
      auto bytes = Read(dir, name, kMaxHomeSnapshotBytes - *total);
      if (!bytes.has_value())
        return base::unexpected(bytes.error());
      *total += bytes->size();
      files->emplace(relative, std::move(*bytes));
      if (files->size() > kMaxHomeSnapshotFiles)
        return base::unexpected("QUOTA_EXCEEDED");
    }
  }
  return base::ok();
}

Result<void> WriteProject(const Directory& root,
                          const HomeFileSnapshot& files) {
  for (const auto& [path, bytes] : files) {
    const auto parts = base::SplitString(path, "/", base::KEEP_WHITESPACE,
                                         base::SPLIT_WANT_ALL);
    std::vector<Directory> parents;
    parents.reserve(parts.size());
    const Directory* parent = &root;
    for (size_t i = 0; i + 1 < parts.size(); ++i) {
      auto child_path =
          parent->path.Append(base::FilePath::FromUTF8Unsafe(parts[i]));
      auto child = OpenDirectory(child_path, parent);
      if (!child.has_value())
        child = OpenDirectory(child_path, parent, true);
      if (!child.has_value())
        return base::unexpected(child.error());
      parents.push_back(std::move(*child));
      parent = &parents.back();
    }
    auto result = Write(*parent, parts.back(), bytes);
    if (!result.has_value())
      return result;
  }
  return base::ok();
}

Result<void> RemoveChildren(const Directory& dir,
                            size_t depth = 0,
                            bool preserve_metadata = false) {
  if (depth > 32)
    return base::unexpected("QUOTA_EXCEEDED");
  auto children = Children(dir);
  if (!children.has_value())
    return base::unexpected(children.error());
  for (const auto& name : *children) {
    if (preserve_metadata && name == ".dao-home")
      continue;
    if (!Component(name))
      return base::unexpected("WORKSPACE_INVALID");
    const auto path = dir.path.Append(base::FilePath::FromUTF8Unsafe(name));
    auto child = OpenDirectory(path, &dir, false, true);
    if (child.has_value()) {
      auto result = RemoveChildren(*child, depth + 1);
      if (!result.has_value())
        return result;
#if BUILDFLAG(IS_WIN)
      FILE_DISPOSITION_INFO disposition = {TRUE};
      if (!SetFileInformationByHandle(child->handle.get(), FileDispositionInfo,
                                      &disposition, sizeof(disposition)))
        return base::unexpected("LOCAL_IO_ERROR");
#else
      if (unlinkat(dir.handle.get(), name.c_str(), AT_REMOVEDIR) != 0)
        return base::unexpected("LOCAL_IO_ERROR");
#endif
    } else {
#if BUILDFLAG(IS_WIN)
      auto file = OpenFile(dir, name, false, false, true);
      if (!file.has_value())
        return base::unexpected(file.error());
      FILE_DISPOSITION_INFO disposition = {TRUE};
      if (!SetFileInformationByHandle(file->GetPlatformFile(),
                                      FileDispositionInfo, &disposition,
                                      sizeof(disposition)))
        return base::unexpected("LOCAL_IO_ERROR");
#else
      // unlinkat removes links themselves and never follows their targets.
      if (unlinkat(dir.handle.get(), name.c_str(), 0) != 0)
        return base::unexpected("LOCAL_IO_ERROR");
#endif
    }
  }
  return base::ok();
}

Result<base::FilePath> SystemTemp() {
  base::FilePath path;
  if (!base::GetTempDir(&path) || !path.IsAbsolute())
    return base::unexpected("WORKSPACE_INVALID");
  path = base::MakeAbsoluteFilePath(path);
  if (path.empty() || !path.IsAbsolute())
    return base::unexpected("WORKSPACE_INVALID");
  base::FilePath cwd;
  if (base::GetCurrentDirectory(&cwd) &&
      base::MakeAbsoluteFilePath(cwd) == path)
    return base::unexpected("WORKSPACE_INVALID");
  return path;
}

}  // namespace

struct DaoHomeWorkspace::Impl {
  Directory root;
  Directory metadata;
  Directory snapshots;
  Directory artifacts;
  base::File lock;
};

DaoHomeWorkspace::DaoHomeWorkspace(std::unique_ptr<Impl> impl)
    : impl_(std::move(impl)) {}
DaoHomeWorkspace::~DaoHomeWorkspace() = default;
const base::FilePath& DaoHomeWorkspace::path() const {
  return impl_->root.path;
}

Result<std::unique_ptr<DaoHomeWorkspace>> DaoHomeWorkspace::Create(
    const HomeFileSnapshot& files,
    base::DictValue state) {
  const auto valid = ValidateHomeFileSnapshot(files);
  if (!valid.has_value())
    return base::unexpected(valid.error() == HomeError::kQuotaExceeded
                                ? "QUOTA_EXCEEDED"
                                : "WORKSPACE_INVALID");
  for (const auto& [path, bytes] : files) {
    if (DevelopmentArtifact(path))
      return base::unexpected("WORKSPACE_INVALID");
  }
  auto temp = SystemTemp();
  if (!temp.has_value())
    return base::unexpected(temp.error());
  base::FilePath cwd;
  if (base::GetCurrentDirectory(&cwd) &&
      base::MakeAbsoluteFilePath(cwd) == *temp)
    return base::unexpected("WORKSPACE_INVALID");
  const auto path = temp->AppendASCII(
      "dao-home-" + base::Uuid::GenerateRandomV4().AsLowercaseString());
  auto root = OpenDirectory(path, nullptr, true);
  if (!root.has_value())
    return base::unexpected(root.error());
  auto metadata = OpenDirectory(path.AppendASCII(".dao-home"), &*root, true);
  auto project = OpenDirectory(path.AppendASCII("project"), &*root, true);
  if (!metadata.has_value() || !project.has_value())
    return base::unexpected("LOCAL_IO_ERROR");
  auto snapshots =
      OpenDirectory(metadata->path.AppendASCII("base"), &*metadata, true);
  auto artifacts =
      OpenDirectory(metadata->path.AppendASCII("artifacts"), &*metadata, true);
  auto lock = OpenFile(*metadata, "lock", true);
  if (!snapshots.has_value() || !artifacts.has_value() || !lock.has_value())
    return base::unexpected("LOCAL_IO_ERROR");
  auto written = WriteProject(*project, files);
  if (!written.has_value())
    return base::unexpected(written.error());
  auto impl = std::make_unique<Impl>();
  impl->root = std::move(*root);
  impl->metadata = std::move(*metadata);
  impl->snapshots = std::move(*snapshots);
  impl->artifacts = std::move(*artifacts);
  impl->lock = std::move(*lock);
  if (impl->lock.Lock(base::File::LockMode::kExclusive) != base::File::FILE_OK)
    return base::unexpected("WORKSPACE_BUSY");
  auto workspace =
      std::unique_ptr<DaoHomeWorkspace>(new DaoHomeWorkspace(std::move(impl)));
  base::DictValue hashes;
  for (const auto& [name, bytes] : files)
    hashes.Set(name, HomeFileSnapshotDigest({{name, bytes}}));
  state.Set("base_file_hashes", std::move(hashes));
  state.Set("format", 1);
  state.Set("workspace_dir", path.AsUTF8Unsafe());
  state.Set("identity", workspace->impl_->root.Identity());
  state.Set("base_digest", HomeFileSnapshotDigest(files));
  workspace->state_ = std::move(state);
  auto saved = workspace->SaveSnapshot(files);
  if (saved.has_value())
    saved = workspace->SaveState();
  if (!saved.has_value())
    return base::unexpected(saved.error());
  return workspace;
}

Result<std::unique_ptr<DaoHomeWorkspace>> DaoHomeWorkspace::Open(
    const base::FilePath& path) {
  auto temp = SystemTemp();
  if (!temp.has_value() || !path.IsAbsolute() || path.ReferencesParent() ||
      path.DirName() != *temp ||
      !base::StartsWith(path.BaseName().AsUTF8Unsafe(), "dao-home-") ||
      base::MakeAbsoluteFilePath(path) != path)
    return base::unexpected("WORKSPACE_INVALID");
  auto root = OpenDirectory(path);
  if (!root.has_value())
    return base::unexpected(root.error());
  auto metadata = OpenDirectory(path.AppendASCII(".dao-home"), &*root);
  if (!metadata.has_value())
    return base::unexpected(metadata.error());
  auto lock = OpenFile(*metadata, "lock", false, true);
  if (!lock.has_value())
    return base::unexpected(lock.error());
  if (lock->Lock(base::File::LockMode::kExclusive) != base::File::FILE_OK)
    return base::unexpected("WORKSPACE_BUSY");
  auto contents = Read(*metadata, "state.json", kStateLimit);
  auto state = contents.has_value()
                   ? base::JSONReader::ReadDict(*contents, base::JSON_PARSE_RFC)
                   : std::nullopt;
  if (!state.has_value() || state->FindInt("format") != 1 ||
      !state->FindString("workspace_dir") ||
      *state->FindString("workspace_dir") != path.AsUTF8Unsafe() ||
      !state->FindString("identity") ||
      *state->FindString("identity") != root->Identity())
    return base::unexpected("WORKSPACE_INVALID");
  auto snapshots =
      OpenDirectory(metadata->path.AppendASCII("base"), &*metadata);
  auto artifacts =
      OpenDirectory(metadata->path.AppendASCII("artifacts"), &*metadata);
  if (!snapshots.has_value() || !artifacts.has_value())
    return base::unexpected("WORKSPACE_INVALID");
  auto impl = std::make_unique<Impl>();
  impl->root = std::move(*root);
  impl->metadata = std::move(*metadata);
  impl->snapshots = std::move(*snapshots);
  impl->artifacts = std::move(*artifacts);
  impl->lock = std::move(*lock);
  auto workspace =
      std::unique_ptr<DaoHomeWorkspace>(new DaoHomeWorkspace(std::move(impl)));
  workspace->state_ = std::move(*state);
  return workspace;
}

Result<HomeFileSnapshot> DaoHomeWorkspace::Capture() const {
  auto project = OpenDirectory(path().AppendASCII("project"), &impl_->root);
  if (!project.has_value())
    return base::unexpected(project.error());
  HomeFileSnapshot first, second;
  size_t total = 0;
  size_t entries = 0;
  auto result = Scan(*project, "", &first, &total, &entries);
  if (!result.has_value())
    return base::unexpected(result.error());
  total = 0;
  entries = 0;
  result = Scan(*project, "", &second, &total, &entries);
  if (!result.has_value())
    return base::unexpected(result.error());
  if (first != second)
    return base::unexpected("FILES_CHANGED");
  if (!ValidateHomeFileSnapshot(first).has_value())
    return base::unexpected("WORKSPACE_INVALID");
  return first;
}

Result<HomeFileSnapshot> DaoHomeWorkspace::ReadSnapshot(
    const std::string& digest) const {
  if (digest.size() != 64 ||
      digest.find_first_not_of("0123456789abcdef") != std::string::npos)
    return base::unexpected("WORKSPACE_INVALID");
  auto bytes = Read(impl_->snapshots, digest + ".json", kEncodedLimit);
  if (!bytes.has_value())
    return base::unexpected(bytes.error());
  auto value = base::JSONReader::ReadDict(*bytes, base::JSON_PARSE_RFC);
  if (!value.has_value())
    return base::unexpected("WORKSPACE_INVALID");
  auto files = DecodeHomeFileSnapshot(*value);
  if (!files.has_value() || HomeFileSnapshotDigest(*files) != digest)
    return base::unexpected("WORKSPACE_INVALID");
  return std::move(*files);
}

Result<HomeFileSnapshot> DaoHomeWorkspace::ReadBase() const {
  const auto* digest = state_.FindString("base_digest");
  return digest ? ReadSnapshot(*digest) : base::unexpected("WORKSPACE_INVALID");
}

Result<void> DaoHomeWorkspace::SaveSnapshot(const HomeFileSnapshot& files) {
  auto bytes = base::WriteJson(EncodeHomeFileSnapshot(files));
  if (!bytes.has_value())
    return base::unexpected("LOCAL_IO_ERROR");
  return Write(impl_->snapshots, HomeFileSnapshotDigest(files) + ".json",
               *bytes);
}

Result<void> DaoHomeWorkspace::SaveState() {
  auto current = OpenDirectory(path());
  if (!current.has_value() || current->Identity() != impl_->root.Identity())
    return base::unexpected("WORKSPACE_INVALID");
  auto bytes = base::WriteJson(state_);
  if (!bytes.has_value() || bytes->size() > kStateLimit)
    return base::unexpected("LOCAL_IO_ERROR");
  auto saved = Write(impl_->metadata, "state.json", *bytes);
  if (saved.has_value())
    PruneSnapshots();
  return saved;
}

void DaoHomeWorkspace::PruneSnapshots() {
  std::set<std::string> retained;
  for (const auto* key : {"base_digest", "candidate_digest"}) {
    if (const auto* digest = state_.FindString(key))
      retained.insert(*digest + ".json");
  }
  if (const auto* digest =
          state_.FindStringByDottedPath("pending_operation.digest"))
    retained.insert(*digest + ".json");
  auto names = Children(impl_->snapshots);
  if (!names.has_value())
    return;
  // Prune only after durable metadata stops referencing a snapshot. Cleanup
  // failure must not turn a successful state write into an ambiguous failure.
  for (const auto& name : *names) {
    if (retained.contains(name) || name.size() != 69 ||
        !name.ends_with(".json") ||
        name.substr(0, 64).find_first_not_of("0123456789abcdef") !=
            std::string::npos)
      continue;
#if BUILDFLAG(IS_WIN)
    auto file = OpenFile(impl_->snapshots, name, false, false, true);
    if (file.has_value()) {
      FILE_DISPOSITION_INFO disposition = {TRUE};
      SetFileInformationByHandle(file->GetPlatformFile(), FileDispositionInfo,
                                 &disposition, sizeof(disposition));
    }
#else
    unlinkat(impl_->snapshots.handle.get(), name.c_str(), 0);
#endif
  }
}

Result<void> DaoHomeWorkspace::AdvanceBase(const std::string& revision,
                                           const HomeFileSnapshot& files) {
  auto saved = SaveSnapshot(files);
  if (!saved.has_value())
    return saved;
  base::DictValue hashes;
  for (const auto& [name, bytes] : files)
    hashes.Set(name, HomeFileSnapshotDigest({{name, bytes}}));
  state_.Set("base_file_hashes", std::move(hashes));
  state_.Set("base_revision", revision);
  state_.Set("base_digest", HomeFileSnapshotDigest(files));
  state_.Remove("draft_id");
  state_.Remove("candidate_digest");
  state_.Remove("preview_digest");
  state_.Remove("publishable");
  state_.Remove("pending_operation");
  return SaveState();
}

Result<void> DaoHomeWorkspace::RecoverOperation(
    const base::DictValue& operation) {
  const auto* pending = state_.FindDict("pending_operation");
  const auto* status = operation.FindString("status");
  const auto* id = operation.FindString("operation_id");
  const auto* revision = operation.FindString("revision");
  if (!pending || !status || *status != "committed" || !id || !revision ||
      revision->empty() || !pending->FindString("operation_id") ||
      *id != *pending->FindString("operation_id"))
    return base::unexpected("OUTCOME_UNKNOWN");
  const auto* kind = pending->FindString("kind");
  if (!kind || (*kind != "publish" && *kind != "rollback"))
    return base::unexpected("WORKSPACE_INVALID");
  auto original = state_.Clone();
  if (*kind == "publish") {
    const auto* digest = operation.FindString("digest");
    if (!digest || !pending->FindString("digest") ||
        *digest != *pending->FindString("digest"))
      return base::unexpected("OUTCOME_UNKNOWN");
    auto bytes = ReadSnapshot(*digest);
    if (!bytes.has_value())
      return base::unexpected("COMMITTED_LOCAL_RECOVERY_NEEDED");
    state_.Set("last_publication", operation.Clone());
    auto saved = AdvanceBase(*revision, *bytes);
    if (!saved.has_value())
      state_ = std::move(original);
    return saved;
  }
  state_.Set("last_publication", operation.Clone());
  state_.Remove("pending_operation");
  state_.Remove("preview_digest");
  state_.Remove("draft_id");
  auto saved = SaveState();
  if (!saved.has_value())
    state_ = std::move(original);
  return saved;
}

Result<bool> DaoHomeWorkspace::IsClean() const {
  auto current = Capture();
  if (!current.has_value())
    return base::unexpected(current.error());
  auto base = ReadBase();
  if (!base.has_value())
    return base::unexpected(base.error());
  return *current == *base;
}

Result<void> DaoHomeWorkspace::Clean(bool discard) {
  if (!discard) {
    auto clean = IsClean();
    if (!clean.has_value())
      return base::unexpected(clean.error());
    if (!*clean || state_.Find("pending_operation"))
      return base::unexpected("UNPUBLISHED_CHANGES");
  }
  const auto root_path = path();
  const auto identity = impl_->root.Identity();
  auto check = OpenDirectory(root_path);
  if (!check.has_value() || check->Identity() != identity)
    return base::unexpected("WORKSPACE_INVALID");
  check = base::unexpected("CLOSED");
  // Keep the lock and recovery state until project cleanup succeeds.
  auto removed = RemoveChildren(impl_->root, 0, true);
  if (!removed.has_value())
    return removed;
  impl_->lock.Close();
  impl_->snapshots = {};
  impl_->artifacts = {};
  impl_->metadata = {};
  removed = RemoveChildren(impl_->root);
  if (!removed.has_value())
    return removed;
#if BUILDFLAG(IS_WIN)
  impl_->root = {};
  auto root_to_remove = OpenDirectory(root_path, nullptr, false, true);
  if (!root_to_remove.has_value() || root_to_remove->Identity() != identity)
    return base::unexpected("WORKSPACE_INVALID");
  FILE_DISPOSITION_INFO disposition = {TRUE};
  if (!SetFileInformationByHandle(root_to_remove->handle.get(),
                                  FileDispositionInfo, &disposition,
                                  sizeof(disposition)))
    return base::unexpected("LOCAL_IO_ERROR");
  root_to_remove = base::unexpected("CLOSED");
#else
  impl_->root = {};
  if (!base::DeleteFile(root_path))
    return base::unexpected("LOCAL_IO_ERROR");
#endif
  return base::ok();
}

Result<base::FilePath> DaoHomeWorkspace::SaveArtifact(
    std::string_view extension,
    std::string_view bytes) {
  if (extension != "png" && extension != "jpg")
    return base::unexpected("PROTOCOL_MISMATCH");
  const std::string name = base::Uuid::GenerateRandomV4().AsLowercaseString() +
                           "." + std::string(extension);
  auto saved = Write(impl_->artifacts, name, bytes);
  if (!saved.has_value())
    return base::unexpected(saved.error());
  return impl_->artifacts.path.AppendASCII(name);
}

}  // namespace dao
