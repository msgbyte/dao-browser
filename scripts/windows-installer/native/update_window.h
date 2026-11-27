// Copyright 2026 Dao Browser Authors. All rights reserved.
#pragma once

#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <objidl.h>
#include <gdiplus.h>
#include <oleacc.h>
#include <shlwapi.h>
#include <uiautomationcore.h>
// The SDK declares its UIA property GUIDs in this header. Instantiate them
// for this standalone host, then restore the normal GUID declaration macro.
#pragma push_macro("DEFINE_GUID")
#pragma push_macro("INITGUID")
#include <initguid.h>
#include <uiautomationcoreapi.h>
#pragma pop_macro("INITGUID")
#pragma pop_macro("DEFINE_GUID")

#include <algorithm>
#include <atomic>
#include <cmath>
#include <memory>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#include "../../../src/dao/browser/updater/dao_update_win_util.h"
#include "native_strings.h"

namespace dao {

enum class UpdateStage { kPreparing, kInstalling, kReopening };
enum class UpdateIssue { kInstallFailed, kLaunchFailed, kSlowLaunch };
enum class UpdateAction { kWait, kRetry, kDismiss };

// Only the handoff worker calls the public methods. The UI thread owns all
// controls and graphics and never waits for the installer or browser.
class UpdateWindow {
 public:
  explicit UpdateWindow(bool chinese) : chinese_(chinese) {}
  UpdateWindow(const UpdateWindow&) = delete;
  UpdateWindow& operator=(const UpdateWindow&) = delete;

  ~UpdateWindow() {
    if (ui_.joinable()) {
      if (HWND window = endpoint_.load()) {
        SendMessageW(window, WM_CANCELMODE, 0, 0);
        PostMessageW(window, kFinish, 0, 0);
      }
      ui_.join();
    }
  }

  bool Start(DWORD browser_pid) {
    if (ui_.joinable() || !ready_.valid() || !answer_.valid())
      return false;
    try {
      ui_ = std::thread([this, browser_pid] { Run(browser_pid); });
    } catch (const std::system_error&) {
      return false;
    }
    const HANDLE handles[] = {ready_.get(), ui_.native_handle()};
    return WaitForMultipleObjects(2, handles, FALSE, INFINITE) ==
           WAIT_OBJECT_0;
  }

  void SetStage(UpdateStage stage) {
    if (HWND window = endpoint_.load())
      SendMessageW(window, kStage, static_cast<WPARAM>(stage), 0);
  }

  bool IsFollowing() const {
    HWND window = endpoint_.load();
    return window && window == GetForegroundWindow() && !IsIconic(window);
  }

  void ShowIssue(UpdateIssue issue, DWORD code) {
    if (HWND window = endpoint_.load())
      SendMessageW(window, kError, static_cast<WPARAM>(issue), code);
  }

  UpdateAction WaitForAction(DWORD timeout) {
    const HANDLE handles[] = {answer_.get(), ui_.native_handle()};
    // While observing the browser, wake for WinEvents as well as UI actions.
    const DWORD result = timeout == INFINITE
        ? WaitForMultipleObjects(2, handles, FALSE, INFINITE)
        : MsgWaitForMultipleObjectsEx(2, handles, timeout, QS_ALLINPUT,
                                      MWMO_INPUTAVAILABLE);
    if (result == WAIT_OBJECT_0)
      return retry_ ? UpdateAction::kRetry : UpdateAction::kDismiss;
    if (result == WAIT_TIMEOUT || result == WAIT_OBJECT_0 + 2)
      return UpdateAction::kWait;
    return UpdateAction::kDismiss;
  }

 private:
  static constexpr UINT kStage = WM_APP + 31;
  static constexpr UINT kError = WM_APP + 32;
  static constexpr UINT kFinish = WM_APP + 33;
  static constexpr UINT kLayout = WM_APP + 34;
  static constexpr UINT_PTR kAnimation = 1;
  enum ControlId { kTitle = 1001, kBody, kMinimize, kClose, kRetry, kDetails,
                   kLogText, kBack };

  const wchar_t* Text(const wchar_t* key) const {
    return NativeString(key, chinese_);
  }

  struct BrowserWindow {
    DWORD pid;
    HWND window;
  };

  static BOOL CALLBACK FindBrowserWindow(HWND window, LPARAM param) {
    auto* browser = reinterpret_cast<BrowserWindow*>(param);
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    DWORD cloaked = 0;
    if (pid == browser->pid && IsWindowVisible(window) &&
        !GetWindow(window, GW_OWNER) &&
        !(GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) &&
        (FAILED(DwmGetWindowAttribute(window, DWMWA_CLOAKED, &cloaked,
                                      sizeof(cloaked))) || !cloaked)) {
      browser->window = window;
      return FALSE;
    }
    return TRUE;
  }

  static RECT WorkArea(HMONITOR monitor) {
    MONITORINFO info{sizeof(info)};
    if (GetMonitorInfoW(monitor, &info))
      return info.rcWork;
    RECT rect{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &rect, 0);
    return rect;
  }

