// Compiled by test_windows_update_window.ps1 as a disposable Windows fixture.
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.Drawing.Imaging;
using System.IO;
using System.IO.Pipes;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using System.Web.Script.Serialization;
using System.Windows.Forms;

public static class UpdateFixture {
    static readonly string Root = Environment.GetEnvironmentVariable("DAO_UPDATE_TEST_ROOT");
    static readonly string Id = Environment.GetEnvironmentVariable("DAO_UPDATE_WINDOW_TEST_ID");
    static readonly string Browser = Path.Combine(Root, @"Dao Browser\Application\dao.exe");
    static string Case { get { return Environment.GetEnvironmentVariable("DAO_UPDATE_TEST_CASE"); } }
    static string Marker(string name) { return Path.Combine(Root, Case, name); }
    static void Mark(string name) { File.WriteAllText(Marker(name), ""); }
    static bool Marked(string name) { return File.Exists(Marker(name)); }
    static void Check(bool ok, string error) { if (!ok) throw new InvalidOperationException(error); }
    static void Wait(Func<bool> probe, string label, int milliseconds = 10000) {
        Stopwatch timer = Stopwatch.StartNew();
        while (!probe()) {
            if (timer.ElapsedMilliseconds >= milliseconds) throw new TimeoutException(label);
            Thread.Sleep(50);
        }
    }
    static void WaitMarker(string name) { Wait(() => Marked(name), name); }
    static Process Start(string path, string args) {
        return Process.Start(new ProcessStartInfo(path, args) {UseShellExecute = false, WorkingDirectory = Root});
    }

