// The local WebView2 UI delegates all installation policy to the NSIS wrapper.
#include <windows.h>
#include <dwmapi.h>
#include <shlobj.h>
#include <wrl.h>

#include <filesystem>
#include <deque>
#include <fstream>
#include <string>
#include <vector>

#include "WebView2.h"
#include "native_strings.h"

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;
namespace fs = std::filesystem;

namespace {
constexpr int kFallback = 77;
// Ask the NSIS wrapper to launch the registered Chromium uninstaller.
constexpr int kUninstall = 78;
constexpr UINT_PTR kStartupTimer = 1;
constexpr UINT_PTR kInstallTimer = 2;
constexpr UINT kWebMessage = WM_APP + 1;
constexpr UINT kViewFailed = WM_APP + 2;
constexpr wchar_t kClassName[] = L"DaoInstallerWindow";

void Log(const char* stage, HRESULT result = S_OK) {
  wchar_t temp[MAX_PATH];
  if (GetTempPathW(MAX_PATH, temp)) {
    std::ofstream output(fs::path(temp) / L"dao_installer_ui.log", std::ios::app);
    output << stage << " (0x" << std::hex << result << ")\n";
  }
}

std::wstring JsonString(const std::wstring& value) {
  std::wstring result = L"\"";
  constexpr wchar_t hex[] = L"0123456789abcdef";
  for (wchar_t ch : value) {
    if (ch == L'\\' || ch == L'\"') {
      result += L'\\';
      result += ch;
    } else if (ch < 32) {
      result += L"\\u00";
      result += hex[(ch >> 4) & 15];
      result += hex[ch & 15];
    } else {
      result += ch;
    }
  }
  return result + L'\"';
}

std::wstring ReadIni(const fs::path& file, const wchar_t* key) {
  std::vector<wchar_t> buffer(32768);
  DWORD length = GetPrivateProfileStringW(L"Installer", key, L"", buffer.data(),
                                        static_cast<DWORD>(buffer.size()), file.c_str());
  return std::wstring(buffer.data(), length);
}

std::wstring ReadHtml(const fs::path& file) {
  std::ifstream input(file, std::ios::binary);
  std::string data((std::istreambuf_iterator<char>(input)), {});
  if (data.empty() || data.size() > 2 * 1024 * 1024)
    return {};
  int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, data.data(),
                                 static_cast<int>(data.size()), nullptr, 0);
  if (!length)
    return {};
  std::wstring result(length, L'\0');
  MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, data.data(),
                      static_cast<int>(data.size()), result.data(), length);
  return result;
}

