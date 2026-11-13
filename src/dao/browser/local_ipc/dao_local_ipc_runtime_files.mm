// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/local_ipc/dao_local_ipc_runtime_files.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstring>
#include <utility>

#include "base/containers/span.h"
#include "base/json/json_writer.h"
#include "base/numerics/safe_conversions.h"
#include "base/strings/string_number_conversions.h"
#include "crypto/random.h"
#include "dao/browser/local_ipc/dao_local_ipc_protocol.h"

namespace dao {
namespace {

constexpr int kRuntimeDirectoryMode = 0700;
constexpr int kRuntimeFileMode = 0600;
constexpr char kMetadataName[] = "runtime.json";

}  // namespace

DaoLocalIpcRuntimeFiles::DaoLocalIpcRuntimeFiles(base::FilePath user_data_dir,
                                                 base::ProcessId browser_pid,
                                                 DaoLocalIpcConfig config)
    : config_(config),
      runtime_dir_(user_data_dir.AppendASCII(config_.runtime_directory)),
      socket_path_(runtime_dir_.AppendASCII(config_.socket_name)),
      metadata_path_(runtime_dir_.AppendASCII("runtime.json")),
      browser_pid_(browser_pid) {}

DaoLocalIpcRuntimeFiles::~DaoLocalIpcRuntimeFiles() = default;

base::expected<void, DaoLocalIpcError> DaoLocalIpcRuntimeFiles::Prepare() {
  runtime_dir_fd_.reset();
  nonce_.clear();
  socket_device_.reset();
  socket_inode_.reset();
  if (!OpenAndSecureRuntimeDirectory(/*create_if_missing=*/true) ||
      !RemoveOwnedLeaf(kMetadataName, /*expect_socket=*/false) ||
      !RemoveOwnedLeaf(config_.socket_name.data(), /*expect_socket=*/true)) {
    runtime_dir_fd_.reset();
    return base::unexpected(FileError(
        "Could not prepare the private local IPC runtime directory."));
  }

  std::array<uint8_t, 32> random = crypto::RandBytesAsArray<32>();
  nonce_ = base::HexEncodeLower(random);
  return base::ok();
}

base::expected<void, DaoLocalIpcError>
DaoLocalIpcRuntimeFiles::CaptureBoundSocket(uint16_t port) {
  struct stat socket_stat = {};
  if (!VerifySocket(&socket_stat)) {
    return base::unexpected(
        FileError("Could not identify the bound local IPC Unix socket."));
  }
  socket_device_ = static_cast<uint64_t>(socket_stat.st_dev);
  socket_inode_ = static_cast<uint64_t>(socket_stat.st_ino);
  return base::ok();
}

base::expected<void, DaoLocalIpcError> DaoLocalIpcRuntimeFiles::Publish() {
  struct stat socket_before = {};
  if (nonce_.size() != 64 || !socket_device_ || !socket_inode_ ||
      !IsPinnedRuntimeDirectoryCurrent() || !VerifySocket(&socket_before) ||
      static_cast<uint64_t>(socket_before.st_dev) != *socket_device_ ||
      static_cast<uint64_t>(socket_before.st_ino) != *socket_inode_ ||
      fchmodat(runtime_dir_fd_.get(), config_.socket_name.data(),
               kRuntimeFileMode, 0) != 0) {
    RemoveOwnedLeaf(kMetadataName, /*expect_socket=*/false);
    return base::unexpected(
        FileError("Could not secure the local IPC Unix socket."));
  }
  struct stat socket_after_chmod = {};
  if (!VerifySocket(&socket_after_chmod) ||
      socket_before.st_dev != socket_after_chmod.st_dev ||
      socket_before.st_ino != socket_after_chmod.st_ino ||
      (socket_after_chmod.st_mode & 0777) != kRuntimeFileMode) {
    RemoveOwnedLeaf(kMetadataName, /*expect_socket=*/false);
    return base::unexpected(
        FileError("The local IPC Unix socket changed while being secured."));
  }

  base::DictValue metadata =
      base::DictValue()
          .Set("version", config_.protocol_version)
          .Set("socket_path", socket_path_.value())
          .Set("browser_pid", base::checked_cast<int>(browser_pid_))
          .Set("nonce", nonce_);
  std::string serialized;
  const std::string temporary_name = ".runtime.json." +
                                     std::to_string(browser_pid_) + "." +
                                     nonce_.substr(0, 16);
  if (!base::JSONWriter::Write(metadata, &serialized)) {
    return base::unexpected(
        FileError("Could not serialize private local IPC runtime metadata."));
  }

  int temporary_fd = openat(
      runtime_dir_fd_.get(), temporary_name.c_str(),
      O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, kRuntimeFileMode);
  if (temporary_fd < 0) {
    return base::unexpected(
        FileError("Could not create private local IPC runtime metadata."));
  }
  base::ScopedFD scoped_temporary_fd(temporary_fd);
  base::span<const char> remaining(serialized);
  while (!remaining.empty()) {
    const ssize_t result =
        write(scoped_temporary_fd.get(), remaining.data(), remaining.size());
    if (result < 0 && errno == EINTR) {
      continue;
    }
    if (result <= 0) {
      unlinkat(runtime_dir_fd_.get(), temporary_name.c_str(), 0);
      return base::unexpected(
          FileError("Could not write private local IPC runtime metadata."));
    }
    remaining = remaining.subspan(static_cast<size_t>(result));
  }

  struct stat metadata_stat = {};
  if (fchmod(scoped_temporary_fd.get(), kRuntimeFileMode) != 0 ||
      fsync(scoped_temporary_fd.get()) != 0 ||
      fstat(scoped_temporary_fd.get(), &metadata_stat) != 0 ||
      !S_ISREG(metadata_stat.st_mode) || metadata_stat.st_uid != getuid() ||
      !IsPinnedRuntimeDirectoryCurrent()) {
    unlinkat(runtime_dir_fd_.get(), temporary_name.c_str(), 0);
    return base::unexpected(
        FileError("Could not secure private local IPC runtime metadata."));
  }
  scoped_temporary_fd.reset();

  struct stat unexpected_metadata = {};
  if (fstatat(runtime_dir_fd_.get(), kMetadataName, &unexpected_metadata,
              AT_SYMLINK_NOFOLLOW) == 0 ||
      errno != ENOENT ||
      renameat(runtime_dir_fd_.get(), temporary_name.c_str(),
               runtime_dir_fd_.get(), kMetadataName) != 0) {
    unlinkat(runtime_dir_fd_.get(), temporary_name.c_str(), 0);
    RemoveOwnedLeaf(kMetadataName, /*expect_socket=*/false);
    return base::unexpected(
        FileError("Could not publish private local IPC runtime metadata."));
  }

  struct stat metadata_after = {};
  struct stat socket_after_publish = {};
  if (fstatat(runtime_dir_fd_.get(), kMetadataName, &metadata_after,
              AT_SYMLINK_NOFOLLOW) != 0 ||
      !S_ISREG(metadata_after.st_mode) || metadata_after.st_uid != getuid() ||
      (metadata_after.st_mode & 0777) != kRuntimeFileMode ||
      !VerifySocket(&socket_after_publish) ||
      socket_after_publish.st_dev != socket_before.st_dev ||
      socket_after_publish.st_ino != socket_before.st_ino) {
    RemoveOwnedLeaf(kMetadataName, /*expect_socket=*/false);
    return base::unexpected(
        FileError("The local IPC runtime paths changed during publication."));
  }
  return base::ok();
}

void DaoLocalIpcRuntimeFiles::Cleanup() {
  if (!runtime_dir_fd_.is_valid()) {
    OpenAndSecureRuntimeDirectory(/*create_if_missing=*/false);
  }
  if (runtime_dir_fd_.is_valid() && IsPinnedRuntimeDirectoryCurrent()) {
    RemoveOwnedLeaf(kMetadataName, /*expect_socket=*/false);
    RemoveOwnedLeaf(config_.socket_name.data(), /*expect_socket=*/true);
  }
  runtime_dir_fd_.reset();
  nonce_.clear();
  socket_device_.reset();
  socket_inode_.reset();
}

bool DaoLocalIpcRuntimeFiles::OpenAndSecureRuntimeDirectory(
    bool create_if_missing) {
  struct stat path_stat = {};
  if (lstat(runtime_dir_.value().c_str(), &path_stat) != 0) {
    if (!create_if_missing || errno != ENOENT ||
        mkdir(runtime_dir_.value().c_str(), kRuntimeDirectoryMode) != 0) {
      return false;
    }
  } else if (!S_ISDIR(path_stat.st_mode) || path_stat.st_uid != getuid()) {
    return false;
  }

  base::ScopedFD directory_fd(
      open(runtime_dir_.value().c_str(),
           O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
  if (!directory_fd.is_valid()) {
    return false;
  }
  struct stat directory_stat = {};
  if (fstat(directory_fd.get(), &directory_stat) != 0 ||
      !S_ISDIR(directory_stat.st_mode) || directory_stat.st_uid != getuid() ||
      fchmod(directory_fd.get(), kRuntimeDirectoryMode) != 0) {
    return false;
  }
  runtime_dir_fd_ = std::move(directory_fd);
  return IsPinnedRuntimeDirectoryCurrent();
}

bool DaoLocalIpcRuntimeFiles::IsPinnedRuntimeDirectoryCurrent() const {
  if (!runtime_dir_fd_.is_valid()) {
    return false;
  }
  struct stat descriptor_stat = {};
  struct stat path_stat = {};
  return fstat(runtime_dir_fd_.get(), &descriptor_stat) == 0 &&
         lstat(runtime_dir_.value().c_str(), &path_stat) == 0 &&
         S_ISDIR(descriptor_stat.st_mode) && S_ISDIR(path_stat.st_mode) &&
         descriptor_stat.st_uid == getuid() && path_stat.st_uid == getuid() &&
         descriptor_stat.st_dev == path_stat.st_dev &&
         descriptor_stat.st_ino == path_stat.st_ino;
}

bool DaoLocalIpcRuntimeFiles::RemoveOwnedLeaf(const char* name,
                                              bool expect_socket) {
  if (!runtime_dir_fd_.is_valid() || !IsPinnedRuntimeDirectoryCurrent()) {
    return false;
  }
  struct stat leaf_stat = {};
  if (fstatat(runtime_dir_fd_.get(), name, &leaf_stat, AT_SYMLINK_NOFOLLOW) !=
      0) {
    return errno == ENOENT;
  }
  const bool type_matches =
      expect_socket ? S_ISSOCK(leaf_stat.st_mode) : S_ISREG(leaf_stat.st_mode);
  return type_matches && leaf_stat.st_uid == getuid() &&
         unlinkat(runtime_dir_fd_.get(), name, 0) == 0;
}

bool DaoLocalIpcRuntimeFiles::VerifySocket(struct stat* socket_stat) const {
  return runtime_dir_fd_.is_valid() && socket_stat &&
         IsPinnedRuntimeDirectoryCurrent() &&
         fstatat(runtime_dir_fd_.get(), config_.socket_name.data(), socket_stat,
                 AT_SYMLINK_NOFOLLOW) == 0 &&
         S_ISSOCK(socket_stat->st_mode) && socket_stat->st_uid == getuid();
}

DaoLocalIpcError DaoLocalIpcRuntimeFiles::FileError(std::string message) const {
  return MakeDaoLocalIpcError("INTERNAL_ERROR", std::move(message));
}

}  // namespace dao