    // All helpers inherit the private desktop from this driver. Never switch
    // the input desktop or show fixture windows in the user's active session.
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    struct Startup {
        public int Size; public string Reserved, Desktop, Title;
        public int X, Y, Width, Height, Columns, Rows, Fill, Flags;
        public short Show, ReservedSize; public IntPtr Reserved2, In, Out, Error;
    }
    [StructLayout(LayoutKind.Sequential)]
    struct ProcessInfo { public IntPtr Process, Thread; public int Pid, Tid; }
    [DllImport("user32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    static extern IntPtr CreateDesktop(string name, IntPtr device, IntPtr devmode, int flags, int access, IntPtr security);
    [DllImport("user32.dll")] static extern bool CloseDesktop(IntPtr desktop);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    static extern bool CreateProcess(string file, StringBuilder command, IntPtr processSecurity, IntPtr threadSecurity,
        bool inherit, int flags, IntPtr environment, string directory, ref Startup startup, out ProcessInfo process);
    [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr handle);
    static int RunOnPrivateDesktop() {
        string desktop = "DaoUpdateTest-" + Id;
        IntPtr handle = CreateDesktop(desktop, IntPtr.Zero, IntPtr.Zero, 0, 0x1ff, IntPtr.Zero);
        Check(handle != IntPtr.Zero, "CreateDesktop failed: " + Marshal.GetLastWin32Error());
        try {
            Startup startup = new Startup {Size = Marshal.SizeOf(typeof(Startup)), Desktop = desktop};
            ProcessInfo result;
            string exe = Path.Combine(Root, "fixture.exe");
            Check(CreateProcess(exe, new StringBuilder("\"" + exe + "\" --driver"), IntPtr.Zero, IntPtr.Zero,
                false, 0, IntPtr.Zero, Root, ref startup, out result), "Could not start the private desktop driver.");
            using (Process driver = Process.GetProcessById(result.Pid)) {
                // The process was started with CreateProcess, so acquire a
                // query handle before exit to preserve its exit code in .NET.
                Check(driver.Handle != IntPtr.Zero, "Cannot observe the driver.");
                CloseHandle(result.Process);
                CloseHandle(result.Thread);
                Check(driver.WaitForExit(210000), "Private desktop driver timed out.");
                return driver.ExitCode;
            }
        } finally { CloseDesktop(handle); }
    }

    // Pretend to be an older, registered browser. Perform its original R /
    // creation time / C handshake with the actual native updater.
    static int OldBrowser() {
        string token = Guid.NewGuid().ToString("N");
        using (var pipe = new NamedPipeServerStream("DaoUpdate-" + token, PipeDirection.InOut, 1)) {
            using (Process wrapper = Start(Path.Combine(Root, "wrapper.exe"), "/S /DAOUPDATE=" + token))
                File.WriteAllText(Marker("wrapper.pid"), wrapper.Id.ToString());
            pipe.WaitForConnection();
            Check(pipe.ReadByte() == 'R', "Host did not acknowledge readiness.");
            byte[] creation = BitConverter.GetBytes(Process.GetCurrentProcess().StartTime.ToUniversalTime().ToFileTimeUtc());
            pipe.Write(creation, 0, creation.Length);
            pipe.Flush();
            Mark("ready");
            WaitMarker("decide");
            pipe.WriteByte((byte)(Case == "cancel" ? 'X' : 'C'));
            pipe.Flush();
            Mark("decided");
            WaitMarker("exit");
        }
        return 0;
    }

    // The real wrapper waits for this harmless backend. Gate each attempt on
    // a marker so that every assertion samples a known process lifetime.
    static int Backend(string[] args) {
        string expected = "--dao-install-dir=" + Path.GetDirectoryName(Path.GetDirectoryName(Browser));
        Check(Array.IndexOf(args, expected) >= 0, "Installer did not receive the fixture destination.");
        File.AppendAllText(Marker("install-calls"), "install\n");
        int attempt = File.ReadAllLines(Marker("install-calls")).Length;
        Mark("install-" + attempt);
        WaitMarker("finish-install-" + attempt);
        return Case == "recover" && attempt == 1 ? 42 : 0;
    }

    // The newly launched executable deliberately has a visible tool window and
    // an initially hidden main window. Only showing the latter signals success.
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    struct WindowClass {
        public uint Style; public IntPtr Proc; public int ClassExtra, WindowExtra;
        public IntPtr Instance, Icon, Cursor, Brush; public string Menu, Name;
    }
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode)] static extern IntPtr GetModuleHandle(string name);
    [DllImport("kernel32.dll", CharSet = CharSet.Ansi, ExactSpelling = true)] static extern IntPtr GetProcAddress(IntPtr module, string name);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern ushort RegisterClass(ref WindowClass window);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    static extern IntPtr CreateWindowEx(int exStyle, string cls, string name, int style, int x, int y, int w, int h,
        IntPtr parent, IntPtr menu, IntPtr instance, IntPtr data);
    [DllImport("user32.dll")] static extern bool ShowWindow(IntPtr window, int show);
    static int UpdatedBrowser(bool background) {
        if (background) {
            Mark("background-ready");
            Wait(() => Marked("launch-2"), "restore request forwarded to the existing browser", 75000);
        } else {
            File.AppendAllText(Marker("launch-calls"), "launch\n");
            // Simulate Chromium forwarding the command to its existing instance.
            if (Case == "background") {
                Mark("launch-" + File.ReadAllLines(Marker("launch-calls")).Length);
                return 0;
            }
        }
        int attempt = File.ReadAllLines(Marker("launch-calls")).Length;
        var cls = new WindowClass {Instance = GetModuleHandle(null), Name = Case == "slow" ? "Chrome_WidgetWin_3" : "Chrome_WidgetWin_1",
            Proc = GetProcAddress(GetModuleHandle("user32.dll"), "DefWindowProcW")};
        Check(RegisterClass(ref cls) != 0, "Could not register the browser window class.");
        IntPtr tool = CreateWindowEx(0x80, cls.Name, "Fixture utility", 0x10cf0000, 10, 10, 80, 80,
            IntPtr.Zero, IntPtr.Zero, cls.Instance, IntPtr.Zero);
        IntPtr main = CreateWindowEx(0, cls.Name, "Fixture browser", 0xcf0000, 10, 10, 320, 220,
            IntPtr.Zero, IntPtr.Zero, cls.Instance, IntPtr.Zero);
        Check(tool != IntPtr.Zero && main != IntPtr.Zero, "Could not create the fixture browser windows.");
        if (!background) Mark("launch-" + attempt);
        Wait(() => { Application.DoEvents(); return Marked("release-launch-" + attempt); }, "launch release",
            Case == "slow" || Case == "late-exit" ? 95000 : 10000);
        if ((Case == "recover" || Case == "late-exit") && attempt == 1) return 9;
        ShowWindow(main, 5);
        Wait(() => { Application.DoEvents(); return Marked("stop"); }, "test completion");
        return 0;
    }

    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern IntPtr FindWindow(string cls, string title);
    [DllImport("user32.dll")] static extern IntPtr GetDlgItem(IntPtr window, int id);
    [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr window);
    [DllImport("user32.dll")] static extern bool IsIconic(IntPtr window);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    static extern IntPtr SendMessageTimeout(IntPtr window, uint message, IntPtr wParam, StringBuilder buffer, uint flags, uint timeout, out IntPtr result);
    [DllImport("user32.dll")] static extern bool PostMessage(IntPtr window, uint message, IntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll")] static extern bool PrintWindow(IntPtr window, IntPtr dc, int flags);
    [StructLayout(LayoutKind.Sequential)] struct Rect { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll")] static extern bool GetClientRect(IntPtr window, out Rect rect);
    [DllImport("user32.dll")] static extern bool SetProcessDpiAwarenessContext(IntPtr context);
    [DllImport("user32.dll")] static extern bool SystemParametersInfo(uint action, uint param, out int value, uint flags);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr window, out uint processId);
    delegate void WinEvent(IntPtr hook, uint evt, IntPtr window, int objectId, int childId, uint tid, uint time);
    [DllImport("user32.dll")] static extern IntPtr SetWinEventHook(uint first, uint last, IntPtr module, WinEvent callback,
        uint pid, uint tid, uint flags);
    [DllImport("user32.dll")] static extern bool UnhookWinEvent(IntPtr hook);
    sealed class HeadlineAnnouncements : IDisposable {
        readonly WinEvent callback;
        readonly IntPtr hook;
        int count;
        public HeadlineAnnouncements(IntPtr window) {
            IntPtr headline = GetDlgItem(window, 1001);
            uint pid;
            GetWindowThreadProcessId(headline, out pid);
            callback = (h, evt, target, obj, child, tid, time) => {
                if (target == headline && obj == -4 && child == 0) count++;
            };
            hook = SetWinEventHook(0x8019, 0x8019, IntPtr.Zero, callback, pid, 0, 0);
            Check(hook != IntPtr.Zero, "Cannot observe headline live-region changes.");
        }
        public void CheckInstalling() {
            Wait(() => { Application.DoEvents(); return count > 0; }, "Installation heading was never announced", 3000);
            // Leave the installation frame animating and pump the out-of-context
            // event hook. Frame invalidation must not announce the title again.
            Thread.Sleep(350);
            Application.DoEvents();
            Check(count == 1, "The installation heading was announced more than once.");
        }
        public void Dispose() { UnhookWinEvent(hook); GC.KeepAlive(callback); }
    }
    static IntPtr Host() { return FindWindow("DaoUpdateWindow", null); }
    static string Text(IntPtr window) {
        var buffer = new StringBuilder(512);
        IntPtr result;
        Check(SendMessageTimeout(window, 13, (IntPtr)buffer.Capacity, buffer, 2, 1000, out result) != IntPtr.Zero,
            "Native control did not respond.");
        return buffer.ToString();
    }
    static void Click(IntPtr window, int control) {
        IntPtr button = GetDlgItem(window, control);
        Check(IsWindowVisible(button), "Action is missing: " + control);
        // The desktop never receives input focus. Deliver the normal native
        // button notification directly so the user can work on another desktop.
        Check(PostMessage(window, 0x111, (IntPtr)control, button), "Button notification failed.");
    }
    static Bitmap Frame(IntPtr window) {
        Rect rect;
        Check(GetClientRect(window, out rect), "Update window has no client area.");
        var image = new Bitmap(rect.Right, rect.Bottom);
        using (var graphics = Graphics.FromImage(image)) {
            IntPtr dc = graphics.GetHdc();
            try { Check(PrintWindow(window, dc, 3), "PrintWindow failed."); }
            finally { graphics.ReleaseHdc(dc); }
        }
        return image;
    }
    static void CheckAnimation(IntPtr window) {
        using (Bitmap first = Frame(window)) {
            first.Save(Path.Combine(Root, "installing.png"), ImageFormat.Png);
            int animation;
            Check(SystemParametersInfo(0x1042, 0, out animation, 0), "Could not query client-area animation.");
            // Compare samples around the halo, excluding text, native chrome
            // and controls. High-contrast or reduced-motion arcs may be static.
            if (animation == 0 || SystemInformation.HighContrast) return;
            Wait(() => {
                using (Bitmap next = Frame(window)) {
                    int changed = 0;
                    for (int y = first.Height / 8; y < first.Height * 3 / 5; y += 2)
                        for (int x = first.Width / 4; x < first.Width * 3 / 4; x += 2)
                            if (first.GetPixel(x, y) != next.GetPixel(x, y)) changed++;
                    return changed > 15;
                }
            }, "The installer is running but the halo has not moved", 3000);
        }
    }
    static void TestCase(string name, string locale) {
        Directory.CreateDirectory(Path.Combine(Root, name));
        Environment.SetEnvironmentVariable("DAO_UPDATE_TEST_CASE", name);
        Environment.SetEnvironmentVariable("DAO_UPDATE_TEST_LANGUAGE", locale == "en" ? "1033" : "2052");
        var strings = new JavaScriptSerializer().Deserialize<Dictionary<string,string>>(File.ReadAllText(Path.Combine(Root, locale + ".json")));
        using (Process old = Start(Browser, "--old-browser")) {
            WaitMarker("ready");
            IntPtr window = Host();
            Check(IsWindowVisible(window), "Preparing window was not visible at the ready handshake.");
            Action<string> stage = key => Wait(() => Host() == window && Text(GetDlgItem(window, 1001)) == strings[key],
                key, key == "updateLaunchSlowTitle" ? 45000 : 10000);
            stage("updatePreparingTitle");
            Check(Text(GetDlgItem(window, 1002)) == strings["updatePreparingDescription"], "Preparing explanation is missing.");
            using (var announcements = new HeadlineAnnouncements(window))
            using (Process wrapper = Process.GetProcessById(int.Parse(File.ReadAllText(Marker("wrapper.pid"))))) {
                Check(wrapper.Handle != IntPtr.Zero, "Cannot observe the wrapper.");
                Mark("decide");
                WaitMarker("decided");
                if (name == "cancel") {
                    Check(wrapper.WaitForExit(5000) && wrapper.ExitCode == 0, "Cancelled update did not finish cleanly.");
                    Check(Host() == IntPtr.Zero && !old.HasExited && !Marked("install-calls"), "Cancellation changed the installation.");
                    Mark("exit");
                    Check(old.WaitForExit(5000) && old.ExitCode == 0, "Original browser did not exit on request.");
                    return;
                }
                Check(!Marked("install-calls") && !old.HasExited, "Installer started before browser exit.");
                Mark("exit");
                WaitMarker("install-1");
                stage("updateInstallingTitle");
                if (name == "background") {
                    using (Process background = Start(Browser, "--background")) {
                        WaitMarker("background-ready");
                        Mark("finish-install-1");
                        WaitMarker("launch-1");
                        // The existing instance stays hidden after the first
                        // forwarded request. An explicit retry must reach it.
                        stage("updateLaunchSlowTitle");
                        Click(window, 1005);
                        WaitMarker("launch-2");
                        Mark("release-launch-2");
                        Check(wrapper.WaitForExit(10000) && wrapper.ExitCode == 0 && Host() == IntPtr.Zero,
                            "The window opened by the background browser did not finish the update.");
                        Mark("stop");
                        Check(background.WaitForExit(5000) && background.ExitCode == 0, "Background browser failed.");
                    }
                    Check(File.ReadAllLines(Marker("install-calls")).Length == 1 && File.ReadAllLines(Marker("launch-calls")).Length == 2,
                        "The background browser should receive one restore request per attempt.");
                    return;
                }
                if (name == "late-exit") {
                    Mark("finish-install-1");
                    WaitMarker("launch-1");
                    stage("updateLaunchSlowTitle");
                    Click(window, 1006);
                    stage("updateLogTitle");
                    Mark("release-launch-1");
                    string error = strings["errorCode"].Replace("{code}", "9");
                    Wait(() => Text(GetDlgItem(window, 1007)).StartsWith(error + "\r\n"), "late exit code in the open details");
                    Click(window, 1008);
                    stage("updateLaunchErrorTitle");
                    Click(window, 1005);
                    WaitMarker("launch-2");
                    Mark("release-launch-2");
                    Check(wrapper.WaitForExit(10000) && wrapper.ExitCode == 0 && Host() == IntPtr.Zero,
                        "Retry after a late crash did not reopen the browser.");
                    Check(File.ReadAllLines(Marker("install-calls")).Length == 1 && File.ReadAllLines(Marker("launch-calls")).Length == 2,
                        "Recovering from a late crash should retry only the browser.");
                    Mark("stop");
                    return;
                }
                if (name == "slow") {
                    Mark("finish-install-1");
                    WaitMarker("launch-1");
                    stage("updateReopeningTitle");
                    stage("updateLaunchSlowTitle");
                    Click(window, 1005);
                    stage("updateReopeningTitle");
                    stage("updateLaunchSlowTitle");
                    // No action after this timeout. The late window must be
                    // recognized while the recovery controls are still open.
                    Mark("release-launch-1");
                    Check(wrapper.WaitForExit(10000) && wrapper.ExitCode == 0 && Host() == IntPtr.Zero,
                        "A late visible browser left the recovery window open.");
                    Check(File.ReadAllLines(Marker("install-calls")).Length == 1 && File.ReadAllLines(Marker("launch-calls")).Length == 1,
                        "Retrying a pending launch duplicated the browser or installer.");
                    Mark("stop");
                    return;
                }
                announcements.CheckInstalling();
                CheckAnimation(window);
                Click(window, 1003);
                Wait(() => IsIconic(window), "minimize while installing");
                ShowWindow(window, 9);
                Wait(() => IsWindowVisible(window) && !IsIconic(window), "restore while installing");
                Mark("finish-install-1");
                stage("updateInstallErrorTitle");
                Check(Text(GetDlgItem(window, 1005)) == strings["updateRetryInstall"] && IsWindowVisible(GetDlgItem(window, 1006)), "Install recovery actions are missing.");
                Click(window, 1006);
                stage("updateLogTitle");
                string log = Text(GetDlgItem(window, 1007));
                Check(log.Contains("42") && log.Contains("dao_installer.log") && log.Contains("dao_installer_ui.log"),
                    "Install details omitted the failure code or a log path.");
                Click(window, 1008);
                stage("updateInstallErrorTitle");
                Click(window, 1005);
                WaitMarker("install-2");
                stage("updateInstallingTitle");
                Mark("finish-install-2");
                WaitMarker("launch-1");
                stage("updateReopeningTitle");
                // Give the window observer several polls to reject both decoys.
                Thread.Sleep(700);
                Check(Host() == window && !wrapper.HasExited, "A hidden browser or a tool window ended the update.");
                Mark("release-launch-1");
                stage("updateLaunchErrorTitle");
                Check(Text(GetDlgItem(window, 1005)) == strings["updateOpenDao"] && IsWindowVisible(GetDlgItem(window, 1006)), "Launch-only recovery actions are missing.");
                Click(window, 1005);
                WaitMarker("launch-2");
                stage("updateReopeningTitle");
                Mark("release-launch-2");
                Check(wrapper.WaitForExit(10000) && wrapper.ExitCode == 0 && Host() == IntPtr.Zero, "Visible browser did not finish the update.");
                Check(File.ReadAllLines(Marker("install-calls")).Length == 2 && File.ReadAllLines(Marker("launch-calls")).Length == 2, "Launch retry reran the installer or duplicated a launch.");
                Mark("stop");
            }
        }
    }

    [STAThread] public static int Main(string[] args) {
        try {
            if (args[0] == "--suite") return RunOnPrivateDesktop();
            if (args[0] == "--driver") {
                SetProcessDpiAwarenessContext((IntPtr)(-4));
                TestCase("late-exit", "zh-CN");
                TestCase("background", "en");
                TestCase("cancel", "en");
                TestCase("recover", "zh-CN");
                TestCase("slow", "en");
                return 0;
            }
            if (args[0] == "--old-browser") return OldBrowser();
            if (args[0] == "--background") return UpdatedBrowser(true);
            if (Array.IndexOf(args, "--restore-last-session") >= 0) return UpdatedBrowser(false);
            return Backend(args);
        } catch (Exception error) {
            File.WriteAllText(Path.Combine(Root, Process.GetCurrentProcess().Id + ".error"), error.ToString());
            return 1;
        }
    }
}
