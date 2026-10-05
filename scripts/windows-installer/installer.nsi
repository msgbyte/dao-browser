; Chromium remains the owner of installation, registration and uninstallation.
Unicode true
ManifestDPIAware true
RequestExecutionLevel user
SetCompressor /FINAL zlib
Name "Dao"
OutFile "${OUTPUT}"
InstallDir "$LOCALAPPDATA\Dao"
BrandingText "Dao ${VERSION}"
ShowInstDetails hide
VIProductVersion "${VERSION}.0"
VIAddVersionKey /LANG=1033 "ProductName" "Dao"
VIAddVersionKey /LANG=1033 "FileDescription" "Dao Installer"
VIAddVersionKey /LANG=1033 "FileVersion" "${VERSION}"
VIAddVersionKey /LANG=1033 "ProductVersion" "${VERSION}"
VIAddVersionKey /LANG=1033 "LegalCopyright" "The Dao Authors"

!include "MUI2.nsh"
!include "FileFunc.nsh"
!include "LogicLib.nsh"
!include "x64.nsh"

Var ExistingRoot
Var PathError
Var ExitCode

!define UNINSTALL_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\Dao"
!define MUI_ICON "${ICON}"
!define MUI_BGCOLOR "FFFFFF"
!define MUI_TEXTCOLOR "253549"
!define MUI_CUSTOMFUNCTION_GUIINIT InitializeTheme
!define MUI_ABORTWARNING
!define MUI_WELCOMEPAGE_TEXT "$(WelcomeText)"
!define MUI_PAGE_CUSTOMFUNCTION_SHOW WelcomeShow
!define MUI_PAGE_CUSTOMFUNCTION_LEAVE WelcomeLeave
!define MUI_PAGE_CUSTOMFUNCTION_DESTROYED ReleasePageIcon
!insertmacro MUI_PAGE_WELCOME
!define MUI_PAGE_HEADER_TEXT "$(InstallingTitle)"
!define MUI_PAGE_HEADER_SUBTEXT "$(InstallingSubtitle)"
!define MUI_PAGE_CUSTOMFUNCTION_SHOW ProgressShow
!insertmacro MUI_PAGE_INSTFILES
!define MUI_FINISHPAGE_RUN "$INSTDIR\Application\chrome.exe"
!define MUI_FINISHPAGE_RUN_TEXT "$(LaunchDao)"
!define MUI_PAGE_CUSTOMFUNCTION_SHOW FinishShow
!define MUI_PAGE_CUSTOMFUNCTION_DESTROYED ReleasePageIcon
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_LANGUAGE "English"
!insertmacro MUI_LANGUAGE "SimpChinese"
SetFont /LANG=${LANG_ENGLISH} "Segoe UI" 9
SetFont /LANG=${LANG_SIMPCHINESE} "Microsoft YaHei UI" 9
!include "locales\en.nsh"
!include "locales\zh-CN.nsh"
!include "theme.nsh"

Function TryWebView
!ifdef WEBVIEW_HOST
  IfSilent native_ui
  ${GetParameters} $0
  ClearErrors
  ${GetOptions} $0 "/NATIVE" $1
  IfErrors 0 native_ui
  InitPluginsDir
  File "/oname=$PLUGINSDIR\dao-installer-ui.exe" "${WEBVIEW_HOST}"
  File "/oname=$PLUGINSDIR\index.html" "${WEBVIEW_HTML}"
  File "/oname=$PLUGINSDIR\WebView2-LICENSE.txt" "${WEBVIEW_LICENSE}"
  ; A file avoids interpreting user-supplied directory text as host arguments.
  ; Win32 INI writes require a BOM to preserve paths outside the system code page.
  FileOpen $0 "$PLUGINSDIR\session.ini" w
  FileWriteWord $0 0xFEFF
  FileClose $0
  WriteINIStr "$PLUGINSDIR\session.ini" "Installer" "Executable" "$EXEPATH"
  WriteINIStr "$PLUGINSDIR\session.ini" "Installer" "Directory" "$INSTDIR"
  WriteINIStr "$PLUGINSDIR\session.ini" "Installer" "Language" "$LANGUAGE"
  WriteINIStr "$PLUGINSDIR\session.ini" "Installer" "Version" "${VERSION}"
  StrCpy $1 0
  ${If} $ExistingRoot != ""
    StrCpy $1 1
  ${EndIf}
  WriteINIStr "$PLUGINSDIR\session.ini" "Installer" "Locked" "$1"
  ClearErrors
  ExecWait '"$PLUGINSDIR\dao-installer-ui.exe"' $0
  IfErrors native_ui
  ; Only pre-installation WebView failure may fall back to the native wizard.
  IntCmp $0 77 native_ui
  SetErrorLevel $0
  Quit
native_ui:
!endif
FunctionEnd

