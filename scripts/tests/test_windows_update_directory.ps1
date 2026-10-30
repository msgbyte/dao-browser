param([string]$NsisDir = $env:DAO_NSIS_DIR)

# Run the production wrapper with a harmless helper and isolated registration.
# The helper removes an empty fixture version directory while the wrapper waits.
# An inherited browser working directory must not keep that directory locked.
$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if (-not $NsisDir) { $NsisDir = Join-Path $projectRoot '.dao/tools/nsis-3.13' }
$compiler = Join-Path $NsisDir 'makensis.exe'
if (-not (Test-Path -LiteralPath $compiler)) { throw 'Set DAO_NSIS_DIR to an NSIS 3 distribution.' }
$testId = [Guid]::NewGuid().ToString('N')
$testRoot = Join-Path ([IO.Path]::GetTempPath()) "dao-update-directory-$testId"
$process = $null
New-Item -ItemType Directory -Path $testRoot | Out-Null
try {
    Copy-Item -LiteralPath (Join-Path $projectRoot 'scripts/windows-installer/locales') -Destination $testRoot -Recurse
    Copy-Item -LiteralPath (Join-Path $projectRoot 'scripts/windows-installer/theme.nsh') -Destination $testRoot
    $script = [IO.File]::ReadAllText((Join-Path $projectRoot 'scripts/windows-installer/installer.nsi'))
    $script = $script.Replace('Software\Microsoft\Windows\CurrentVersion\Uninstall\Dao', "Software\DaoInstallerTests\$testId")
    [IO.File]::WriteAllText((Join-Path $testRoot 'installer.nsi'), $script)
    $versionDir = Join-Path $testRoot 'Dao space 雪\Application\155.0.8059.86'
    New-Item -ItemType Directory -Path $versionDir -Force | Out-Null
    $helperScript = @'
Unicode true
RequestExecutionLevel user
SilentInstall silent
OutFile "${OUTPUT}"
Section
  ; Release the helper's own directory lock before probing the waiting wrapper.
  SetOutPath "$EXEDIR"
  System::Call 'kernel32::RemoveDirectoryW(w "${VERSION_DIR}") i.r0 ?e'
  Pop $1
  StrCmp $0 0 failed
  SetErrorLevel 0
  Goto done
failed:
  SetErrorLevel $1
done:
SectionEnd
'@
    $helperSource = Join-Path $testRoot 'helper.nsi'
    [IO.File]::WriteAllText($helperSource, $helperScript)
    $helper = Join-Path $testRoot 'helper.exe'
    & $compiler /V2 /INPUTCHARSET UTF8 "/DOUTPUT=$helper" "/DVERSION_DIR=$versionDir" $helperSource
    if ($LASTEXITCODE -ne 0) { throw 'Fixture packaging failed.' }
    $html = Join-Path $testRoot 'index.html'
    [IO.File]::WriteAllText($html, '<html></html>')
    $wrapper = Join-Path $testRoot 'wrapper.exe'
    & $compiler /V2 /INPUTCHARSET UTF8 "/DPAYLOAD=$helper" "/DOUTPUT=$wrapper" /DVERSION=1.2.3 "/DICON=$projectRoot\branding\win\dao.ico" "/DWEBVIEW_HOST=$helper" "/DWEBVIEW_HTML=$html" "/DWEBVIEW_LICENSE=$html" (Join-Path $testRoot 'installer.nsi')
    if ($LASTEXITCODE -ne 0) { throw 'Wrapper packaging failed.' }
    $info = New-Object Diagnostics.ProcessStartInfo
    $info.FileName = $wrapper
    $info.Arguments = '/S /DAOUPDATE=0123456789abcdef0123456789abcdef'
    $info.WorkingDirectory = $versionDir
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
    $process = [Diagnostics.Process]::Start($info)
    if (-not $process.WaitForExit(15000)) { throw 'Update wrapper timed out.' }
    if ($process.ExitCode -ne 0) {
        throw "Update wrapper kept the old version directory locked (helper exit $($process.ExitCode))."
    }
    if (Test-Path -LiteralPath $versionDir) { throw 'The old version directory was not removed.' }
    Write-Output 'PASS: the update wrapper releases an inherited version directory before waiting for its helper.'
} finally {
    if ($process -and -not $process.HasExited) { Stop-Process -Id $process.Id -Force }
    $resolvedTestRoot = [IO.Path]::GetFullPath($testRoot)
    $expectedRoot = Join-Path ([IO.Path]::GetTempPath()) "dao-update-directory-$testId"
    if ($resolvedTestRoot -ne [IO.Path]::GetFullPath($expectedRoot)) { throw 'Unsafe test cleanup path.' }
    Remove-Item -LiteralPath $resolvedTestRoot -Recurse -Force
}
