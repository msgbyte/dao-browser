// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/mcp/dao_mcp_win_util.h"

// Winsock must precede Windows networking headers.
#include <winsock2.h>

#include <windows.h>

#include <iphlpapi.h>

#include <array>
#include <vector>

#include "base/compiler_specific.h"
#include "base/containers/span.h"
#include "base/numerics/safe_conversions.h"
#include "base/win/access_token.h"
#include "base/win/security_descriptor.h"

namespace dao {
namespace {

bool IsPrivateHandle(HANDLE handle, bool directory) {
  BY_HANDLE_FILE_INFORMATION info = {};
  if (!GetFileInformationByHandle(handle, &info) ||
      (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
      !!(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != directory ||
      (!directory && info.nNumberOfLinks != 1)) {
    return false;
  }
  auto token = base::win::AccessToken::FromCurrentProcess();
  auto security = base::win::SecurityDescriptor::FromHandle(
      handle, base::win::SecurityObjectType::kFile,
      OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION);
  if (!token || !security || !security->owner() ||
      *security->owner() != token->User() || !security->dacl_protected() ||
      !security->dacl() || security->dacl()->is_null()) {
    return false;
  }
  ACL* acl = security->dacl()->get();
  void* raw_ace = nullptr;
  if (acl->AceCount != 1 || !GetAce(acl, 0, &raw_ace)) {
    return false;
  }
  const auto* ace = static_cast<ACCESS_ALLOWED_ACE*>(raw_ace);
  return ace->Header.AceType == ACCESS_ALLOWED_ACE_TYPE &&
         ace->Header.AceFlags == 0 && ace->Mask == FILE_ALL_ACCESS &&
         token->User().Equal(const_cast<DWORD*>(&ace->SidStart));
}

}  // namespace

base::win::ScopedHandle OpenDaoMcpPrivatePath(const base::FilePath& path,
                                              bool directory,
                                              bool create,
                                              DWORD access) {
  auto token = base::win::AccessToken::FromCurrentProcess();
  if (!token) {
    return {};
  }
  auto security = base::win::SecurityDescriptor::CreateWithEmptyDacl();
  security.set_owner(token->User());
  security.set_dacl_protected(true);
  if (!security.SetDaclEntry(*token, base::win::SecurityAccessMode::kGrant,
                             FILE_ALL_ACCESS, 0)) {
    return {};
  }
  SECURITY_DESCRIPTOR absolute = security.ToAbsolute();
  SECURITY_ATTRIBUTES attributes = {sizeof(attributes), &absolute, FALSE};
  if (directory && create &&
      !CreateDirectoryW(path.value().c_str(), &attributes) &&
      GetLastError() != ERROR_ALREADY_EXISTS) {
    return {};
  }
  base::win::ScopedHandle handle(CreateFileW(
      path.value().c_str(), access | READ_CONTROL, FILE_SHARE_READ, &attributes,
      create && !directory ? CREATE_NEW : OPEN_EXISTING,
      FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr));
  if (handle.is_valid() && !IsPrivateHandle(handle.get(), directory)) {
    handle.Close();
    SetLastError(ERROR_ACCESS_DENIED);
  }
  return handle;
}

bool ReadDaoMcpPrivateFile(HANDLE file,
                           size_t max_bytes,
                           std::string* contents) {
  contents->clear();
  std::array<char, 4096> buffer;
  DWORD bytes = 0;
  do {
    if (!ReadFile(file, buffer.data(), base::checked_cast<DWORD>(buffer.size()),
                  &bytes, nullptr) ||
        bytes > max_bytes - contents->size()) {
      return false;
    }
    contents->append(buffer.data(), bytes);
  } while (bytes != 0);
  return true;
}

bool DeleteDaoMcpPrivateFile(const base::FilePath& path) {
  auto file = OpenDaoMcpPrivatePath(path, false, false, DELETE);
  if (!file.is_valid()) {
    return GetLastError() == ERROR_FILE_NOT_FOUND;
  }
  FILE_DISPOSITION_INFO disposition = {TRUE};
  return SetFileInformationByHandle(file.get(), FileDispositionInfo,
                                    &disposition, sizeof(disposition));
}

bool IsDaoMcpCurrentUserProcess(base::ProcessId pid) {
  base::win::ScopedHandle process(
      OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid));
  if (!process.is_valid()) {
    return false;
  }
  auto current = base::win::AccessToken::FromCurrentProcess();
  auto peer = base::win::AccessToken::FromProcess(process.get());
  return current && peer && current->User() == peer->User();
}

std::optional<base::ProcessId> GetDaoMcpLoopbackTcpOwner(uint16_t local_port,
                                                         uint16_t remote_port) {
  if (local_port == 0) {
    return std::nullopt;
  }
  DWORD size = 0;
  const TCP_TABLE_CLASS table_class = remote_port == 0
                                          ? TCP_TABLE_OWNER_PID_LISTENER
                                          : TCP_TABLE_OWNER_PID_CONNECTIONS;
  if (GetExtendedTcpTable(nullptr, &size, FALSE, AF_INET, table_class, 0) !=
      ERROR_INSUFFICIENT_BUFFER) {
    return std::nullopt;
  }
  std::vector<uint8_t> storage;
  DWORD result = ERROR_INSUFFICIENT_BUFFER;
  // The table can grow between calls while other applications open sockets.
  for (int attempt = 0; attempt < 3 && result == ERROR_INSUFFICIENT_BUFFER;
       ++attempt) {
    storage.resize(size);
    result = GetExtendedTcpTable(storage.data(), &size, FALSE, AF_INET,
                                 table_class, 0);
  }
  if (result != NO_ERROR) {
    return std::nullopt;
  }
  const auto* table =
      reinterpret_cast<const MIB_TCPTABLE_OWNER_PID*>(storage.data());
  const auto rows =
      UNSAFE_BUFFERS(base::span(table->table, table->dwNumEntries));
  std::optional<base::ProcessId> owner;
  for (const auto& row : rows) {
    if (row.dwLocalAddr != htonl(INADDR_LOOPBACK) ||
        row.dwLocalPort != htons(local_port) ||
        (remote_port == 0 ? row.dwState != MIB_TCP_STATE_LISTEN
                          : row.dwState != MIB_TCP_STATE_ESTAB ||
                                row.dwRemoteAddr != htonl(INADDR_LOOPBACK) ||
                                row.dwRemotePort != htons(remote_port))) {
      continue;
    }
    if (owner && *owner != row.dwOwningPid) {
      return std::nullopt;
    }
    owner = row.dwOwningPid;
  }
  return owner;
}

}  // namespace dao
