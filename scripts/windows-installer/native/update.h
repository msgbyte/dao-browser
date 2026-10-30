// The signed NSIS wrapper runs this handoff without initializing WebView2.
#pragma once
#include "../../../src/dao/browser/updater/dao_update_win_util.h"

namespace dao {
inline int RunUpdate(const std::wstring& token, const std::wstring& installer) {
  using namespace update_win;
  const std::wstring name = PipeName(token);
  const std::wstring browser = RegisteredBrowser();
  if (name.empty() || browser.empty() || !NoReparsePoints(installer))
    return 10;
  Handle wrapper(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE,
                             FALSE, ParentPid(GetCurrentProcessId())));
  if (!wrapper.valid() || !SameFile(ProcessPath(wrapper.get()), installer))
    return 10;
  Handle installer_lock(CreateFileW(installer.c_str(), GENERIC_READ,
                                    FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                    FILE_ATTRIBUTE_NORMAL, nullptr));
  if (!installer_lock.valid())
    return 10;
  UserSecurity security;
  if (!security.get())
    return 10;
  Handle mutex(CreateMutexW(security.get(), TRUE, L"Local\\DaoUpdateInstall"));
  if (!mutex.valid() || GetLastError() == ERROR_ALREADY_EXISTS)
    return 10;
  Handle pipe(CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0,
                          nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED,
                          nullptr));
  ULONG pid = 0;
  if (!pipe.valid() || !GetNamedPipeServerProcessId(pipe.get(), &pid))
    return 10;
  Handle installed(
      CreateFileW(browser.c_str(), FILE_READ_ATTRIBUTES,
                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                  nullptr, OPEN_EXISTING, 0, nullptr));
  if (!installed.valid())
    return 10;
  Handle parent(
      OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, pid));
  FILETIME expected{}, actual{};
  if (!parent.valid() || !SameFile(ProcessPath(parent.get()), browser) ||
      !CreationTime(parent.get(), &actual) || !Send(pipe.get(), 'R') ||
      !Transfer(pipe.get(), &expected, sizeof(expected), false, 30000) ||
      CompareFileTime(&expected, &actual) != 0)
    return 10;
  // Disconnect, timeout, cancellation, or a crash before commit never installs.
  const auto exit =
      WaitForCommitAndExit(pipe.get(), parent.get(), kHandoffTimeout, 30000);
  if (exit == ExitResult::kCancelled)
    return 0;
  if (exit != ExitResult::kReady)
    return 10;
  if (RegisteredBrowser() != browser || !SameFile(installed.get(), browser))
    return 10;
  // Child processes may take a moment to follow their browser out.
  for (int attempt = 0; attempt != 20 && AnotherBrowserRunning(browser);
       ++attempt)
    Sleep(250);
  if (AnotherBrowserRunning(browser) || RegisteredBrowser() != browser)
    return 10;
  const std::wstring application = browser.substr(0, browser.find_last_of(L'\\'));
  std::wstring root = application;
  root.resize(root.find_last_of(L'\\'));
  if (root.size() > 180 || !NoReparsePoints(root))
    return 10;
  std::wstring command = L"\"" + installer + L"\" /S /D=" + root;
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process{};
  if (!CreateProcessW(installer.c_str(), command.data(), nullptr, nullptr,
                      FALSE, CREATE_NO_WINDOW, nullptr, root.c_str(), &startup,
                      &process))
    return 10;
  Handle child(process.hProcess);
  CloseHandle(process.hThread);
  // Do not terminate an installer that may be committing files or registration.
  WaitForSingleObject(child.get(), INFINITE);
  const std::wstring updated_browser = application + L"\\dao.exe";
  DWORD code = 10;
  if (!GetExitCodeProcess(child.get(), &code))
    return 10;
  if (code != 0)
    return static_cast<int>(code);
  if (RegisteredBrowser() != updated_browser ||
      AnotherBrowserRunning(updated_browser) || AnotherBrowserRunning(browser))
    return 10;
  command = L"\"" + updated_browser + L"\" --restore-last-session";
  if (!CreateProcessW(updated_browser.c_str(), command.data(), nullptr, nullptr,
                      FALSE, 0, nullptr, root.c_str(), &startup, &process))
    return 10;
  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);
  return 0;
}
}  // namespace dao
