// The signed NSIS wrapper runs this handoff without initializing WebView2.
#pragma once

#include "../../../src/dao/browser/updater/dao_update_win_util.h"
#include "update_window.h"

#include <dwmapi.h>
#include <optional>

namespace dao {
namespace installer_update {
using update_win::Handle;
using FailureLog = void (*)(const char*, DWORD);

struct Result {
  DWORD code;
  bool show_fallback = false;
};

struct Destination {
  explicit Destination(const std::wstring& browser)
      : original_browser(browser),
        application(browser.substr(0, browser.find_last_of(L'\\'))),
        root(application.substr(0, application.find_last_of(L'\\'))),
        updated_browser(application + L"\\dao.exe") {}

  // A failed installer may already have replaced or removed dao.exe. Pin the
  // original install root and require its exact per-user registration on retry.
  bool Open() {
    if (root.size() > 180 || !update_win::NoReparsePoints(root))
      return false;
    root_lock = Handle(CreateFileW(
        root.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr));
    original_file = Handle(
        CreateFileW(original_browser.c_str(), FILE_READ_ATTRIBUTES,
                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    nullptr, OPEN_EXISTING, 0, nullptr));
    return root_lock.valid() && original_file.valid();
  }

  bool StillRegistered() const {
    return update_win::ReadRegistration(HKEY_LOCAL_MACHINE).empty() &&
           update_win::ReadRegistration(HKEY_CURRENT_USER) == application &&
           update_win::NoReparsePoints(root);
  }

  static bool SafeEntryOrMissing(const std::wstring& path, bool directory) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
      const DWORD code = GetLastError();
      return code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND;
    }
    const bool is_directory = (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    return !(attributes & FILE_ATTRIBUTE_REPARSE_POINT) &&
           is_directory == directory;
  }

  bool CanInstall() const {
    if (!StillRegistered())
      return false;
    if (original_file.valid()) {
      return update_win::RegisteredBrowser() == original_browser &&
             update_win::SameFile(original_file.get(), original_browser);
    }
    return SafeEntryOrMissing(application, true) &&
           SafeEntryOrMissing(updated_browser, false) &&
           SafeEntryOrMissing(application + L"\\chrome.exe", false);
  }

  bool BrowserRunning() const {
    return update_win::AnotherBrowserRunning(updated_browser) ||
           (original_browser != updated_browser &&
            update_win::AnotherBrowserRunning(original_browser));
  }

  const std::wstring original_browser;
  const std::wstring application;
  const std::wstring root;
  const std::wstring updated_browser;
  Handle root_lock;
  Handle original_file;
};

inline DWORD StartInstaller(Destination& target,
                            const std::wstring& installer,
                            HANDLE installer_lock,
                            Handle& child) {
  for (int attempt = 0; attempt < 20 && target.BrowserRunning(); ++attempt)
    Sleep(250);
  if (target.BrowserRunning())
    return ERROR_BUSY;
  if (!target.CanInstall() || !update_win::NoReparsePoints(installer) ||
      !update_win::SameFile(installer_lock, installer))
    return ERROR_INVALID_DATA;

  std::wstring command = L"\"" + installer + L"\" /S /D=" + target.root;
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process{};
  if (!CreateProcessW(installer.c_str(), command.data(), nullptr, nullptr,
                      FALSE, CREATE_NO_WINDOW, nullptr, target.root.c_str(),
                      &startup, &process))
    return GetLastError();
  child = Handle(process.hProcess);
  CloseHandle(process.hThread);
  target.original_file = Handle();
  return ERROR_SUCCESS;
}

inline DWORD WaitForInstaller(Handle& child) {
  // Retain the handle on a failed wait: retry must observe this backend before
  // it can start another one. Never terminate a backend while it commits files.
  if (WaitForSingleObject(child.get(), INFINITE) != WAIT_OBJECT_0)
    return GetLastError();
  DWORD code = 0;
  if (!GetExitCodeProcess(child.get(), &code))
    return GetLastError();
  child = Handle();
  return code;
}

class BrowserWindows {
 public:
  explicit BrowserWindows(const Destination& target) : target_(target) {
    // WinEvent delivers out-of-context notifications on the registering thread.
    // Observe the whole desktop because Chromium may redirect a singleton
    // launch.
    hook_ = SetWinEventHook(EVENT_OBJECT_SHOW, EVENT_OBJECT_SHOW, nullptr,
                            OnWindowShown, 0, 0,
                            WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
  }
  ~BrowserWindows() {
    if (hook_)
      UnhookWinEvent(hook_);
  }

  HWND Find() const {
    MSG message{};
    for (int i = 0; i < 64 && PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE);
         ++i) {
      if (message.message == kWindowShown && !message.hwnd) {
        HWND window = reinterpret_cast<HWND>(message.wParam);
        if (Matches(window))
          return window;
      } else {
        TranslateMessage(&message);
        DispatchMessageW(&message);
      }
    }
    // Covers a show before hook delivery, a missed event, and older systems.
    Search search{this};
    EnumWindows(FindMatchingWindow, reinterpret_cast<LPARAM>(&search));
    return search.found;
  }