class Installer {
 public:
  int Run(HINSTANCE instance, int show) {
    std::vector<wchar_t> module(32768);
    if (!GetModuleFileNameW(nullptr, module.data(), static_cast<DWORD>(module.size())))
      return kFallback;
    fs::path folder = fs::path(module.data()).parent_path();
    fs::path config = folder / L"session.ini";
    installer_ = ReadIni(config, L"Executable");
    directory_ = ReadIni(config, L"Directory");
    version_ = ReadIni(config, L"Version");
    chinese_ = ReadIni(config, L"Language") == L"2052";
    locked_ = ReadIni(config, L"Locked") == L"1";
    html_ = ReadHtml(folder / L"index.html");
    if (installer_.empty() || directory_.empty() || html_.empty())
      return kFallback;
    LPWSTR runtime = nullptr;
    HRESULT hr = GetAvailableCoreWebView2BrowserVersionString(nullptr, &runtime);
    bool has_runtime = SUCCEEDED(hr) && runtime && *runtime;
    CoTaskMemFree(runtime);
    if (!has_runtime)
      return kFallback;
    Log("Runtime detected");

    wchar_t temp[MAX_PATH];
    wchar_t unique[MAX_PATH];
    if (!GetTempPathW(MAX_PATH, temp) || !GetTempFileNameW(temp, L"Dao", 0, unique))
      return kFallback;
    DeleteFileW(unique);
    if (!CreateDirectoryW(unique, nullptr))
      return kFallback;
    user_data_ = unique;
    log_path_ = std::wstring(temp) + L"dao_installer.log";

    show_ = show;
    WNDCLASSW klass{};
    klass.lpfnWndProc = WindowProc;
    klass.hInstance = instance;
    klass.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(101));
    klass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    klass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    klass.lpszClassName = kClassName;
    RegisterClassW(&klass);
    const DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    UINT dpi = GetDpiForSystem();
    const int width = MulDiv(560, dpi, 96);
    const int height = MulDiv(420, dpi, 96);
    RECT work_area{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work_area, 0);
    window_ = CreateWindowExW(0, kClassName, Text(L"windowTitle"), style,
                             work_area.left + (work_area.right - work_area.left - width) / 2,
                             work_area.top + (work_area.bottom - work_area.top - height) / 2,
                             width, height, nullptr, nullptr, instance, this);
    if (!window_) {
      Cleanup();
      return kFallback;
    }
    DWM_WINDOW_CORNER_PREFERENCE corners = DWMWCP_ROUND;
    DwmSetWindowAttribute(window_, DWMWA_WINDOW_CORNER_PREFERENCE, &corners, sizeof(corners));
    const MARGINS margins{1, 1, 1, 1};
    DwmExtendFrameIntoClientArea(window_, &margins);
    // Fallback also covers a runtime that is present but cannot initialize.
    SetTimer(window_, kStartupTimer, 15000, nullptr);
    hr = CreateCoreWebView2EnvironmentWithOptions(
        nullptr, user_data_.c_str(), nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [this](HRESULT result, ICoreWebView2Environment* environment) -> HRESULT {
              Log("Environment callback", result);
              if (!window_)
                return S_OK;
              if (FAILED(result) || !environment) {
                FailView();
                return S_OK;
              }
              environment_ = environment;
              HRESULT created = environment->CreateCoreWebView2Controller(
                  window_, Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                      [this](HRESULT status, ICoreWebView2Controller* controller) -> HRESULT {
                        Log("Controller callback", status);
                        if (!window_)
                          return S_OK;
                        if (FAILED(status) || !controller) {
                          FailView();
                          return S_OK;
                        }
                        controller_ = controller;
                        if (FAILED(controller->get_CoreWebView2(&view_)) || !ConfigureView())
                          FailView();
                        return S_OK;
                      }).Get());
              if (FAILED(created))
                FailView();
              return S_OK;
            }).Get());
    if (FAILED(hr))
      FailView();

    MSG message;
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
      TranslateMessage(&message);
      DispatchMessageW(&message);
    }
    Cleanup();
    return exit_code_;
  }

 private:
  const wchar_t* Text(const wchar_t* key) const { return dao::NativeString(key, chinese_); }

  static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* app = reinterpret_cast<Installer*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
      app = static_cast<Installer*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
      SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
    }
    if (!app)
      return DefWindowProcW(window, message, wparam, lparam);
    switch (message) {
      case WM_NCCALCSIZE:
        // Keep native system-menu behavior while the page draws the title bar.
        return 0;
      case WM_SYSCOMMAND:
        if ((wparam & 0xfff0) == SC_MAXIMIZE || (wparam & 0xfff0) == SC_SIZE)
          return 0;
        break;
      case kWebMessage:
        if (!app->messages_.empty()) {
          std::wstring command = std::move(app->messages_.front());
          app->messages_.pop_front();
          app->HandleMessage(command);
        }
        return 0;
      case kViewFailed:
        app->HandleViewFailure();
        return 0;
      case WM_SIZE:
        app->Resize();
        if (app->controller_)
          app->controller_->put_IsVisible(app->ready_ && wparam != SIZE_MINIMIZED);
        return 0;
      case WM_MOVE:
        if (app->controller_)
          app->controller_->NotifyParentWindowPositionChanged();
        return 0;
      case WM_SETFOCUS:
        if (app->ready_ && app->controller_)
          app->controller_->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
        return 0;
      case WM_DPICHANGED: {
        const auto* bounds = reinterpret_cast<RECT*>(lparam);
        SetWindowPos(window, nullptr, bounds->left, bounds->top,
                     bounds->right - bounds->left, bounds->bottom - bounds->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
      }
      case WM_GETMINMAXINFO: {
        auto* info = reinterpret_cast<MINMAXINFO*>(lparam);
        UINT dpi = GetDpiForWindow(window);
        info->ptMinTrackSize = {MulDiv(560, dpi, 96), MulDiv(420, dpi, 96)};
        info->ptMaxTrackSize = info->ptMinTrackSize;
        return 0;
      }
      case WM_CLOSE:
        if (app->installing_) {
          MessageBoxW(window, app->Text(L"installingClose"), app->Text(L"windowTitle"),
                      MB_OK | MB_ICONINFORMATION);
        } else {
          app->Close(app->completed_ ? 0 : 1);
        }
        return 0;
      case WM_TIMER:
        if (wparam == kStartupTimer)
          app->FailView();
        else if (wparam == kInstallTimer)
          app->PollInstall();
        return 0;
      case WM_DESTROY:
        app->window_ = nullptr;
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
  }

  void Resize() {
    if (window_ && controller_) {
      RECT rect;
      GetClientRect(window_, &rect);
      controller_->put_Bounds(rect);
    }
  }

  bool ConfigureView() {
    if (!view_)
      return false;
    ComPtr<ICoreWebView2Settings> settings;
    if (FAILED(view_->get_Settings(&settings)))
      return false;
    settings->put_AreDevToolsEnabled(FALSE);
    settings->put_AreDefaultContextMenusEnabled(FALSE);
    settings->put_AreDefaultScriptDialogsEnabled(FALSE);
    settings->put_AreHostObjectsAllowed(FALSE);
    settings->put_IsStatusBarEnabled(FALSE);
    settings->put_IsZoomControlEnabled(FALSE);
    // Native WebView2 drag regions preserve moving and the system menu.
    ComPtr<ICoreWebView2Settings9> window_settings;
    if (FAILED(settings.As(&window_settings)) ||
        FAILED(window_settings->put_IsNonClientRegionSupportEnabled(TRUE)))
      return false;
    ComPtr<ICoreWebView2Controller2> background;
    if (SUCCEEDED(controller_.As(&background))) {
      DWORD light = 1;
      DWORD size = sizeof(light);
      RegGetValueW(HKEY_CURRENT_USER,
                   L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                   L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &light, &size);
      const COREWEBVIEW2_COLOR color = light ? COREWEBVIEW2_COLOR{255, 231, 238, 245} :
                                               COREWEBVIEW2_COLOR{255, 54, 59, 64};
      background->put_DefaultBackgroundColor(color);
    }
    EventRegistrationToken token;
    HRESULT hr = view_->add_NavigationStarting(
        Callback<ICoreWebView2NavigationStartingEventHandler>(
            [this](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs* args) -> HRESULT {
              LPWSTR uri = nullptr;
              args->get_Uri(&uri);
              // NavigateToString uses a data URL for the initial navigation;
              // the resulting document and its web messages use about:blank.
              std::wstring target = uri ? uri : L"";
              bool permitted = !navigated_ &&
                  (target == L"about:blank" ||
                   target.rfind(L"data:text/html;charset=utf-8;base64,", 0) == 0);
              Log(permitted ? "Navigation allowed" : "Navigation blocked");
              CoTaskMemFree(uri);
              if (permitted)
                navigated_ = true;
              else
                args->put_Cancel(TRUE);
              return S_OK;
            }).Get(), &token);
    if (FAILED(hr))
      return false;
    hr = view_->add_NewWindowRequested(
        Callback<ICoreWebView2NewWindowRequestedEventHandler>(
            [](ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs* args) -> HRESULT {
              return args->put_Handled(TRUE);
            }).Get(), &token);
    if (FAILED(hr))
      return false;
    hr = view_->add_PermissionRequested(
        Callback<ICoreWebView2PermissionRequestedEventHandler>(
            [](ICoreWebView2*, ICoreWebView2PermissionRequestedEventArgs* args) -> HRESULT {
              return args->put_State(COREWEBVIEW2_PERMISSION_STATE_DENY);
            }).Get(), &token);
    if (FAILED(hr))
      return false;
    hr = view_->add_WebMessageReceived(
        Callback<ICoreWebView2WebMessageReceivedEventHandler>(
            [this](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
              LPWSTR source = nullptr;
              LPWSTR message = nullptr;
              args->get_Source(&source);
              bool trusted = source && std::wstring(source) == L"about:blank";
              CoTaskMemFree(source);
              if (trusted && SUCCEEDED(args->TryGetWebMessageAsString(&message)) && message) {
                Log("Web message received");
                // Modal Win32 UI must run outside WebView2 callbacks.
                messages_.emplace_back(message);
                PostMessageW(window_, kWebMessage, 0, 0);
              }
              CoTaskMemFree(message);
              return S_OK;
            }).Get(), &token);
    if (FAILED(hr))
      return false;
    hr = view_->add_ProcessFailed(
        Callback<ICoreWebView2ProcessFailedEventHandler>(
            [this](ICoreWebView2*, ICoreWebView2ProcessFailedEventArgs*) -> HRESULT {
              FailView();
              return S_OK;
            }).Get(), &token);
    if (FAILED(hr))
      return false;
    Resize();
    hr = view_->NavigateToString(html_.c_str());
    Log("NavigateToString", hr);
    return SUCCEEDED(hr);
  }

  void Send(const std::wstring& json) {
    if (view_ && !view_failed_ && FAILED(view_->PostWebMessageAsJson(json.c_str())))
      FailView();
  }

  void HandleMessage(const std::wstring& message) {
    if (message == L"ready" && !ready_) {
      Log("Page ready");
      ready_ = true;
      KillTimer(window_, kStartupTimer);
      Send(L"{\"type\":\"init\",\"language\":" + JsonString(chinese_ ? L"zh-CN" : L"en") +
           L",\"directory\":" + JsonString(directory_) + L",\"locked\":" +
           (locked_ ? L"true" : L"false") + L",\"version\":" + JsonString(version_) +
           L",\"logPath\":" + JsonString(log_path_) + L"}");
      if (window_) {
        // A controller created while its parent is hidden starts invisible.
        controller_->put_IsVisible(TRUE);
        ShowWindow(window_, show_);
      }
      return;
    }
    if (!ready_ || view_failed_ || browsing_)
      return;
    if (message == L"minimize") {
      ShowWindow(window_, SW_MINIMIZE);
      return;
    }
    if (installing_)
      return;
    if (message == L"cancel" || message == L"finish") {
      Close(completed_ ? 0 : 1);
    } else if (message == L"uninstall" && locked_ && !completed_) {
      Close(kUninstall);
    } else if (message.rfind(L"browse:", 0) == 0 && !locked_ && !completed_) {
      Browse(message.substr(7));
    } else if (message.rfind(L"install:", 0) == 0 && !completed_) {
      Install(locked_ ? directory_ : message.substr(8));
    } else if (message == L"launch" && completed_) {
      Launch();
    }
  }

  void Browse(const std::wstring& requested) {
    ComPtr<IFileOpenDialog> dialog;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&dialog))))
      return;
    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_NOCHANGEDIR);
    // New destination folders may not exist yet. Start at their nearest parent.
    fs::path initial = directory_;
    if (requested.size() >= 3 && requested.size() <= 180 &&
        ((requested[0] >= L'A' && requested[0] <= L'Z') ||
         (requested[0] >= L'a' && requested[0] <= L'z')) &&
        requested[1] == L':' && requested[2] == L'\\' &&
        requested.find_first_of(L"\"\r\n") == std::wstring::npos)
      initial = requested;
    while (!initial.empty()) {
      const DWORD attributes = GetFileAttributesW(initial.c_str());
      if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY))
        break;
      const fs::path parent = initial.parent_path();
      if (parent == initial)
        break;
      initial = parent;
    }
    ComPtr<IShellItem> folder;
    if (SUCCEEDED(SHCreateItemFromParsingName(initial.c_str(), nullptr, IID_PPV_ARGS(&folder))))
      dialog->SetFolder(folder.Get());
    browsing_ = true;
    HRESULT shown = dialog->Show(window_);
    browsing_ = false;
    if (SUCCEEDED(shown) && window_ && !view_failed_) {
      ComPtr<IShellItem> selection;
      LPWSTR path = nullptr;
      if (SUCCEEDED(dialog->GetResult(&selection)) &&
          SUCCEEDED(selection->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
        fs::path destination(path);
        CoTaskMemFree(path);
        if (_wcsicmp(destination.filename().c_str(), L"Dao") != 0)
          destination /= L"Dao";
        directory_ = destination.wstring();
        Send(L"{\"type\":\"directory\",\"directory\":" + JsonString(directory_) + L"}");
      }
    }
  }

  void Install(const std::wstring& directory) {
    // NSIS owns policy validation; reject command-line delimiters at this boundary.
    if (directory.size() < 4 || directory.size() > 180 ||
        directory.find_first_of(L"\"\r\n") != std::wstring::npos) {
      Error(L"invalidDirectory", 10);
      return;
    }
    std::wstring command = L"\"" + installer_ + L"\" /S /D=" + directory;
    STARTUPINFOW startup{sizeof(startup)};
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(installer_.c_str(), command.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) {
      Error(L"installFailed", GetLastError());
      return;
    }
    directory_ = directory;
    process_ = process.hProcess;
    CloseHandle(process.hThread);
    installing_ = true;
    attempted_ = true;
    EnableMenuItem(GetSystemMenu(window_, FALSE), SC_CLOSE, MF_BYCOMMAND | MF_GRAYED);
    Send(L"{\"type\":\"state\",\"state\":\"installing\"}");
    SetTimer(window_, kInstallTimer, 100, nullptr);
  }

  void PollInstall() {
    if (!process_ || WaitForSingleObject(process_, 0) != WAIT_OBJECT_0)
      return;
    DWORD code = 10;
    GetExitCodeProcess(process_, &code);
    CloseHandle(process_);
    process_ = nullptr;
    installing_ = false;
    KillTimer(window_, kInstallTimer);
    EnableMenuItem(GetSystemMenu(window_, FALSE), SC_CLOSE, MF_BYCOMMAND | MF_ENABLED);
    completed_ = code == 0;
    if (view_failed_) {
      // Never launch a second installer after the backend has already started.
      MessageBoxW(window_, Text(completed_ ? L"hostCompleted" : L"hostFailed"),
                  Text(L"windowTitle"), MB_OK | MB_ICONINFORMATION);
      Close(code == 0 ? 0 : 1);
    } else if (completed_) {
      Send(L"{\"type\":\"state\",\"state\":\"complete\"}");
    } else {
      Error(L"installFailed", code);
    }
  }

  void Error(const wchar_t* error, DWORD code) {
    Send(L"{\"type\":\"state\",\"state\":\"error\",\"error\":" +
         JsonString(error) + L",\"code\":" + std::to_wstring(code) + L"}");
  }

  void Launch() {
    fs::path browser = fs::path(directory_) / L"Application" / L"chrome.exe";
    std::wstring command = L"\"" + browser.wstring() + L"\"";
    STARTUPINFOW startup{sizeof(startup)};
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(browser.c_str(), command.data(), nullptr, nullptr, FALSE, 0,
                        nullptr, browser.parent_path().c_str(), &startup, &process)) {
      Error(L"launchFailed", GetLastError());
      return;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    Close(0);
  }

  void FailView() {
    if (window_)
      PostMessageW(window_, kViewFailed, 0, 0);
  }

  void HandleViewFailure() {
    Log("WebView failure");
    if (!window_)
      return;
    view_failed_ = true;
    KillTimer(window_, kStartupTimer);
    if (!attempted_) {
      Close(kFallback);
    } else if (!installing_) {
      MessageBoxW(window_, Text(completed_ ? L"hostCompleted" : L"hostFailed"),
                  Text(L"windowTitle"), MB_OK | MB_ICONINFORMATION);
      Close(completed_ ? 0 : 1);
    }
  }

  void Close(int code) {
    exit_code_ = code;
    if (window_)
      DestroyWindow(window_);
  }

  void Cleanup() {
    UINT32 browser_pid = 0;
    if (view_)
      view_->get_BrowserProcessId(&browser_pid);
    HANDLE browser = browser_pid ? OpenProcess(SYNCHRONIZE, FALSE, browser_pid) : nullptr;
    if (controller_)
      controller_->Close();
    view_.Reset();
    controller_.Reset();
    environment_.Reset();
    if (browser) {
      WaitForSingleObject(browser, 2000);
      CloseHandle(browser);
    }
    if (!user_data_.empty()) {
      std::error_code error;
      fs::remove_all(user_data_, error);
    }
  }

  HWND window_ = nullptr;
  HANDLE process_ = nullptr;
  ComPtr<ICoreWebView2Environment> environment_;
  ComPtr<ICoreWebView2Controller> controller_;
  ComPtr<ICoreWebView2> view_;
  std::wstring installer_, directory_, version_, log_path_, html_;
  std::deque<std::wstring> messages_;
  fs::path user_data_;
  bool chinese_ = false, locked_ = false, navigated_ = false, ready_ = false, browsing_ = false;
  bool installing_ = false, attempted_ = false, completed_ = false, view_failed_ = false;
  int exit_code_ = kFallback;
  int show_ = SW_SHOW;
};
}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
  if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)))
    return kFallback;
  int code;
  {
    Installer app;
    code = app.Run(instance, show);
  }
  CoUninitialize();
  return code;
}
