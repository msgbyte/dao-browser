param([string]$NsisDir = $env:DAO_NSIS_DIR, [string]$CapturePath)

# Prerequisite: npm run rebuild -- -- --release --target dao_update_window_test_host --platform windows
# Drive the native host and NSIS wrapper on a private desktop with a disposable
# browser and installer backend. Both registry roots in the host are isolated.
$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$hostFile = Join-Path $projectRoot '.dao/installer-tests/dao-update-window-test-host.exe'
if (-not (Test-Path -LiteralPath $hostFile)) { throw 'Rebuild dao_update_window_test_host before running this test.' }
if (-not $NsisDir) { $NsisDir = Join-Path $projectRoot '.dao/tools/nsis-3.13' }
$compiler = Join-Path $NsisDir 'makensis.exe'
if (-not (Test-Path -LiteralPath $compiler)) { throw 'Set DAO_NSIS_DIR to an NSIS 3 distribution.' }
$testId = [Guid]::NewGuid().ToString('N')
$testRoot = [IO.Path]::GetFullPath((Join-Path ([IO.Path]::GetTempPath()) "dao-update-window-$testId"))
$key = "Software\DaoInstallerTests\$testId"
$registration = "$key\User\Software\Microsoft\Windows\CurrentVersion\Uninstall\Dao"
$source = Join-Path $projectRoot 'scripts/windows-installer'
$application = Join-Path $testRoot 'Dao Browser\Application'
$runner = $null
New-Item -ItemType Directory -Path $application | Out-Null
try {
    New-Item -Path "HKCU:\$key\Machine" -Force | Out-Null
    New-Item -Path "HKCU:\$registration" -Force | Out-Null
    New-ItemProperty -Path "HKCU:\$registration" -Name InstallLocation -Value $application | Out-Null
    $fixture = Join-Path $testRoot 'fixture.exe'
    Add-Type -Path (Join-Path $PSScriptRoot 'windows-update-window/fixture.cs') -OutputAssembly $fixture -OutputType WindowsApplication -ReferencedAssemblies System.Drawing,System.Windows.Forms,System.Web.Extensions
    Copy-Item -LiteralPath $fixture -Destination (Join-Path $application 'dao.exe')
    Copy-Item -LiteralPath (Join-Path $source 'locales') -Destination $testRoot -Recurse
    Copy-Item -LiteralPath (Join-Path $source 'theme.nsh') -Destination $testRoot
    foreach ($locale in @('en', 'zh-CN')) {
        Copy-Item -LiteralPath (Join-Path $source "web/locales/$locale.json") -Destination $testRoot
    }
    $wrapper = [IO.File]::ReadAllText((Join-Path $source 'installer.nsi'))
    $wrapper = $wrapper.Replace('Software\Microsoft\Windows\CurrentVersion\Uninstall\Dao', $registration)
    $wrapper = $wrapper.Replace('Function .onInit', ('Function .onInit' + [Environment]::NewLine + '  ReadEnvStr $LANGUAGE "DAO_UPDATE_TEST_LANGUAGE"'))
    $nsisScript = Join-Path $testRoot 'wrapper.nsi'
    [IO.File]::WriteAllText($nsisScript, $wrapper)
    $html = Join-Path $testRoot 'index.html'
    [IO.File]::WriteAllText($html, '<html></html>')
    $output = Join-Path $testRoot 'wrapper.exe'
    & $compiler /V2 /INPUTCHARSET UTF8 "/DPAYLOAD=$fixture" "/DOUTPUT=$output" /DVERSION=1.2.3 "/DICON=$projectRoot\branding\win\dao.ico" "/DWEBVIEW_HOST=$hostFile" "/DWEBVIEW_HTML=$html" "/DWEBVIEW_LICENSE=$html" $nsisScript
    if ($LASTEXITCODE -ne 0) { throw 'Fixture wrapper packaging failed.' }

    $info = New-Object Diagnostics.ProcessStartInfo
    $info.FileName = $fixture
    $info.Arguments = '--suite'
    $info.WorkingDirectory = $testRoot
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
    $info.EnvironmentVariables['DAO_UPDATE_WINDOW_TEST_ID'] = $testId
    $info.EnvironmentVariables['DAO_UPDATE_TEST_ROOT'] = $testRoot
    $info.EnvironmentVariables['WEBVIEW2_BROWSER_EXECUTABLE_FOLDER'] = Join-Path $testRoot 'absent-runtime'
    $info.EnvironmentVariables['TEMP'] = $testRoot
    $info.EnvironmentVariables['TMP'] = $testRoot
    $runner = [Diagnostics.Process]::Start($info)
    if (-not $runner.WaitForExit(220000)) { throw 'Timed out running update window scenarios.' }
    Get-ChildItem -LiteralPath $testRoot -Filter '*.error' -Recurse | ForEach-Object { [IO.File]::ReadAllText($_.FullName) | Write-Output }
    if ($runner.ExitCode -ne 0) { throw "Update window integration failed (exit $($runner.ExitCode))." }
    if ($CapturePath) { Copy-Item -LiteralPath (Join-Path $testRoot 'installing.png') -Destination $CapturePath }
    Write-Output 'PASS: cancellation, installation progress and retry, minimize/restore, background browser relaunch, late launch failure and recovery, and first visible window after timeout.'
} finally {
    # Every executable we start, including the NSIS-extracted host and its
    # backend, lives under this unique root. End only this run's processes.
    $expectedRoot = [IO.Path]::GetFullPath((Join-Path ([IO.Path]::GetTempPath()) "dao-update-window-$testId"))
    if ($testRoot -ne $expectedRoot) { throw 'Unsafe test cleanup path.' }
    Get-Process | Where-Object { $_.Path -and $_.Path.StartsWith(($testRoot + '\'), [StringComparison]::OrdinalIgnoreCase) } | ForEach-Object {
        if (-not $_.HasExited) { $_.Kill(); $_.WaitForExit(5000) | Out-Null }
    }
    Remove-Item -LiteralPath "HKCU:\$key" -Recurse -Force
    Remove-Item -LiteralPath $testRoot -Recurse -Force
}