 private:
  static constexpr UINT kWindowShown = WM_APP + 43;
  struct Search {
    const BrowserWindows* probe;
    HWND found = nullptr;
  };

  static void CALLBACK OnWindowShown(HWINEVENTHOOK,
                                     DWORD,
                                     HWND window,
                                     LONG object,
                                     LONG child,
                                     DWORD,
                                     DWORD) {
    if (window && object == OBJID_WINDOW && child == CHILDID_SELF)
      PostThreadMessageW(GetCurrentThreadId(), kWindowShown,
                         reinterpret_cast<WPARAM>(window), 0);
  }

  static BOOL CALLBACK FindMatchingWindow(HWND window, LPARAM data) {
    auto* search = reinterpret_cast<Search*>(data);
    if (!search->probe->Matches(window))
      return TRUE;
    search->found = window;
    return FALSE;
  }

  bool Matches(HWND window) const {
    if (!IsWindowVisible(window) || IsIconic(window) ||
        GetAncestor(window, GA_ROOT) != window || GetWindow(window, GW_OWNER) ||
        !(GetWindowLongPtrW(window, GWL_STYLE) & WS_SYSMENU) ||
        (GetWindowLongPtrW(window, GWL_EXSTYLE) &
         (WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_DLGMODALFRAME)))
      return false;
    wchar_t class_name[64]{};
    // Chromium appends its per-process class registration index to this prefix.
    constexpr wchar_t prefix[] = L"Chrome_WidgetWin_";
    constexpr size_t prefix_length = ARRAYSIZE(prefix) - 1;
    if (!GetClassNameW(window, class_name, ARRAYSIZE(class_name)) ||
        wcsncmp(class_name, prefix, prefix_length) != 0)
      return false;
    const wchar_t* suffix = class_name + prefix_length;
    if (!*suffix || suffix[wcsspn(suffix, L"0123456789")] != L'\0')
      return false;
    DWORD pid = 0;
    DWORD cloaked = 0;
    RECT bounds{};
    if (!GetWindowRect(window, &bounds) || IsRectEmpty(&bounds) ||
        (SUCCEEDED(DwmGetWindowAttribute(window, DWMWA_CLOAKED, &cloaked,
                                         sizeof(cloaked))) &&
         cloaked) ||
        !GetWindowThreadProcessId(window, &pid))
      return false;
    Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid));
    return process.valid() &&
           update_win::SameFile(update_win::ProcessPath(process.get()),
                                target_.updated_browser) &&
           update_win::RegisteredBrowser() == target_.updated_browser;
  }

  const Destination& target_;
  HWINEVENTHOOK hook_ = nullptr;
};

inline DWORD RequestBrowserWindow(const Destination& target,
                                  UpdateWindow& window,
                                  Handle& launched) {
  if (!target.StillRegistered() ||
      update_win::RegisteredBrowser() != target.updated_browser)
    return ERROR_INVALID_DATA;
  if (target.original_browser != target.updated_browser &&
      update_win::AnotherBrowserRunning(target.original_browser))
    return ERROR_BUSY;

  // Send the restore request even if a background instance already exists.
  // Chromium forwards it to that instance. Only reuse our own in-flight launch.
  if (launched.valid()) {
    const DWORD wait = WaitForSingleObject(launched.get(), 0);
    if (wait == WAIT_TIMEOUT)
      return ERROR_SUCCESS;
    if (wait == WAIT_FAILED)
      return GetLastError();
  }
  std::wstring command =
      L"\"" + target.updated_browser + L"\" --restore-last-session";
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  if (!window.IsFollowing()) {
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_SHOWNOACTIVATE;
  }
  PROCESS_INFORMATION process{};
  if (!CreateProcessW(target.updated_browser.c_str(), command.data(), nullptr,
                      nullptr, FALSE, 0, nullptr, target.root.c_str(),
                      &startup, &process))
    return GetLastError();
  launched = Handle(process.hProcess);
  CloseHandle(process.hThread);
  return ERROR_SUCCESS;
}

inline std::optional<DWORD> LaunchFailure(HANDLE launched,
                                          const Destination& target) {
  const DWORD wait = WaitForSingleObject(launched, 0);
  if (wait == WAIT_TIMEOUT)
    return std::nullopt;
  if (wait == WAIT_FAILED)
    return GetLastError();
  DWORD exit_code = 0;
  if (!GetExitCodeProcess(launched, &exit_code))
    return GetLastError();
  if (exit_code != ERROR_SUCCESS)
    return exit_code;
  // A successful singleton handoff exits before the receiving browser shows.
  if (update_win::AnotherBrowserRunning(target.updated_browser))
    return std::nullopt;
  return ERROR_PROCESS_ABORTED;
}

