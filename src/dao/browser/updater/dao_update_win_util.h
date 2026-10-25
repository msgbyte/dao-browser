// Copyright 2026 Dao Browser Authors. All rights reserved.
// Shared with the standalone installer host; keep this header free of Chromium.
#ifndef DAO_BROWSER_UPDATER_DAO_UPDATE_WIN_UTIL_H_
#define DAO_BROWSER_UPDATER_DAO_UPDATE_WIN_UTIL_H_

// windows.h defines types required by the SDK headers below.
#include <windows.h>

#include <sddl.h>
#include <tlhelp32.h>

#include <string>
#include <utility>
#include <vector>

namespace dao::update_win {
inline constexpr wchar_t kRegistration[] =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Dao";
inline constexpr DWORD kHandoffTimeout = 5 * 60 * 1000;

class Handle {
 public:
  explicit Handle(HANDLE value = nullptr) : value_(value) {}
  ~Handle() {
    if (valid())
      CloseHandle(value_);
  }
  Handle(Handle&& other) noexcept
      : value_(std::exchange(other.value_, nullptr)) {}
  Handle& operator=(Handle&& other) noexcept {
    std::swap(value_, other.value_);
    return *this;
  }
  Handle(const Handle&) = delete;
  Handle& operator=(const Handle&) = delete;
  HANDLE get() const { return value_; }
  bool valid() const { return value_ && value_ != INVALID_HANDLE_VALUE; }

 private:
  HANDLE value_;
};

class UserSecurity {
 public:
  UserSecurity() {
    HANDLE raw_token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &raw_token))
      return;
    Handle token(raw_token);
    DWORD size = 0;
    GetTokenInformation(token.get(), TokenUser, nullptr, 0, &size);
    if (size < sizeof(TOKEN_USER))
      return;
    std::vector<TOKEN_USER> data((size + sizeof(TOKEN_USER) - 1) /
                                 sizeof(TOKEN_USER));
    if (!GetTokenInformation(token.get(), TokenUser, data.data(), size, &size))
      return;
    LPWSTR sid = nullptr;
    if (!ConvertSidToStringSidW(data.front().User.Sid, &sid))
      return;
    const std::wstring sddl = L"D:P(A;OICI;GA;;;" + std::wstring(sid) + L")";
    LocalFree(sid);
    if (ConvertStringSecurityDescriptorToSecurityDescriptorW(
            sddl.c_str(), SDDL_REVISION_1, &descriptor_, nullptr)) {
      attributes_ = {sizeof(attributes_), descriptor_, FALSE};
    }
  }
  ~UserSecurity() {
    if (descriptor_)
      LocalFree(descriptor_);
  }
  SECURITY_ATTRIBUTES* get() { return descriptor_ ? &attributes_ : nullptr; }
  bool Protect(const std::wstring& path) {
    return descriptor_ &&
           SetFileSecurityW(
               path.c_str(),
               DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
               descriptor_);
  }

 private:
  PSECURITY_DESCRIPTOR descriptor_ = nullptr;
  SECURITY_ATTRIBUTES attributes_{};
};

inline bool SafeLocalPath(const std::wstring& path) {
  return path.size() >= 4 && path.size() < 32760 && path[1] == L':' &&
         path[2] == L'\\' &&
         path.find_first_of(L"\"\r\n") == std::wstring::npos &&
         path.find(L':', 2) == std::wstring::npos;
}

inline bool NoReparsePoints(std::wstring path) {
  if (!SafeLocalPath(path))
    return false;
  while (path.size() > 3) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES ||
        (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
      return false;
    path.resize(path.find_last_of(L'\\'));
  }
  return true;
}

inline std::wstring ReadRegistration(HKEY root) {
  wchar_t text[32768]{};
  DWORD bytes = sizeof(text);
  if (RegGetValueW(root, kRegistration, L"InstallLocation",
                   RRF_RT_REG_SZ | RRF_SUBKEY_WOW6432KEY, nullptr, text,
                   &bytes) != ERROR_SUCCESS)
    return {};
  std::wstring path(text);
  while (!path.empty() && path.back() == L'\\')
    path.pop_back();
  return path;
}

inline std::wstring BrowserInDirectory(const std::wstring& directory) {
  const auto slash = directory.find_last_of(L'\\');
  if (!SafeLocalPath(directory) || slash == std::wstring::npos ||
      _wcsicmp(directory.substr(slash + 1).c_str(), L"Application") != 0)
    return {};
  std::wstring path = directory + L"\\dao.exe";
  DWORD attributes = GetFileAttributesW(path.c_str());
  if (attributes == INVALID_FILE_ATTRIBUTES) {
    // Older Dao installations still use the Chromium executable name.
    if (GetLastError() != ERROR_FILE_NOT_FOUND)
      return {};
    path = directory + L"\\chrome.exe";
    attributes = GetFileAttributesW(path.c_str());
  }
  return attributes != INVALID_FILE_ATTRIBUTES &&
                 !(attributes & FILE_ATTRIBUTE_DIRECTORY) && NoReparsePoints(path)
             ? path
             : std::wstring();
}

