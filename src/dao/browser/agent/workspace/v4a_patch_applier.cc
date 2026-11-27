// Copyright 2026 Dao Browser Authors. All rights reserved.

#include "dao/browser/agent/workspace/v4a_patch_applier.h"

#include <optional>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "base/files/file.h"
#include "base/files/file_util.h"
#include "base/files/scoped_file.h"
#include "base/files/scoped_temp_dir.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "build/build_config.h"
#include "dao/browser/agent/workspace/path_normalizer.h"
#include "dao/browser/agent/workspace/text_only_filter.h"
#include "dao/browser/agent/workspace/workspace_file.h"
#include "dao/browser/agent/workspace/workspace_quota.h"

#if BUILDFLAG(IS_MAC)
#include <sys/mount.h>
#include <unistd.h>

#include "base/strings/sys_string_conversions.h"
#elif BUILDFLAG(IS_WIN)
#include <windows.h>
#endif

namespace dao {

namespace {

// Read file as a vector of lines (no trailing empty entry from terminating
// newline).
base::expected<std::vector<std::string>, WorkspaceError> ReadLines(
    base::File file) {
  base::ScopedFILE stream(base::FileToFILE(std::move(file), "rb"));
  std::string raw;
  if (!stream || !base::ReadStreamToString(stream.get(), &raw)) {
    return base::unexpected(WorkspaceError::kIoError);
  }
  std::vector<std::string> lines = base::SplitString(
      raw, "\n", base::KEEP_WHITESPACE, base::SPLIT_WANT_ALL);
  if (!lines.empty() && lines.back().empty()) {
    lines.pop_back();
  }
  return lines;
}

std::string JoinLines(const std::vector<std::string>& lines) {
  std::string out;
  for (const auto& l : lines) {
    out += l;
    out.push_back('\n');
  }
  return out;
}

// Locates the unique index in `body` where the hunk's context+remove block
// matches. Returns -1 if not found, -2 if found in multiple places.
int FindUniqueHunkMatch(const std::vector<std::string>& body,
                        const std::vector<std::string>& needle) {
  if (needle.empty()) {
    return -1;
  }
  int found = -1;
  for (int i = 0;
       i + static_cast<int>(needle.size()) <= static_cast<int>(body.size());
       ++i) {
    bool ok = true;
    for (size_t j = 0; j < needle.size(); ++j) {
      if (body[i + j] != needle[j]) {
        ok = false;
        break;
      }
    }
    if (ok) {
      if (found != -1) {
        return -2;
      }
      found = i;
    }
  }
  return found;
}

base::expected<std::vector<std::string>, WorkspaceError> ApplyHunkToBody(
    std::vector<std::string> body,
    const V4AHunk& hunk) {
  // Needle: context + remove lines, in declaration order.
  std::vector<std::string> needle;
  std::vector<std::string> replacement;
  for (const auto& l : hunk.lines) {
    switch (l.kind) {
      case V4AHunkLine::Kind::kContext:
        needle.push_back(l.text);
        replacement.push_back(l.text);
        break;
      case V4AHunkLine::Kind::kRemove:
        needle.push_back(l.text);
        break;
      case V4AHunkLine::Kind::kAdd:
        replacement.push_back(l.text);
        break;
    }
  }
  int at = FindUniqueHunkMatch(body, needle);
  if (at < 0) {
    return base::unexpected(WorkspaceError::kPatchContextMismatch);
  }
  body.erase(body.begin() + at, body.begin() + at + needle.size());
  body.insert(body.begin() + at, replacement.begin(), replacement.end());
  return body;
}

struct StagedWrite {
  base::FilePath staged;
  base::FilePath final_dest;
  WorkspacePublishMode mode;
};

struct DirectoryNameRules {
  bool case_sensitive = true;
#if BUILDFLAG(IS_MAC)
  bool apfs = false;
  bool hfs = false;
#endif
};

// Missing directories inherit their parent's rules. Existing directories may
// be mount points or NTFS folders with case sensitivity explicitly enabled.
base::expected<DirectoryNameRules, WorkspaceError> ReadNameRules(
    const base::FilePath& root,
    const base::FilePath& parent,
    DirectoryNameRules inherited) {
#if BUILDFLAG(IS_MAC)
  auto dir = OpenWorkspaceDirectory(root, parent);
  if (!dir.is_valid()) {
    if (!parent.empty() && errno == ENOENT) {
      return inherited;
    }
    return base::unexpected(WorkspaceError::kIoError);
  }
  const long sensitive = fpathconf(dir.get(), _PC_CASE_SENSITIVE);
  struct statfs volume{};
  if (sensitive < 0 || fstatfs(dir.get(), &volume) != 0) {
    return base::unexpected(WorkspaceError::kIoError);
  }
  const std::string_view type(volume.f_fstypename);
  return DirectoryNameRules{sensitive != 0, type == "apfs", type == "hfs"};
#elif BUILDFLAG(IS_WIN)
  auto dir = OpenWorkspaceDirectory(root, parent);
  if (dir.empty()) {
    const DWORD error = ::GetLastError();
    if (!parent.empty() &&
        (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)) {
      return inherited;
    }
    return base::unexpected(WorkspaceError::kIoError);
  }
  FILE_CASE_SENSITIVE_INFO info{};
  if (::GetFileInformationByHandleEx(dir.back().GetPlatformFile(),
                                     FileCaseSensitiveInfo, &info,
                                     sizeof(info))) {
    return DirectoryNameRules{(info.Flags & FILE_CS_FLAG_CASE_SENSITIVE_DIR) !=
                              0};
  }
  const DWORD error = ::GetLastError();
  if (error == ERROR_INVALID_PARAMETER || error == ERROR_NOT_SUPPORTED ||
      error == ERROR_INVALID_FUNCTION) {
    return DirectoryNameRules{false};
  }
  return base::unexpected(WorkspaceError::kIoError);
#else
  return inherited;
#endif
}

class PatchPaths {
 public:
  base::expected<void, WorkspaceError> Reserve(const base::FilePath& root,
                                               const base::FilePath& path) {
    base::FilePath parent;
    base::FilePath key;
    DirectoryNameRules rules;
    for (auto name : path.GetComponents()) {
      if (!key.empty()) {
        if (files_.contains(key)) {
          return base::unexpected(WorkspaceError::kPatchParseError);
        }
        directories_.insert(key);
      }
      auto entry = rules_.find(parent);
      if (entry == rules_.end()) {
        auto read = ReadNameRules(root, parent, rules);
        if (!read.has_value()) {
          return base::unexpected(read.error());
        }
        entry = rules_.emplace(parent, *read).first;
      }
      rules = entry->second;
      parent = parent.Append(name);
#if BUILDFLAG(IS_MAC)
      if (rules.hfs) {
        name = base::FilePath::GetHFSDecomposedForm(name);
        if (!rules.case_sensitive) {
          name = *insensitive_names_.insert(name).first;
        }
      } else if (rules.apfs || !rules.case_sensitive) {
        auto string = base::SysUTF8ToCFStringRef(name);
        if (!string) {
          return base::unexpected(WorkspaceError::kInvalidPath);
        }
        base::apple::ScopedCFTypeRef<CFMutableStringRef> folded(
            CFStringCreateMutableCopy(nullptr, 0, string.get()));
        if (!rules.case_sensitive) {
          CFStringFold(folded.get(), kCFCompareCaseInsensitive, nullptr);
        }
        if (rules.apfs) {
          CFStringNormalize(folded.get(), kCFStringNormalizationFormD);
        }
        name = base::SysCFStringRefToUTF8(folded.get());
      }
#elif BUILDFLAG(IS_WIN)
      if (!rules.case_sensitive) {
        name = *insensitive_names_.insert(name).first;
      }
#endif
      key = key.Append(name);
    }
    if (directories_.contains(key) || !files_.insert(key).second) {
      return base::unexpected(WorkspaceError::kPatchParseError);
    }
    return {};
  }