  static POINT Center(const RECT& rect) {
    return {rect.left + (rect.right - rect.left) / 2,
            rect.top + (rect.bottom - rect.top) / 2};
  }

  void Run(DWORD pid) {
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(com))
      return;
    ULONG_PTR token = 0;
    Gdiplus::GdiplusStartupInput input;
    if (Gdiplus::GdiplusStartup(&token, &input, nullptr) == Gdiplus::Ok) {
      if (Create(pid)) {
        endpoint_.store(window_);
        // Beforeunload can be drawn inside the browser's main HWND. Stack the
        // preparation frame behind the foreground window from the first paint.
        HWND foreground = GetForegroundWindow();
        HWND after = foreground &&
            !(GetWindowLongPtrW(foreground, GWL_EXSTYLE) & WS_EX_TOPMOST)
            ? foreground : HWND_TOP;
        if (!SetWindowPos(window_, after, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE |
                          SWP_NOACTIVATE | SWP_SHOWWINDOW))
          ShowWindow(window_, SW_SHOWNOACTIVATE);
        RedrawWindow(window_, nullptr, nullptr,
                     RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
        Announce();
        SetEvent(ready_.get());
        MSG msg{};
        while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
          if (msg.message == WM_KEYDOWN && IsDialogKey(msg))
            continue;
          TranslateMessage(&msg);
          DispatchMessageW(&msg);
        }
      }
      endpoint_.store(nullptr);
      if (window_)
        DestroyWindow(window_);
      base_.reset();
      ring_.reset();
      logo_.reset();
      if (logo_stream_)
        logo_stream_->Release();
      DeleteObject(surface_);
      DeleteObject(title_font_);
      DeleteObject(body_font_);
      DeleteObject(caption_font_);
      Gdiplus::GdiplusShutdown(token);
    }
    CoUninitialize();
  }

