// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef DAO_BROWSER_UPDATER_DAO_UPDATE_MANIFEST_H_
#define DAO_BROWSER_UPDATER_DAO_UPDATE_MANIFEST_H_

#include <cstdint>
#include <string>
#include <string_view>

#include "base/files/file_path.h"
#include "base/types/expected.h"
#include "base/version.h"
#include "url/gurl.h"

namespace dao {
inline constexpr char kWindowsUpdateFeedUrl[] =
    "https://dao.msgbyte.com/windows-update.json";
inline constexpr char kWindowsUpdateOrigin[] =
    "https://dao-release.msgbyte.com";
inline constexpr char kWindowsUpdatePublicKey[] =
    "dz3KV6hBHuxJGYXru/dOYip1oSBH5o02nrdGZdWF8x4=";
inline constexpr int kWindowsUpdateManifestLimit = 65536;
inline constexpr int64_t kWindowsUpdateInstallerLimit = 1024LL * 1024 * 1024;

struct WindowsUpdateManifest {
  WindowsUpdateManifest();
  ~WindowsUpdateManifest();
  WindowsUpdateManifest(const WindowsUpdateManifest&);
  WindowsUpdateManifest& operator=(const WindowsUpdateManifest&);
  WindowsUpdateManifest(WindowsUpdateManifest&&);
  WindowsUpdateManifest& operator=(WindowsUpdateManifest&&);
  std::string version;
  std::string chromium_version;
  GURL url;
  int64_t size = 0;
  std::string sha256;
};
enum class WindowsUpdateManifestError { kInvalid, kNoUpdate };
using WindowsUpdateManifestResult =
    base::expected<WindowsUpdateManifest, WindowsUpdateManifestError>;

WindowsUpdateManifestResult ParseSignedWindowsUpdate(
    std::string_view envelope,
    const base::Version& current_dao,
    const base::Version& current_chromium);
WindowsUpdateManifestResult ParseSignedWindowsUpdateWithKeyForTesting(
    std::string_view envelope,
    const base::Version& current_dao,
    const base::Version& current_chromium,
    std::string_view public_key);
bool IsWindowsUpdateCacheFileName(std::string_view name);
bool VerifyWindowsUpdateFile(const base::FilePath& file,
                             const WindowsUpdateManifest& manifest);
}  // namespace dao
#endif  // DAO_BROWSER_UPDATER_DAO_UPDATE_MANIFEST_H_