Function .onInit
  ${IfNot} ${RunningX64}
    MessageBox MB_OK|MB_ICONSTOP "$(RequiresX64)" /SD IDOK
    SetErrorLevel 1
    Quit
  ${EndIf}
  ; Chromium writes installer registration into the 32-bit registry view.
  SetRegView 32
  SetShellVarContext current
  ReadRegStr $0 HKLM "${UNINSTALL_KEY}" "InstallLocation"
  ${If} $0 != ""
    MessageBox MB_OK|MB_ICONSTOP "$(MachineInstallExists)" /SD IDOK
    SetErrorLevel 1
    Quit
  ${EndIf}
  ReadRegStr $0 HKCU "${UNINSTALL_KEY}" "InstallLocation"
  ${If} $0 != ""
    ${GetFileName} "$0" $1
    ${If} $1 != "Application"
      MessageBox MB_OK|MB_ICONSTOP "$(InvalidRegistration)" /SD IDOK
      SetErrorLevel 1
      Quit
    ${EndIf}
    ${GetParent} "$0" $ExistingRoot
    StrCpy $INSTDIR $ExistingRoot
  ${EndIf}
  Call TryWebView
FunctionEnd

; Validate again in the section so silent installation follows the same rules.
Function ValidateDirectory
  StrCpy $PathError "$(InvalidDirectory)"
  StrLen $0 $INSTDIR
  ${If} $0 < 4
  ${OrIf} $0 > 180
    Return
  ${EndIf}
  StrCpy $0 $INSTDIR 2 1
  ${If} $0 != ":\"
    Return
  ${EndIf}
  ; NSIS GetFullPathName requires an existing path. Win32 also handles new folders.
  System::Call 'kernel32::GetFullPathNameW(w "$INSTDIR", i ${NSIS_MAX_STRLEN}, w .r0, p 0) i .r1'
  ${If} $1 == 0
  ${OrIf} $1 >= ${NSIS_MAX_STRLEN}
  ${OrIf} $0 != $INSTDIR
    Return
  ${EndIf}
  ${GetRoot} "$INSTDIR" $0
  System::Call 'kernel32::GetDriveTypeW(w "$0\") i .r1'
  ${If} $1 != 3
    Return
  ${EndIf}
  ; Reject protected trees and the browser profile before extracting the payload.
  !macro RejectTree ROOT
    StrLen $0 "${ROOT}"
    StrCpy $1 "$INSTDIR\" $0
    ${If} $1 == "${ROOT}"
      Return
    ${EndIf}
  !macroend
  !insertmacro RejectTree "$PROGRAMFILES32\"
  !insertmacro RejectTree "$PROGRAMFILES64\"
  !insertmacro RejectTree "$WINDIR\"
  !insertmacro RejectTree "$LOCALAPPDATA\Dao\User Data\"
  ${If} $ExistingRoot != ""
    ${If} $INSTDIR != $ExistingRoot
      Return
    ${EndIf}
  ${Else}
    ${If} ${FileExists} "$INSTDIR\Application\*.*"
    ${OrIf} ${FileExists} "$INSTDIR\Temp\*.*"
      StrCpy $PathError "$(DirectoryOccupied)"
      Return
    ${EndIf}
  ${EndIf}
  StrCpy $PathError ""
FunctionEnd

Function WelcomeLeave
  ${NSD_GetText} $DirectoryInput $INSTDIR
  Call ValidateDirectory
  ${If} $PathError != ""
    MessageBox MB_OK|MB_ICONEXCLAMATION "$PathError"
    Abort
  ${EndIf}
FunctionEnd

Section
  Call ValidateDirectory
  ${If} $PathError != ""
    SetErrorLevel 10
    MessageBox MB_OK|MB_ICONSTOP "$PathError" /SD IDOK
    Abort
  ${EndIf}
  ; Check write access without leaving files or removing user directories.
  ClearErrors
  CreateDirectory "$INSTDIR"
  GetTempFileName $0 "$INSTDIR"
  ${If} ${Errors}
    SetErrorLevel 10
    MessageBox MB_OK|MB_ICONSTOP "$(DirectoryNotWritable)" /SD IDOK
    Abort
  ${EndIf}
  Delete "$0"
  InitPluginsDir
  SetOutPath "$PLUGINSDIR"
  SetDetailsPrint textonly
  DetailPrint "$(Installing)"
  SetDetailsPrint listonly
  File /oname=mini_installer.exe "${PAYLOAD}"
  ClearErrors
  ExecWait '"$PLUGINSDIR\mini_installer.exe" --do-not-launch-chrome --verbose-logging --dao-install-dir="$INSTDIR"' $ExitCode
  ${If} ${Errors}
    StrCpy $ExitCode 10
  ${EndIf}
  ${If} $ExitCode != 0
    SetErrorLevel $ExitCode
    MessageBox MB_OK|MB_ICONSTOP "$(InstallFailed)" /SD IDOK
    Abort
  ${EndIf}
  ReadRegStr $0 HKCU "${UNINSTALL_KEY}" "InstallLocation"
  ${If} $0 != "$INSTDIR\Application"
    SetErrorLevel 10
    MessageBox MB_OK|MB_ICONSTOP "$(UnexpectedDirectory)" /SD IDOK
    Abort
  ${EndIf}
  ${IfNot} ${FileExists} "$INSTDIR\Application\chrome.exe"
    SetErrorLevel 10
    MessageBox MB_OK|MB_ICONSTOP "$(UnexpectedDirectory)" /SD IDOK
    Abort
  ${EndIf}
  SetErrorLevel 0
SectionEnd