 private:
  std::map<base::FilePath, DirectoryNameRules> rules_;
  std::set<base::FilePath> directories_;
  std::set<base::FilePath> files_;
#if BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN)
  static bool LessIgnoreCase(const base::FilePath::StringType& left,
                             const base::FilePath::StringType& right) {
#if BUILDFLAG(IS_WIN)
    return ::CompareStringOrdinal(left.c_str(), -1, right.c_str(), -1, TRUE) ==
           CSTR_LESS_THAN;
#else
    return base::FilePath::CompareLessIgnoreCase(left, right);
#endif
  }
  std::set<base::FilePath::StringType, decltype(&LessIgnoreCase)>
      insensitive_names_{&LessIgnoreCase};
#endif
};

}  // namespace

base::expected<PatchResult, WorkspaceError> ApplyV4APatch(
    const base::FilePath& workspace_root,
    WorkspaceQuota* quota,
    const V4APatch& patch) {
  base::ScopedTempDir stage;
  if (!stage.CreateUniqueTempDir()) {
    return base::unexpected(WorkspaceError::kIoError);
  }

  PatchResult result;
  PatchPaths touched_paths;
  std::vector<StagedWrite> writes;
  std::vector<base::FilePath> deletes;
  std::vector<WorkspaceQuota::FileChange> changes;

  for (const V4AFileOp& op : patch.ops) {
    auto path = ResolveWorkspacePath(workspace_root, op.path);
    if (!path.has_value()) {
      return base::unexpected(path.error());
    }
    auto reserved = touched_paths.Reserve(workspace_root, *path);
    if (!reserved.has_value()) {
      return base::unexpected(reserved.error());
    }

    base::File file = OpenWorkspaceFile(workspace_root, *path);
    if (!file.IsValid()) {
      if (file.error_details() != base::File::FILE_ERROR_NOT_FOUND) {
        return base::unexpected(WorkspaceError::kIoError);
      }
      if (op.kind != V4AFileOp::Kind::kAdd) {
        return base::unexpected(WorkspaceError::kNotFound);
      }
    }

    base::FilePath destination = *path;
    auto mode = WorkspacePublishMode::kReplace;
    std::optional<uint64_t> existing_size;
    if (file.IsValid()) {
      const int64_t length = file.GetLength();
      if (length < 0) {
        return base::unexpected(WorkspaceError::kIoError);
      }
      existing_size = static_cast<uint64_t>(length);
    }
    std::string new_text;
    switch (op.kind) {
      case V4AFileOp::Kind::kAdd:
        if (file.IsValid()) {
          return base::unexpected(WorkspaceError::kAlreadyExists);
        }
        if (!IsTextExtensionAllowed(*path)) {
          return base::unexpected(WorkspaceError::kBinaryRejected);
        }
        new_text = JoinLines(op.add_lines);
        mode = WorkspacePublishMode::kCreate;
        result.added.push_back(op.path);
        break;
      case V4AFileOp::Kind::kUpdate: {
        auto body = ReadLines(std::move(file));
        if (!body.has_value()) {
          return base::unexpected(body.error());
        }
        for (const V4AHunk& h : op.hunks) {
          body = ApplyHunkToBody(std::move(*body), h);
          if (!body.has_value()) {
            return base::unexpected(body.error());
          }
        }
        new_text = JoinLines(*body);
        if (op.move_to.has_value()) {
          auto dest = ResolveWorkspacePath(workspace_root, *op.move_to);
          if (!dest.has_value()) {
            return base::unexpected(dest.error());
          }
          if (!IsTextExtensionAllowed(*dest)) {
            return base::unexpected(WorkspaceError::kBinaryRejected);
          }
          if (*dest != *path) {
            reserved = touched_paths.Reserve(workspace_root, *dest);
            if (!reserved.has_value()) {
              return base::unexpected(reserved.error());
            }
            base::File target = OpenWorkspaceFile(workspace_root, *dest);
            if (target.IsValid()) {
              return base::unexpected(WorkspaceError::kAlreadyExists);
            }
            if (target.error_details() != base::File::FILE_ERROR_NOT_FOUND) {
              return base::unexpected(WorkspaceError::kIoError);
            }
            deletes.push_back(*path);
            changes.push_back({*path, existing_size, std::nullopt});
            existing_size.reset();
            destination = *dest;
            mode = WorkspacePublishMode::kCreate;
          }
          result.moved.emplace_back(op.path, *op.move_to);
        } else {
          result.updated.push_back(op.path);
        }
        break;
      }
      case V4AFileOp::Kind::kDelete:
        changes.push_back({*path, existing_size, std::nullopt});
        deletes.push_back(*path);
        result.deleted.push_back(op.path);
        continue;
    }

    if (ContainsNulByte(new_text)) {
      return base::unexpected(WorkspaceError::kBinaryRejected);
    }
    if (new_text.size() > WorkspaceQuota::kPerFileMaxBytes) {
      return base::unexpected(WorkspaceError::kQuotaExceeded);
    }
    changes.push_back({destination, existing_size, new_text.size()});
    base::FilePath staged =
        stage.GetPath().AppendASCII(base::NumberToString(writes.size()));
    if (!base::WriteFile(staged, new_text)) {
      return base::unexpected(WorkspaceError::kIoError);
    }
    writes.push_back({staged, destination, mode});
  }

  if (!quota->CanApplyChanges(changes)) {
    return base::unexpected(WorkspaceError::kQuotaExceeded);
  }

  // A later I/O error can leave earlier commits visible.
  quota->InvalidateCache();
  for (const auto& write : writes) {
    base::File staged(write.staged,
                      base::File::FLAG_OPEN | base::File::FLAG_READ);
    if (!staged.IsValid()) {
      return base::unexpected(WorkspaceError::kIoError);
    }
    auto published = CopyToWorkspaceFile(workspace_root, write.final_dest,
                                         staged, write.mode);
    if (!published.has_value()) {
      return base::unexpected(published.error());
    }
  }
  for (const auto& path : deletes) {
    if (!DeleteWorkspaceFile(workspace_root, path)) {
      return base::unexpected(WorkspaceError::kIoError);
    }
  }
  return result;
}

}  // namespace dao