inline std::wstring RegisteredBrowser() {
  if (!ReadRegistration(HKEY_LOCAL_MACHINE).empty())
    return {};
  return BrowserInDirectory(ReadRegistration(HKEY_CURRENT_USER));
}

inline std::wstring ProcessPath(HANDLE process) {
  std::wstring path(32768, L'\0');
  DWORD size = static_cast<DWORD>(path.size());
  if (!QueryFullProcessImageNameW(process, 0, path.data(), &size))
    return {};
  path.resize(size);
  return path;
}

inline bool SameFile(HANDLE first, const std::wstring& second) {
  if (second.empty())
    return false;
  Handle b(CreateFileW(second.c_str(), FILE_READ_ATTRIBUTES,
                       FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                       nullptr, OPEN_EXISTING, 0, nullptr));
  BY_HANDLE_FILE_INFORMATION ai{}, bi{};
  return b.valid() && GetFileInformationByHandle(first, &ai) &&
         GetFileInformationByHandle(b.get(), &bi) &&
         ai.dwVolumeSerialNumber == bi.dwVolumeSerialNumber &&
         ai.nFileIndexHigh == bi.nFileIndexHigh &&
         ai.nFileIndexLow == bi.nFileIndexLow;
}

inline bool SameFile(const std::wstring& first, const std::wstring& second) {
  if (first.empty())
    return false;
  Handle a(CreateFileW(first.c_str(), FILE_READ_ATTRIBUTES,
                       FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                       nullptr, OPEN_EXISTING, 0, nullptr));
  return a.valid() && SameFile(a.get(), second);
}

inline DWORD ParentPid(DWORD pid) {
  Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
  PROCESSENTRY32W entry{};
  entry.dwSize = sizeof(entry);
  if (!snapshot.valid() || !Process32FirstW(snapshot.get(), &entry))
    return 0;
  do {
    if (entry.th32ProcessID == pid)
      return entry.th32ParentProcessID;
  } while (Process32NextW(snapshot.get(), &entry));
  return 0;
}

inline bool AnotherBrowserRunning(const std::wstring& browser) {
  const auto slash = browser.find_last_of(L'\\');
  if (slash == std::wstring::npos)
    return true;
  const std::wstring filename = browser.substr(slash + 1);
  Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
  PROCESSENTRY32W entry{};
  entry.dwSize = sizeof(entry);
  if (!snapshot.valid() || !Process32FirstW(snapshot.get(), &entry))
    return true;
  do {
    if (_wcsicmp(entry.szExeFile, filename.c_str()) != 0)
      continue;
    Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE,
                               FALSE, entry.th32ProcessID));
    if (!process.valid())
      return true;
    if (WaitForSingleObject(process.get(), 0) != WAIT_OBJECT_0 &&
        SameFile(ProcessPath(process.get()), browser))
      return true;
  } while (Process32NextW(snapshot.get(), &entry));
  return false;
}

inline std::wstring PipeName(const std::wstring& token) {
  if (token.size() != 32 ||
      token.find_first_not_of(L"0123456789abcdef") != std::wstring::npos)
    return {};
  return L"\\\\.\\pipe\\DaoUpdate-" + token;
}

// Both sides use overlapped I/O so a dead or stalled peer cannot block
// shutdown.
inline bool Transfer(HANDLE pipe,
                     void* data,
                     DWORD size,
                     bool write,
                     DWORD timeout) {
  Handle event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
  OVERLAPPED overlapped{};
  overlapped.hEvent = event.get();
  if (!event.valid())
    return false;
  DWORD count = 0;
  BOOL done = write ? WriteFile(pipe, data, size, &count, &overlapped)
                    : ReadFile(pipe, data, size, &count, &overlapped);
  if (!done && GetLastError() != ERROR_IO_PENDING)
    return false;
  if (!done && WaitForSingleObject(event.get(), timeout) != WAIT_OBJECT_0) {
    CancelIoEx(pipe, &overlapped);
    GetOverlappedResult(pipe, &overlapped, &count, TRUE);
    return false;
  }
  return GetOverlappedResult(pipe, &overlapped, &count, TRUE) && count == size;
}

inline bool Send(HANDLE pipe, char message) {
  return Transfer(pipe, &message, 1, true, 1000);
}

enum class ExitResult { kCancelled, kParentRunning, kReady };

inline ExitResult WaitForCommitAndExit(HANDLE pipe,
                                       HANDLE parent,
                                       DWORD decision_timeout,
                                       DWORD exit_timeout) {
  char decision = 0;
  if (!Transfer(pipe, &decision, 1, false, decision_timeout) || decision != 'C')
    return ExitResult::kCancelled;
  return WaitForSingleObject(parent, exit_timeout) == WAIT_OBJECT_0
             ? ExitResult::kReady
             : ExitResult::kParentRunning;
}

inline bool CreationTime(HANDLE process, FILETIME* creation) {
  FILETIME exit{}, kernel{}, user{};
  return GetProcessTimes(process, creation, &exit, &kernel, &user);
}
}  // namespace dao::update_win
#endif  // DAO_BROWSER_UPDATER_DAO_UPDATE_WIN_UTIL_H_