inline DWORD ReopenBrowser(const Destination& target, UpdateWindow& window,
                           FailureLog log_failure) {
  BrowserWindows windows(target);
  Handle launched;
  for (;;) {
    window.SetStage(UpdateStage::kReopening);
    DWORD code = RequestBrowserWindow(target, window, launched);
    bool observing = code == ERROR_SUCCESS;
    if (!observing) {
      log_failure("Updated browser did not open", code);
      window.ShowIssue(UpdateIssue::kLaunchFailed, code);
    }
    const ULONGLONG deadline = GetTickCount64() + 30000;
    for (;;) {
      const auto action = window.WaitForAction(250);
      if (action == UpdateAction::kDismiss)
        return code == ERROR_SUCCESS ? ERROR_OPERATION_ABORTED : code;
      if (HWND ready = windows.Find()) {
        if (window.IsFollowing())
          SetForegroundWindow(ready);
        return ERROR_SUCCESS;
      }
      if (action == UpdateAction::kRetry)
        break;
      if (observing) {
        if (auto failure = LaunchFailure(launched.get(), target)) {
          code = *failure;
          observing = false;
          log_failure("Updated browser did not open", code);
          window.ShowIssue(UpdateIssue::kLaunchFailed, code);
        } else if (code == ERROR_SUCCESS && GetTickCount64() >= deadline) {
          code = ERROR_TIMEOUT;
          log_failure("Waiting for updated browser window", code);
          window.ShowIssue(UpdateIssue::kSlowLaunch, code);
        }
      }
    }
  }
}
}  // namespace installer_update

inline installer_update::Result RunUpdate(
    const std::wstring& token,
    const std::wstring& installer,
    bool chinese,
    installer_update::FailureLog log_failure) {
  using namespace update_win;
  using namespace installer_update;
  const std::wstring name = PipeName(token);
  const std::wstring browser = RegisteredBrowser();
  if (name.empty() || browser.empty() || !NoReparsePoints(installer))
    return {ERROR_INVALID_DATA, true};
  Handle wrapper(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE,
                             FALSE, ParentPid(GetCurrentProcessId())));
  if (!wrapper.valid())
    return {GetLastError(), true};
  Handle installer_lock(CreateFileW(installer.c_str(), GENERIC_READ,
                                    FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                    FILE_ATTRIBUTE_NORMAL, nullptr));
  if (!installer_lock.valid())
    return {GetLastError(), true};
  if (!SameFile(installer_lock.get(), ProcessPath(wrapper.get())))
    return {ERROR_ACCESS_DENIED, true};
  UserSecurity security;
  if (!security.get())
    return {ERROR_ACCESS_DENIED, true};
  Handle mutex(CreateMutexW(security.get(), TRUE, L"Local\\DaoUpdateInstall"));
  const DWORD mutex_error = GetLastError();
  if (!mutex.valid() || mutex_error == ERROR_ALREADY_EXISTS)
    return {mutex_error};
  Handle pipe(CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0,
                          nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED,
                          nullptr));
  ULONG pid = 0;
  if (!pipe.valid() || !GetNamedPipeServerProcessId(pipe.get(), &pid))
    return {GetLastError(), true};
  Destination target(browser);
  if (!target.Open())
    return {ERROR_INVALID_DATA, true};
  Handle parent(
      OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, pid));
  FILETIME expected{}, actual{};
  if (!parent.valid() ||
      !SameFile(target.original_file.get(), ProcessPath(parent.get())) ||
      !CreationTime(parent.get(), &actual))
    return {ERROR_ACCESS_DENIED, true};

  UpdateWindow window(chinese);
  // Older browsers use this same ready byte to proceed with graceful shutdown.
  // On preparation failure the authenticated browser presents its own error.
  if (!window.Start(pid))
    return {ERROR_OPEN_FAILED};
  if (!Send(pipe.get(), 'R') ||
      !Transfer(pipe.get(), &expected, sizeof(expected), false, 30000) ||
      CompareFileTime(&expected, &actual) != 0)
    return {ERROR_INVALID_DATA};
  const auto exit =
      WaitForCommitAndExit(pipe.get(), parent.get(), kHandoffTimeout, 30000);
  if (exit == ExitResult::kCancelled)
    return {ERROR_SUCCESS};

  DWORD code = exit == ExitResult::kReady ? ERROR_SUCCESS : ERROR_TIMEOUT;
  Handle backend;
  for (;;) {
    if (code == ERROR_SUCCESS && !backend.valid())
      code = StartInstaller(target, installer, installer_lock.get(), backend);
    if (code == ERROR_SUCCESS) {
      window.SetStage(UpdateStage::kInstalling);
      code = WaitForInstaller(backend);
    }
    if (code == ERROR_SUCCESS)
      break;
    log_failure("Update installation failed", code);
    window.ShowIssue(UpdateIssue::kInstallFailed, code);
    if (window.WaitForAction(INFINITE) != UpdateAction::kRetry)
      return {code};
    window.SetStage(UpdateStage::kPreparing);
    const DWORD waited = WaitForSingleObject(parent.get(), 30000);
    code = waited == WAIT_OBJECT_0  ? ERROR_SUCCESS
           : waited == WAIT_TIMEOUT ? ERROR_TIMEOUT
                                    : GetLastError();
  }

  return {ReopenBrowser(target, window, log_failure)};
}
}  // namespace dao
