param(
    [string]$NsisDir = $env:DAO_NSIS_DIR,
    [string]$CaptureDir,
    [int]$Language = 0
)

# Exercise the real wizard with a harmless backend and isolated registry key.
# This test never installs Dao or changes its actual installation registration.
$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if (-not $NsisDir) { $NsisDir = Join-Path $projectRoot '.dao/tools/nsis-3.13' }
$compiler = Join-Path $NsisDir 'makensis.exe'
if (-not (Test-Path -LiteralPath $compiler)) { throw 'Set DAO_NSIS_DIR to an NSIS 3 distribution.' }
$testId = [Guid]::NewGuid().ToString('N')
$testRoot = Join-Path ([IO.Path]::GetTempPath()) "dao-wizard-test-$testId"
$testKey = "Software\DaoInstallerTests\$testId"
$testRegistryPath = "HKCU:\$testKey"
$oldMode = $env:DAO_INSTALLER_TEST_MODE
New-Item -ItemType Directory -Path $testRoot | Out-Null
try {
    Copy-Item -LiteralPath (Join-Path $projectRoot 'scripts/windows-installer/locales') -Destination $testRoot -Recurse
    Copy-Item -LiteralPath (Join-Path $projectRoot 'scripts/windows-installer/theme.nsh') -Destination $testRoot
    $script = [IO.File]::ReadAllText((Join-Path $projectRoot 'scripts/windows-installer/installer.nsi'))
    $script = $script.Replace('Software\Microsoft\Windows\CurrentVersion\Uninstall\Dao', $testKey)
    if ($Language) { $script = $script.Replace('Function .onInit', "Function .onInit`n  StrCpy `$LANGUAGE $Language") }
    [IO.File]::WriteAllText((Join-Path $testRoot 'installer.nsi'), $script)
    $backend = @'
using System;
using System.IO;
using Microsoft.Win32;
public class InstallerFixture {
    public static int Main(string[] args) {
        string root = null;
        foreach (string arg in args) {
            if (arg.StartsWith("--dao-install-dir=")) root = arg.Substring(18);
        }
        if (root == null) return 90;
        string mode = Environment.GetEnvironmentVariable("DAO_INSTALLER_TEST_MODE");
        if (mode == "failure") return 42;
        if (mode == "missing") return 0;
        if (mode == "slow-success") System.Threading.Thread.Sleep(1500);
        string application = Path.Combine(root, "Application");
        Directory.CreateDirectory(application);
        File.WriteAllText(Path.Combine(application, "chrome.exe"), "fixture");
        using (RegistryKey hive = RegistryKey.OpenBaseKey(RegistryHive.CurrentUser, RegistryView.Registry32))
        using (RegistryKey key = hive.CreateSubKey("TEST_KEY")) {
            key.SetValue("InstallLocation", application);
        }
        return 0;
    }
}
'@
    $backend = $backend.Replace('TEST_KEY', $testKey.Replace('\', '\\'))
    $backendPath = Join-Path $testRoot 'backend.exe'
    Add-Type -TypeDefinition $backend -OutputAssembly $backendPath -OutputType ConsoleApplication
    $outputPath = Join-Path $testRoot 'wizard.exe'
    & $compiler /V2 /INPUTCHARSET UTF8 "/DPAYLOAD=$backendPath" "/DOUTPUT=$outputPath" /DVERSION=1.2.3 "/DICON=$projectRoot\branding\win\dao.ico" (Join-Path $testRoot 'installer.nsi')
    if ($LASTEXITCODE -ne 0) { throw 'Wizard compilation failed.' }

    function Start-Wizard([string]$Arguments) {
        $info = New-Object System.Diagnostics.ProcessStartInfo
        $info.FileName = $outputPath
        $info.Arguments = $Arguments
        $info.UseShellExecute = $false
        $info.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
        return [Diagnostics.Process]::Start($info)
    }
    function Run-Wizard([string]$Destination, [int]$Expected) {
        # /D is an NSIS special argument: it must be last and unquoted.
        $process = Start-Wizard "/S /D=$Destination"
        if (-not $process.WaitForExit(20000)) {
            Stop-Process -Id $process.Id -Force
            throw 'Wizard timed out.'
        }
        if ($process.ExitCode -ne $Expected) {
            throw "Expected exit $Expected, received $($process.ExitCode): $Destination"
        }
    }
    function Clear-FixtureRegistration {
        if (Test-Path -LiteralPath $testRegistryPath) { Remove-Item -LiteralPath $testRegistryPath -Recurse }
    }

    Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @'
using System;
using System.Text;
using System.Runtime.InteropServices;
using System.Drawing;
using System.Drawing.Imaging;
public static class WizardWindow {
    public delegate bool Visitor(IntPtr window, IntPtr parameter);
    [DllImport("user32.dll")] static extern bool EnumWindows(Visitor visitor, IntPtr parameter);
    [DllImport("user32.dll")] static extern bool EnumChildWindows(IntPtr parent, Visitor visitor, IntPtr parameter);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr window, out uint pid);
    [DllImport("user32.dll")] static extern int GetDlgCtrlID(IntPtr window);
    [DllImport("user32.dll")] public static extern bool IsWindowEnabled(IntPtr window);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern IntPtr SendMessage(IntPtr window, uint message, IntPtr count, StringBuilder text);
    [DllImport("user32.dll", CharSet=CharSet.Unicode, EntryPoint="SendMessageW")] static extern IntPtr SetTextMessage(IntPtr window, uint message, IntPtr unused, string text);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr window, uint message, IntPtr wparam, IntPtr lparam);
    [StructLayout(LayoutKind.Sequential)] struct Rect { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll")] static extern bool GetWindowRect(IntPtr window, out Rect rect);
    [DllImport("user32.dll")] static extern bool PrintWindow(IntPtr window, IntPtr dc, uint flags);
    [DllImport("user32.dll")] static extern bool RedrawWindow(IntPtr window, IntPtr rect, IntPtr region, uint flags);
    public static void Capture(IntPtr window, string file) {
        RedrawWindow(window, IntPtr.Zero, IntPtr.Zero, 0x185);
        Rect rect; GetWindowRect(window, out rect);
        using (var bitmap = new Bitmap(rect.Right - rect.Left, rect.Bottom - rect.Top))
        using (var graphics = Graphics.FromImage(bitmap)) {
            var dc = graphics.GetHdc();
            try { if (!PrintWindow(window, dc, 2)) throw new Exception("Cannot capture wizard."); }
            finally { graphics.ReleaseHdc(dc); }
            bitmap.Save(file, ImageFormat.Png);
        }
    }
    public static IntPtr Find(uint pid) {
        IntPtr result = IntPtr.Zero;
        EnumWindows((window, unused) => {
            uint owner; GetWindowThreadProcessId(window, out owner);
            if (owner == pid) { result = window; return false; }
            return true;
        }, IntPtr.Zero);
        return result;
    }
    public static IntPtr Control(IntPtr window, int id) {
        IntPtr result = IntPtr.Zero;
        EnumChildWindows(window, (child, unused) => {
            if (GetDlgCtrlID(child) == id) { result = child; return false; }
            return true;
        }, IntPtr.Zero);
        return result;
    }
    public static string Text(IntPtr window) {
        var text = new StringBuilder(1024); SendMessage(window, 0xD, (IntPtr)text.Capacity, text);
        return text.ToString();
    }
    public static void CheckIcon(IntPtr window) {
        var control = Control(window, 1029);
        if (SendMessage(control, 0x173, (IntPtr)1, null) == IntPtr.Zero)
            throw new Exception("Dao branding icon is missing.");
        Rect rect; GetWindowRect(control, out rect);
        int width = rect.Right - rect.Left, height = rect.Bottom - rect.Top;
        if (width <= 0 || width != height)
            throw new Exception("Dao branding icon is stretched: " + width + "x" + height);
    }
    public static void SetText(IntPtr window, string text) { SetTextMessage(window, 0xC, IntPtr.Zero, text); }
}
'@
    function Check-DirectoryPage([string]$Destination, [bool]$Editable) {
        $process = Start-Wizard "/D=$Destination"
        try {
            if (-not $process.WaitForInputIdle(10000)) { throw 'Wizard did not become ready.' }
            $window = [WizardWindow]::Find($process.Id)
            if ($window -eq [IntPtr]::Zero) { throw 'Welcome window is missing.' }
            # The first page must expose the path directly. Never press Install.
            $control = [IntPtr]::Zero
            for ($attempt = 0; $attempt -lt 50; $attempt++) {
                Start-Sleep -Milliseconds 100
                $control = [WizardWindow]::Control($window, 1019)
                if ($control -ne [IntPtr]::Zero -and [WizardWindow]::IsWindowEnabled($control) -eq $Editable -and [WizardWindow]::Text($control) -eq $Destination) { break }
            }
            if ($control -eq [IntPtr]::Zero) { throw 'Directory page is missing.' }
            if ([WizardWindow]::IsWindowEnabled($control) -ne $Editable) { throw "Unexpected directory edit state (expected editable: $Editable)." }
            if ([WizardWindow]::Text($control) -ne $Destination) { throw "Directory page shows the wrong path: $([WizardWindow]::Text($control))" }
            [WizardWindow]::CheckIcon($window)
            if ($CaptureDir) {
                [IO.Directory]::CreateDirectory($CaptureDir) | Out-Null
                $name = if ($Editable) { 'install' } else { 'repair' }
                [WizardWindow]::Capture($window, (Join-Path $CaptureDir "$name-$Language.png"))
            }
        } finally {
            if (-not $process.HasExited) { Stop-Process -Id $process.Id -Force }
        }
    }

    $destination = Join-Path $testRoot ('Dao with spaces ' + [char]0x96ea)
    Check-DirectoryPage $destination $true
    if (Test-Path -LiteralPath $destination) { throw 'Opening the wizard wrote to the installation folder.' }
    $env:DAO_INSTALLER_TEST_MODE = 'success'
    Run-Wizard $destination 0
    if (-not (Test-Path -LiteralPath "$destination\Application\chrome.exe")) { throw 'Custom path was not passed literally.' }
    Check-DirectoryPage $destination $false
    # Repairs keep the registered directory, including when /D requests a move.
    $other = Join-Path $testRoot 'Other Dao'
    Run-Wizard $other 0
    if (Test-Path -LiteralPath "$other\Application") { throw 'Repair relocated the installation.' }
    Clear-FixtureRegistration

    # Exercise the visible page flow against the isolated backend, including edits.
    $env:DAO_INSTALLER_TEST_MODE = 'slow-success'
    $guiDestination = Join-Path $testRoot 'Edited directory'
    $process = Start-Wizard "/D=$other"
    try {
        if (-not $process.WaitForInputIdle(10000)) { throw 'Wizard did not become ready.' }
        $window = [WizardWindow]::Find($process.Id)
        for ($attempt = 0; $attempt -lt 50; $attempt++) {
            $control = [WizardWindow]::Control($window, 1019)
            if ($control -ne [IntPtr]::Zero) { break }
            Start-Sleep -Milliseconds 100
        }
        if ($control -eq [IntPtr]::Zero) { throw 'Installation path input is missing.' }
        [WizardWindow]::SetText($control, $guiDestination)
        [WizardWindow]::PostMessage($window, 0x111, [IntPtr]1, [IntPtr]::Zero) | Out-Null
        for ($attempt = 0; $attempt -lt 50; $attempt++) {
            if ([WizardWindow]::Control($window, 1004) -ne [IntPtr]::Zero) { break }
            Start-Sleep -Milliseconds 100
        }
        if ($CaptureDir) { [WizardWindow]::Capture($window, (Join-Path $CaptureDir "progress-$Language.png")) }
        for ($attempt = 0; $attempt -lt 100; $attempt++) {
            $finish = [WizardWindow]::Control($window, 1030)
            if ($finish -ne [IntPtr]::Zero) { break }
            Start-Sleep -Milliseconds 100
        }
        if ($finish -eq [IntPtr]::Zero) { throw 'Successful installation did not reach the finish page.' }
        $finishMessage = [WizardWindow]::Text([WizardWindow]::Control($window, 1031))
        if (-not $finishMessage) { throw 'The finish message is missing.' }
        [WizardWindow]::CheckIcon($window)
        if (-not (Test-Path -LiteralPath "$guiDestination\Application\chrome.exe")) { throw 'The edited installation path was ignored.' }
        if ($CaptureDir) {
            # Let nsDialogs finish showing the newly created controls before capture.
            Start-Sleep -Milliseconds 200
            [WizardWindow]::Capture($window, (Join-Path $CaptureDir "finish-$Language.png"))
        }
    } finally {
        # Do not launch the fixture chrome.exe from the finish page.
        if (-not $process.HasExited) { Stop-Process -Id $process.Id -Force }
    }
    Clear-FixtureRegistration

    $occupied = Join-Path $testRoot 'Occupied'
    New-Item -ItemType Directory -Path "$occupied\Temp" | Out-Null
    [IO.File]::WriteAllText("$occupied\Temp\important.txt", 'keep')
    Run-Wizard $occupied 10
    if ([IO.File]::ReadAllText("$occupied\Temp\important.txt") -ne 'keep') { throw 'Unrelated files changed.' }

    $env:DAO_INSTALLER_TEST_MODE = 'failure'
    Run-Wizard (Join-Path $testRoot 'Failure') 42
    $env:DAO_INSTALLER_TEST_MODE = 'missing'
    Run-Wizard (Join-Path $testRoot 'Missing') 10
    Run-Wizard ([IO.Path]::GetPathRoot($testRoot)) 10
    Run-Wizard "$($env:ProgramFiles.ToLowerInvariant())\Dao" 10
    Write-Output 'Windows wizard integration checks passed (isolated backend and registry).'
} finally {
    $env:DAO_INSTALLER_TEST_MODE = $oldMode
    if (Test-Path -LiteralPath $testRegistryPath) { Remove-Item -LiteralPath $testRegistryPath -Recurse }
    $resolvedTestRoot = [IO.Path]::GetFullPath($testRoot)
    $expectedRoot = Join-Path ([IO.Path]::GetTempPath()) "dao-wizard-test-$testId"
    if ($resolvedTestRoot -ne [IO.Path]::GetFullPath($expectedRoot)) { throw 'Unsafe test cleanup path.' }
    Remove-Item -LiteralPath $resolvedTestRoot -Recurse -Force
}