  bool Create(DWORD pid) {
    HINSTANCE instance = GetModuleHandleW(nullptr);
    auto resource = FindResourceW(instance, MAKEINTRESOURCEW(102), RT_RCDATA);
    if (!resource)
      return false;
    auto bytes = static_cast<const BYTE*>(
        LockResource(LoadResource(instance, resource)));
    if (!bytes || !(logo_stream_ = SHCreateMemStream(
                        bytes, SizeofResource(instance, resource))))
      return false;
    logo_.reset(Gdiplus::Bitmap::FromStream(logo_stream_));
    if (!logo_ || logo_->GetLastStatus() != Gdiplus::Ok)
      return false;

    WNDCLASSW klass{};
    klass.lpfnWndProc = WindowProc;
    klass.hInstance = instance;
    klass.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(101));
    klass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    klass.lpszClassName = L"DaoUpdateWindow";
    if (!RegisterClassW(&klass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
      return false;
    BrowserWindow browser{pid, nullptr};
    EnumWindows(FindBrowserWindow, reinterpret_cast<LPARAM>(&browser));
    RECT anchor = WorkArea(MonitorFromWindow(browser.window,
                                             MONITOR_DEFAULTTOPRIMARY));
    if (browser.window && !IsIconic(browser.window))
      GetWindowRect(browser.window, &anchor);
    POINT center = Center(anchor);
    DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX |
                  WS_CLIPCHILDREN;
    if (!CreateWindowExW(WS_EX_APPWINDOW, klass.lpszClassName,
                         Text(L"updateWindowTitle"), style, center.x - 184,
                         center.y - 134, 368, 268, nullptr, nullptr, instance,
                         this))
      return false;
    const auto add = [&](int id, const wchar_t* type, const wchar_t* text,
                         DWORD flags) {
      return CreateWindowExW(0, type, text, WS_CHILD | flags, 0, 0, 0, 0,
                             window_, reinterpret_cast<HMENU>(
                                 static_cast<INT_PTR>(id)), instance, nullptr);
    };
    const DWORD button = WS_TABSTOP | BS_OWNERDRAW;
    const DWORD label = WS_VISIBLE | SS_CENTER | SS_NOPREFIX;
    if (!add(kTitle, L"STATIC", L"", label) ||
        !add(kBody, L"STATIC", L"", label) ||
        !add(kMinimize, L"BUTTON", Text(L"minimize"), button | WS_VISIBLE) ||
        !add(kClose, L"BUTTON", Text(L"close"), button) ||
        !add(kRetry, L"BUTTON", L"", button) ||
        !add(kDetails, L"BUTTON", Text(L"updateLogDetails"), button) ||
        !add(kLogText, L"EDIT", L"", WS_TABSTOP | ES_MULTILINE | ES_READONLY |
                                          ES_AUTOVSCROLL | WS_VSCROLL) ||
        !add(kBack, L"BUTTON", Text(L"close"), button))
      return false;
    // Annotate the native STATIC itself so UIA clients can retrieve the
    // current accessible name and know to announce changes politely.
    if (SUCCEEDED(CoCreateInstance(__uuidof(CAccPropServices), nullptr,
                                   CLSCTX_INPROC_SERVER,
                                   IID_PPV_ARGS(&accessibility_)))) {
      VARIANT setting{};
      setting.vt = VT_I4;
      setting.lVal = Polite;
      accessibility_->SetHwndProp(Item(kTitle), static_cast<DWORD>(OBJID_CLIENT),
                                  CHILDID_SELF, LiveSetting_Property_GUID, setting);
    }
    DWM_WINDOW_CORNER_PREFERENCE corner = DWMWCP_ROUND;
    DwmSetWindowAttribute(window_, DWMWA_WINDOW_CORNER_PREFERENCE, &corner,
                          sizeof(corner));
    const MARGINS margins{1, 1, 1, 1};
    DwmExtendFrameIntoClientArea(window_, &margins);
    Preferences();
    RenderState();
    return true;
  }

  HWND Item(int id) const { return GetDlgItem(window_, id); }

  // A real button or edit HWND keeps system keyboard and accessibility
  // semantics even though the borderless window is not a dialog template.
  bool IsDialogKey(const MSG& msg) {
    HWND focus = GetFocus();
    if (msg.wParam == VK_TAB && !(GetKeyState(VK_CONTROL) & 0x8000)) {
      SetFocus(GetNextDlgTabItem(window_, focus == window_ ? nullptr : focus,
                                 GetKeyState(VK_SHIFT) < 0));
      return true;
    }
    if (msg.wParam == VK_RETURN) {
      int id = GetDlgCtrlID(focus);
      if (id == kMinimize || id == kClose || id == kRetry || id == kDetails ||
          id == kBack) {
        SendMessageW(focus, BM_CLICK, 0, 0);
        return true;
      }
    }
    if (msg.wParam == VK_ESCAPE && error_) {
      if (details_)
        ShowDetails(false);
      else
        Choose(false);
      return true;
    }
    return false;
  }

  static DWORD Setting(const wchar_t* path, const wchar_t* key, DWORD fallback) {
    DWORD value = 0;
    DWORD size = sizeof(value);
    return RegGetValueW(HKEY_CURRENT_USER, path, key, RRF_RT_REG_DWORD,
                        nullptr, &value, &size) == ERROR_SUCCESS ? value
                                                                  : fallback;
  }

  int Dip(int n) const { return MulDiv(n, dpi_, 96); }
  int TextPx(int n) const { return MulDiv(Dip(n), text_scale_, 100); }

  void Preferences() {
    dpi_ = GetDpiForWindow(window_);
    text_scale_ = std::clamp<DWORD>(
        Setting(L"Software\\Microsoft\\Accessibility", L"TextScaleFactor", 100),
        100, 225);
    HIGHCONTRASTW contrast{sizeof(contrast)};
    high_contrast_ = SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast),
                                            &contrast, 0) &&
                     (contrast.dwFlags & HCF_HIGHCONTRASTON);
    dark_ = !Setting(L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes"
                     L"\\Personalize", L"AppsUseLightTheme", 1);
    BOOL animations = TRUE;
    SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animations, 0);
    animate_ = animations != FALSE;
    auto color = [&](int system, COLORREF light, COLORREF dark) {
      return high_contrast_ ? GetSysColor(system) : (dark_ ? dark : light);
    };
    bg_ = color(COLOR_WINDOW, RGB(252,253,254), RGB(54,59,64));
    ink_ = color(COLOR_WINDOWTEXT, RGB(21,23,26), RGB(250,252,255));
    muted_ = color(COLOR_WINDOWTEXT, RGB(81,91,101), RGB(200,209,219));
    edge_ = color(COLOR_WINDOWFRAME, RGB(201,209,219), RGB(81,91,102));
    action_ = color(COLOR_HIGHLIGHT, RGB(48,100,165), RGB(53,107,172));
    if (surface_)
      DeleteObject(surface_);
    surface_ = CreateSolidBrush(bg_);
    BOOL dark_frame = dark_ && !high_contrast_;
    DwmSetWindowAttribute(window_, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark_frame,
                          sizeof(dark_frame));
    DwmSetWindowAttribute(window_, DWMWA_BORDER_COLOR, &edge_, sizeof(edge_));
    NONCLIENTMETRICSW metrics{sizeof(metrics)};
    SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(metrics),
                               &metrics, 0, dpi_);
    auto font = [&](HFONT& old, int size, int weight, const wchar_t* face) {
      LOGFONTW lf = metrics.lfMessageFont;
      lf.lfHeight = -size;
      lf.lfWeight = weight;
      if (face)
        wcscpy_s(lf.lfFaceName, face);
      HFONT fresh = CreateFontIndirectW(&lf);
      if (fresh) {
        DeleteObject(old);
        old = fresh;
      }
    };
    // On an English desktop GDI can fall back to a bitmap CJK font for
    // Chinese copy. Use the Windows Simplified Chinese UI face for that copy.
    const wchar_t* text_face = chinese_ && !high_contrast_ ? L"Microsoft YaHei UI" : nullptr;
    font(title_font_, TextPx(20), FW_SEMIBOLD, text_face);
    font(body_font_, TextPx(13), FW_NORMAL, text_face);
    font(caption_font_, Dip(10), FW_NORMAL, L"Marlett");
    for (int id : {kTitle, kBody, kLogText})
      SendMessageW(Item(id), WM_SETFONT, reinterpret_cast<WPARAM>(
          id == kTitle ? title_font_ : body_font_), FALSE);
    base_.reset();
    ring_.reset();
    UpdateAnimation();
  }

  void RenderState() {
    const wchar_t* title;
    const wchar_t* body;
    if (error_ && issue_ == UpdateIssue::kSlowLaunch) {
      title = L"updateLaunchSlowTitle";
      body = L"updateLaunchSlowDescription";
    } else if (error_) {
      title = issue_ == UpdateIssue::kInstallFailed ? L"updateInstallErrorTitle" : L"updateLaunchErrorTitle";
      body = issue_ == UpdateIssue::kInstallFailed ? L"updateInstallErrorDescription" : L"updateLaunchErrorDescription";
    } else if (stage_ == UpdateStage::kReopening) {
      title = L"updateReopeningTitle";
      body = L"updateReopeningDescription";
    } else if (stage_ == UpdateStage::kInstalling) {
      title = L"updateInstallingTitle";
      body = L"updateInstallingDescription";
    } else {
      title = L"updatePreparingTitle";
      body = L"updatePreparingDescription";
    }
    heading_ = Text(title);
    description_ = Text(body);
    SetWindowTextW(Item(kTitle), details_ ? Text(L"updateLogTitle") : heading_.c_str());
    SetWindowTextW(Item(kBody), description_.c_str());
    SetWindowTextW(Item(kRetry), RetryLabel());
    EnableMenuItem(GetSystemMenu(window_, FALSE), SC_CLOSE,
                   MF_BYCOMMAND | (error_ ? MF_ENABLED : MF_GRAYED));
    for (int id : {kRetry, kDetails})
      ShowWindow(Item(id), error_ && !details_ ? SW_SHOWNA : SW_HIDE);
    for (int id : {kLogText, kBack})
      ShowWindow(Item(id), details_ ? SW_SHOWNA : SW_HIDE);
    ShowWindow(Item(kClose), error_ ? SW_SHOWNA : SW_HIDE);
    ShowWindow(Item(kBody), details_ ? SW_HIDE : SW_SHOWNA);
    for (int id : {kClose, kDetails, kRetry})
      EnableWindow(Item(id), awaiting_);
    Layout();
    UpdateAnimation();
    if (IsWindowVisible(window_))
      Announce();
  }

  void Announce() {
    if (announced_ == heading_)
      return;
    announced_ = heading_;
    NotifyWinEvent(EVENT_OBJECT_LIVEREGIONCHANGED, Item(kTitle),
                   OBJID_CLIENT, CHILDID_SELF);
  }

  void Choose(bool retry) {
    if (!awaiting_)
      return;
    awaiting_ = false;
    for (int id : {kClose, kDetails, kRetry})
      EnableWindow(Item(id), FALSE);
    if (!retry)
      ShowWindow(window_, SW_HIDE);
    retry_.store(retry);
    SetEvent(answer_.get());
  }

  const wchar_t* RetryLabel() const {
    return Text(issue_ == UpdateIssue::kInstallFailed ? L"updateRetryInstall"
                                                     : L"updateOpenDao");
  }

  void UpdateLogDetails() {
    std::vector<wchar_t> dir(32768);
    DWORD length = GetTempPathW(static_cast<DWORD>(dir.size()), dir.data());
    std::wstring temp = length && length < dir.size()
                            ? std::wstring(dir.data(), length) : L"%TEMP%\\";
    std::wstring text = Text(L"errorCode");
    size_t pos = text.find(L"{code}");
    if (pos != std::wstring::npos)
      text.replace(pos, 6, std::to_wstring(code_));
    text += L"\r\n\r\n" + std::wstring(Text(L"logLabel")) + L"\r\n" +
            temp + L"dao_installer.log\r\n\r\n" + Text(L"updateHandoffLogLabel") +
            L"\r\n" + temp + L"dao_installer_ui.log";
    SetWindowTextW(Item(kLogText), text.c_str());
  }

  void ShowDetails(bool show) {
    if (!awaiting_)
      return;
    details_ = show;
    if (show)
      UpdateLogDetails();
    RenderState();
    SetFocus(Item(show ? kLogText : kDetails));
    if (show)
      SendMessageW(Item(kLogText), EM_SETSEL, 0, 0);
  }

  void UpdateAnimation() {
    KillTimer(window_, kAnimation);
    if (!error_ && animate_ && !IsIconic(window_))
      SetTimer(window_, kAnimation, 40, nullptr);
  }

  void Layout() {
    RECT bounds{};
    GetWindowRect(window_, &bounds);
    const RECT work = WorkArea(MonitorFromWindow(window_, MONITOR_DEFAULTTONEAREST));
    const int width = std::min<int>(Dip(368) + TextPx(140) - Dip(140),
                                    work.right - work.left);
    const int pad = Dip(18);
    const int text_width = std::max(1, width - 2 * pad);
    HDC dc = GetDC(window_);
    auto measure = [&](const wchar_t* text, HFONT font, bool wrap) {
      auto old = SelectObject(dc, font);
      RECT rect{0, 0, text_width, 0};
      DrawTextW(dc, text, -1, &rect, DT_CALCRECT | DT_NOPREFIX |
                (wrap ? DT_WORDBREAK : DT_SINGLELINE));
      SelectObject(dc, old);
      return SIZE{rect.right - rect.left, rect.bottom - rect.top};
    };
    auto place = [&](int id, int x, int y, int w, int h) {
      SetWindowPos(Item(id), nullptr, x, y, w, h,
                   SWP_NOZORDER | SWP_NOACTIVATE);
    };
    int cap = Dip(44);
    place(kMinimize, width - cap * (error_ ? 2 : 1), 1, cap - 1, Dip(30));
    place(kClose, width - cap, 1, cap - 1, Dip(30));
    const int diameter = Dip(error_ ? 80 : 114);
    const int top = Dip(error_ ? 30 : 45);
    const int extent = diameter + 2 * Dip(16);
    const int x = (width - extent) / 2;
    emblem_ = {x, top - Dip(16), x + extent, top - Dip(16) + extent};
    int y = details_ ? Dip(38) : top + diameter + Dip(18);
    auto title_size = measure(details_ ? Text(L"updateLogTitle") : heading_.c_str(),
                              title_font_, true);
    const int title_height = std::max<int>(TextPx(27), title_size.cy);
    place(kTitle, pad, y, text_width, title_height);
    y += title_height + Dip(6);
    if (details_) {
      const int edit_height = std::min<int>(TextPx(158), std::max<int>(
          TextPx(40), work.bottom - work.top - y - TextPx(32) - Dip(36)));
      place(kLogText, pad, y, text_width, edit_height);
      y += edit_height + Dip(12);
      place(kBack, (width - TextPx(90)) / 2, y, TextPx(90), TextPx(32));
      y += TextPx(32);
    } else {
      auto body_size = measure(description_.c_str(), body_font_, true);
      int body_height = std::max<int>(TextPx(20), body_size.cy + Dip(2));
      place(kBody, pad, y, text_width, body_height);
      y += body_height;
      if (error_) {
        const int first = measure(RetryLabel(), body_font_, false).cx + Dip(26);
        const int second = measure(Text(L"updateLogDetails"), body_font_, false).cx + Dip(26);
        const int gap = Dip(8);
        const int h = TextPx(22) + Dip(10);
        y += Dip(17);
        if (first + gap + second <= text_width) {
          int left = (width - first - gap - second) / 2;
          place(kRetry, left, y, first, h);
          place(kDetails, left + first + gap, y, second, h);
        } else {
          place(kRetry, pad, y, text_width, h);
          y += h + gap;
          place(kDetails, pad, y, text_width, h);
        }
        y += h;
      }
    }
    ReleaseDC(window_, dc);
    // Both source locales fit the 368 x 268 frame at the default text size,
    // including the English installation description wrapping onto two lines.
    const int height = std::max(Dip(268), y + Dip(error_ ? 20 : 16));
    if (!IsIconic(window_)) {
      const POINT center = Center(bounds);
      int left = std::clamp<int>(center.x - width / 2, work.left,
                                 std::max(work.left, work.right - width));
      int upper = std::clamp<int>(center.y - height / 2, work.top,
                                  std::max(work.top, work.bottom - height));
      SetWindowPos(window_, nullptr, left, upper, width, height,
                   SWP_NOACTIVATE | SWP_NOZORDER);
    }
    RedrawWindow(window_, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
  }

  static BYTE Mix(BYTE a, BYTE b, double amount) {
    return static_cast<BYTE>(std::lround(a + (b - a) * amount));
  }

  // Rasterize the stationary halo and the rotating, tapered arc once for
  // this DPI/theme. Animation only composites the two small cached images.
  void MakeEmblem(int extent) {
    using namespace Gdiplus;
    base_ = std::make_unique<Bitmap>(extent, extent, PixelFormat32bppARGB);
    ring_ = std::make_unique<Bitmap>(extent, extent, PixelFormat32bppARGB);
    Rect rect(0, 0, extent, extent);
    BitmapData base_data{}, ring_data{};
    if (base_->LockBits(&rect, ImageLockModeWrite, PixelFormat32bppARGB,
                        &base_data) != Ok)
      return;
    if (ring_->LockBits(&rect, ImageLockModeWrite, PixelFormat32bppARGB,
                        &ring_data) != Ok) {
      base_->UnlockBits(&base_data);
      return;
    }
    const double radius = Dip(error_ ? 80 : 114) / 2.0;
    const double stroke = dpi_ / 96.0 * (high_contrast_ ? 2.4 : 1.6);
    COLORREF glow = dark_ ? RGB(57,78,105) : RGB(220,236,255);
    struct Stop { double angle; BYTE alpha; COLORREF rgb; };
    const Stop stops[] = {
        {0, 0, RGB(155,194,255)}, {19, 0, RGB(155,194,255)},
        {52, 20, RGB(155,194,255)}, {119, 179, RGB(105,163,255)},
        {210, 38, RGB(179,209,255)}, {290, 88, RGB(140,187,255)},
        {360, 255, RGB(40,126,250)}};
    for (int y = 0; y < extent; ++y) {
      auto* base_row = reinterpret_cast<ARGB*>(
          static_cast<BYTE*>(base_data.Scan0) + y * base_data.Stride);
      auto* ring_row = reinterpret_cast<ARGB*>(
          static_cast<BYTE*>(ring_data.Scan0) + y * ring_data.Stride);
      for (int x = 0; x < extent; ++x) {
        double dx = x + .5 - extent / 2.0;
        double dy = y + .5 - extent / 2.0;
        double r = std::sqrt(dx * dx + dy * dy);
        double fade = std::clamp((1.28 - r / radius) / .76, 0.0, 1.0);
        double glow_amount = high_contrast_ ? 0 :
            fade * fade * (3 - 2 * fade) * (error_ ? .35 : 1);
        base_row[x] = Color::MakeARGB(255,
            Mix(GetRValue(bg_), GetRValue(glow), glow_amount),
            Mix(GetGValue(bg_), GetGValue(glow), glow_amount),
            Mix(GetBValue(bg_), GetBValue(glow), glow_amount));
        ring_row[x] = 0;
        double coverage = std::clamp(stroke / 2 + .5 -
                                     std::abs(r - radius + stroke / 2), 0.0, 1.0);
        if (!coverage || error_)
          continue;
        double angle = std::fmod(std::atan2(dy, dx) * 57.295779513 + 385, 360);
        if (high_contrast_) {
          COLORREF rgb = GetSysColor(COLOR_HIGHLIGHT);
          if (angle >= 55)
            ring_row[x] = Color::MakeARGB(Mix(0, 255, coverage),
                GetRValue(rgb), GetGValue(rgb), GetBValue(rgb));
          continue;
        }
        size_t i = 1;
        while (stops[i].angle < angle)
          ++i;
        const auto& a = stops[i - 1];
        const auto& b = stops[i];
        double t = (angle - a.angle) / (b.angle - a.angle);
        ring_row[x] = Color::MakeARGB(Mix(0, Mix(a.alpha, b.alpha, t), coverage),
            Mix(GetRValue(a.rgb), GetRValue(b.rgb), t),
            Mix(GetGValue(a.rgb), GetGValue(b.rgb), t),
            Mix(GetBValue(a.rgb), GetBValue(b.rgb), t));
      }
    }
    base_->UnlockBits(&base_data);
    ring_->UnlockBits(&ring_data);
    Graphics g(base_.get());
    g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
    g.SetPixelOffsetMode(PixelOffsetModeHalf);
    ImageAttributes attributes;
    if (high_contrast_ || dark_) {
      // Preserve the original mark's alpha. In forced colors the mark follows
      // WindowText exactly, including non-black-and-white contrast themes.
      ColorMatrix matrix{};
      matrix.m[3][3] = matrix.m[4][4] = 1;
      for (int i = 0; i < 3; ++i) {
        matrix.m[i][i] = high_contrast_ ? 0.f : -1.f;
        matrix.m[4][i] = high_contrast_ ?
            static_cast<REAL>((ink_ >> (8 * i)) & 255) / 255 : 1.f;
      }
      attributes.SetColorMatrix(&matrix);
    }
    int size = Dip(error_ ? 51 : 78);
    Rect logo_rect((extent - size) / 2, (extent - size) / 2, size, size);
    g.DrawImage(logo_.get(), logo_rect, 0, 0, logo_->GetWidth(),
                logo_->GetHeight(), UnitPixel, &attributes);
  }

  void Paint(HDC target, const RECT& clip) {
    const int w = clip.right - clip.left, h = clip.bottom - clip.top;
    if (w <= 0 || h <= 0 || !surface_)
      return;
    HDC dc = CreateCompatibleDC(target);
    HBITMAP bitmap = CreateCompatibleBitmap(target, w, h);
    if (!dc || !bitmap) {
      DeleteDC(dc);
      DeleteObject(bitmap);
      FillRect(target, &clip, surface_);
      return;
    }
    HGDIOBJ old = SelectObject(dc, bitmap);
    SetViewportOrgEx(dc, -clip.left, -clip.top, nullptr);
    FillRect(dc, &clip, surface_);
    RECT intersection{};
    const int extent = emblem_.right - emblem_.left;
    if (!details_ && IntersectRect(&intersection, &emblem_, &clip) &&
        extent > 0) {
      if (!base_ || base_->GetWidth() != static_cast<UINT>(extent))
        MakeEmblem(extent);
      using namespace Gdiplus;
      // GDI+ uses its own transform, independent of the GDI viewport origin.
      SetViewportOrgEx(dc, 0, 0, nullptr);
      {
        Graphics g(dc);
        const int x = emblem_.left - clip.left, y = emblem_.top - clip.top;
        g.DrawImage(base_.get(), x, y, extent, extent);
        if (!error_) {
          const REAL mid = extent / 2.f;
          g.TranslateTransform(x + mid, y + mid);
          const REAL angle = animate_ ? (GetTickCount64() % 2100) * (360.f / 2100) : 0.f;
          g.RotateTransform(angle);
          g.SetInterpolationMode(InterpolationModeHighQualityBilinear);
          g.DrawImage(ring_.get(), -mid, -mid, static_cast<REAL>(extent),
                      static_cast<REAL>(extent));
        }
      }
      SetViewportOrgEx(dc, -clip.left, -clip.top, nullptr);
    }
    RECT client{};
    GetClientRect(window_, &client);
    HBRUSH border = CreateSolidBrush(edge_);
    FrameRect(dc, &client, border);
    DeleteObject(border);
    SetViewportOrgEx(dc, 0, 0, nullptr);
    BitBlt(target, clip.left, clip.top, w, h, dc, 0, 0, SRCCOPY);
    SelectObject(dc, old);
    DeleteObject(bitmap);
    DeleteDC(dc);
  }

  void DrawButton(const DRAWITEMSTRUCT& item) {
    const UINT id = item.CtlID;
    bool caption = id == kMinimize || id == kClose;
    bool primary = id == kRetry;
    bool disabled = (item.itemState & ODS_DISABLED) != 0;
    RECT rect = item.rcItem;
    FillRect(item.hDC, &rect, surface_);
    bool pressed = (item.itemState & ODS_SELECTED) != 0;
    COLORREF fill = primary ? action_ : bg_;
    if (pressed)
      fill = high_contrast_ ? GetSysColor(COLOR_HIGHLIGHT) :
              (primary ? RGB(42,85,140) : (dark_ ? RGB(70,77,85) : RGB(233,238,245)));
    if (!caption || pressed) {
      HPEN pen = CreatePen(PS_SOLID, 1, primary || caption ? fill : edge_);
      HBRUSH brush = CreateSolidBrush(fill);
      HGDIOBJ old_pen = SelectObject(item.hDC, pen);
      HGDIOBJ old_brush = SelectObject(item.hDC, brush);
      RoundRect(item.hDC, rect.left, rect.top, rect.right, rect.bottom, Dip(10), Dip(10));
      SelectObject(item.hDC, old_brush);
      SelectObject(item.hDC, old_pen);
      DeleteObject(brush);
      DeleteObject(pen);
    }
    std::wstring label;
    if (caption) {
      // Marlett supplies the actual Windows minimize and close glyphs.
      label = id == kMinimize ? L"0" : L"r";
    } else {
      label = id == kBack ? Text(L"close") :
              id == kDetails ? Text(L"updateLogDetails") :
              RetryLabel();
    }
    auto old = SelectObject(item.hDC, caption ? caption_font_ : body_font_);
    SetBkMode(item.hDC, TRANSPARENT);
    COLORREF text = primary || (pressed && high_contrast_) ?
        (high_contrast_ ? GetSysColor(COLOR_HIGHLIGHTTEXT) : RGB(255,255,255)) : ink_;
    SetTextColor(item.hDC, disabled ? GetSysColor(COLOR_GRAYTEXT) : text);
    DrawTextW(item.hDC, label.c_str(), -1, &rect,
              DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOPREFIX);
    SelectObject(item.hDC, old);
    if ((item.itemState & ODS_FOCUS) && !(item.itemState & ODS_NOFOCUSRECT)) {
      InflateRect(&rect, -Dip(3), -Dip(3));
      DrawFocusRect(item.hDC, &rect);
    }
  }

  static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* self = reinterpret_cast<UpdateWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
      self = static_cast<UpdateWindow*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
      self->window_ = hwnd;
      SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (self)
      return self->HandleMessage(hwnd, msg, wp, lp);
    return DefWindowProcW(hwnd, msg, wp, lp);
  }

  LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
      case kStage:
        if (error_ || stage_ != static_cast<UpdateStage>(wp)) {
          stage_ = static_cast<UpdateStage>(wp);
          error_ = details_ = awaiting_ = false;
          RenderState();
        }
        return 0;
      case kError: {
        if (error_ && !awaiting_)
          return 0;  // A retry or dismissal is already on its way to the worker.
        const bool first = !error_;
        issue_ = static_cast<UpdateIssue>(wp);
        code_ = static_cast<DWORD>(lp);
        error_ = awaiting_ = true;
        if (details_)
          UpdateLogDetails();
        RenderState();
        if (first && GetForegroundWindow() == hwnd)
          SetFocus(Item(kRetry));
        return 0;
      }
      case kFinish:
        DestroyWindow(hwnd);
        return 0;
      case WM_DESTROY:
        if (accessibility_) {
          accessibility_->ClearHwndProps(Item(kTitle), static_cast<DWORD>(OBJID_CLIENT),
                                         CHILDID_SELF, &LiveSetting_Property_GUID, 1);
          accessibility_->Release();
          accessibility_ = nullptr;
        }
        endpoint_.store(nullptr);
        PostQuitMessage(0);
        return 0;
      case WM_NCDESTROY:
        window_ = nullptr;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        break;
      case WM_ERASEBKGND:
        return 1;
      case WM_NCCALCSIZE:
        return 0;
      case WM_NCHITTEST: {
        POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ScreenToClient(hwnd, &p);
        return p.y >= 0 && p.y < Dip(30) &&
               ChildWindowFromPointEx(hwnd, p, CWP_SKIPINVISIBLE) == hwnd ?
               HTCAPTION : HTCLIENT;
      }
      case WM_SYSCOMMAND:
        if ((wp & 0xfff0) == SC_MAXIMIZE || (wp & 0xfff0) == SC_SIZE ||
            ((wp & 0xfff0) == SC_CLOSE && !error_))
          return 0;
        break;
      case WM_CLOSE:
        Choose(false);
        return 0;
      case WM_SETFOCUS:
        if (Item(kMinimize))
          SetFocus(Item(details_ ? kLogText : (awaiting_ ? kRetry : kMinimize)));
        return 0;
      case WM_COMMAND:
        if (HIWORD(wp) != BN_CLICKED)
          break;
        switch (LOWORD(wp)) {
          case kMinimize: ShowWindow(hwnd, SW_MINIMIZE); return 0;
          case kClose: Choose(false); return 0;
          case kRetry: Choose(true); return 0;
          case kDetails: ShowDetails(true); return 0;
          case kBack: ShowDetails(false); return 0;
        }
        break;
      case WM_SIZE:
        if (Item(kBack)) {
          if (wp != SIZE_MINIMIZED && was_minimized_)
            PostMessageW(hwnd, kLayout, 0, 0);
          was_minimized_ = wp == SIZE_MINIMIZED;
          UpdateAnimation();
        }
        return 0;
      case kLayout:
        Layout();
        return 0;
      case WM_DPICHANGED: {
        const auto* bounds = reinterpret_cast<const RECT*>(lp);
        SetWindowPos(hwnd, nullptr, bounds->left, bounds->top,
                     bounds->right - bounds->left, bounds->bottom - bounds->top,
                     SWP_NOACTIVATE | SWP_NOZORDER);
        [[fallthrough]];
      }
      case WM_THEMECHANGED:
      case WM_SYSCOLORCHANGE:
      case WM_SETTINGCHANGE:
        if (Item(kBack)) {
          Preferences();
          Layout();
        }
        return 0;
      case WM_TIMER:
        if (wp == kAnimation)
          InvalidateRect(hwnd, &emblem_, FALSE);
        return 0;
      case WM_CTLCOLORSTATIC:
      case WM_CTLCOLOREDIT: {
        auto dc = reinterpret_cast<HDC>(wp);
        SetTextColor(dc, reinterpret_cast<HWND>(lp) == Item(kBody) ? muted_ : ink_);
        SetBkColor(dc, bg_);
        return reinterpret_cast<LRESULT>(surface_);
      }
      case WM_DRAWITEM:
        if (const auto* item = reinterpret_cast<DRAWITEMSTRUCT*>(lp);
            item && item->CtlType == ODT_BUTTON) {
          DrawButton(*item);
          return TRUE;
        }
        break;
      case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC dc = BeginPaint(hwnd, &ps);
        Paint(dc, ps.rcPaint);
        EndPaint(hwnd, &ps);
        return 0;
      }
      case WM_PRINTCLIENT: {
        RECT client{};
        GetClientRect(hwnd, &client);
        Paint(reinterpret_cast<HDC>(wp), client);
        return 0;
      }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
  }

  const bool chinese_;
  update_win::Handle ready_{CreateEventW(nullptr, FALSE, FALSE, nullptr)};
  update_win::Handle answer_{CreateEventW(nullptr, FALSE, FALSE, nullptr)};
  std::thread ui_;
  std::atomic<HWND> endpoint_{nullptr};
  std::atomic<bool> retry_{false};

  // These members belong exclusively to the UI thread.
  HWND window_ = nullptr;
  UpdateStage stage_ = UpdateStage::kPreparing;
  bool error_ = false, details_ = false, awaiting_ = false;
  UpdateIssue issue_ = UpdateIssue::kInstallFailed;
  bool animate_ = true, dark_ = false, high_contrast_ = false, was_minimized_ = false;
  UINT dpi_ = 96;
  DWORD text_scale_ = 100, code_ = 0;
  std::wstring heading_, description_, announced_;
  COLORREF bg_ = 0, ink_ = 0, muted_ = 0, edge_ = 0, action_ = 0;
  HBRUSH surface_ = nullptr;
  HFONT title_font_ = nullptr, body_font_ = nullptr, caption_font_ = nullptr;
  RECT emblem_{};
  IStream* logo_stream_ = nullptr;
  IAccPropServices* accessibility_ = nullptr;
  std::unique_ptr<Gdiplus::Bitmap> logo_, base_, ring_;
};

}  // namespace dao
