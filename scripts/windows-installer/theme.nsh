; Keep native controls and MUI page lifecycle; only the presentation is custom.
Var TitleFont
Var BodyFont
Var LabelFont
Var ControlFont
Var DirectoryInput
Var BrowseButton
Var PageIcon

!macro ThemeColors CONTROL TEXT BACKGROUND
  ${If} ${IsHighContrastModeActive}
    SetCtlColors ${CONTROL} SYSCLR:8 SYSCLR:5
  ${Else}
    SetCtlColors ${CONTROL} "${TEXT}" "${BACKGROUND}"
  ${EndIf}
!macroend

Function InitializeTheme
  InitPluginsDir
  File "/oname=$PLUGINSDIR\dao.ico" "${ICON}"
  CreateFont $TitleFont "$(ThemeFont)" 20 600
  CreateFont $BodyFont "$(ThemeFont)" 10
  CreateFont $LabelFont "$(ThemeFont)" 9 600
  CreateFont $ControlFont "$(ThemeFont)" 9
  SendMessage $mui.Button.Next ${WM_SETFONT} $LabelFont 0
  SendMessage $mui.Button.Cancel ${WM_SETFONT} $ControlFont 0
  !insertmacro ThemeColors $HWNDPARENT "253549" "FFFFFF"
  ShowWindow $mui.Line.Standard ${SW_HIDE}
  ShowWindow $mui.Line.FullWindow ${SW_HIDE}
  Call SizeActionButtons
FunctionEnd

Function SizeActionButtons
  ; Convert dialog units so action labels remain readable at different DPI.
  System::Call '*(i0, i0, i90, i18) p.r0'
  System::Call 'user32::MapDialogRect(p$HWNDPARENT, p r0)'
  System::Call '*$0(i, i, i.r5, i.r6)'
  System::Call 'user32::GetWindowRect(p$mui.Button.Next, p r0)'
  System::Call 'user32::MapWindowPoints(p0, p$HWNDPARENT, p r0, i2)'
  System::Call '*$0(i.r1, i.r2, i.r3, i.r4)'
  IntOp $1 $3 - $5
  IntOp $4 $4 - $2
  IntOp $4 $6 - $4
  IntOp $4 $4 / 2
  IntOp $2 $2 - $4
  System::Call 'user32::MoveWindow(p$mui.Button.Next, i r1, i r2, i r5, i r6, i1)'
  System::Free $0
FunctionEnd

!macro Label X Y WIDTH HEIGHT TEXT COLOR BACKGROUND FONT
  ${NSD_CreateLabel} ${X} ${Y} ${WIDTH} ${HEIGHT} "${TEXT}"
  Pop $0
  !insertmacro ThemeColors $0 "${COLOR}" "${BACKGROUND}"
  SendMessage $0 ${WM_SETFONT} ${FONT} 0
!macroend

!macro Hero TITLE SUBTITLE
  !insertmacro Label 0 0 100% 87u "" "253549" "E7EEF5" $BodyFont
  ${NSD_CreateIcon} 22u 19u 48u 48u ""
  Pop $0
  System::Call 'user32::SetWindowLongW(p$0, i-12, i1029)'
  ; Horizontal and vertical dialog units differ. Keep the icon square in pixels.
  System::Call '*(i0, i0, i0, i0) p.r1'
  System::Call 'user32::GetClientRect(p$0, p r1)'
  System::Call '*$1(i, i, i.r2, i)'
  System::Free $1
  System::Call 'user32::SetWindowPos(p$0, p0, i0, i0, i r2, i r2, i0x12)'
  LoadAndSetImage /STRINGID /RESIZETOFIT $0 ${IMAGE_ICON} ${LR_LOADFROMFILE} "$PLUGINSDIR\dao.ico" $PageIcon
  !insertmacro Label 85u 20u 220u 28u "${TITLE}" "253549" "E7EEF5" $TitleFont
  !insertmacro Label 86u 54u 220u 20u "${SUBTITLE}" "556579" "E7EEF5" $BodyFont
!macroend

Function WelcomeShow
  !insertmacro ThemeColors $mui.WelcomePage "253549" "FFFFFF"
  ShowWindow $mui.Button.Back ${SW_HIDE}
  ShowWindow $mui.WelcomePage.Image ${SW_HIDE}
  ShowWindow $mui.WelcomePage.Title ${SW_HIDE}
  ShowWindow $mui.WelcomePage.Text ${SW_HIDE}
  !insertmacro Hero "$(WelcomeTitle)" "$(WelcomeSubtitle)"
  !insertmacro Label 22u 103u 280u 15u "$(DirectoryLabel)" "253549" "FFFFFF" $LabelFont
  ${NSD_CreateDirRequest} 22u 125u 218u 20u "$INSTDIR"
  Pop $DirectoryInput
  ; Keep a stable native control ID for accessibility and UI verification.
  System::Call 'user32::SetWindowLongW(p$DirectoryInput, i-12, i1019)'
  !insertmacro ThemeColors $DirectoryInput "253549" "F5F8FC"
  SendMessage $DirectoryInput ${WM_SETFONT} $ControlFont 0
  ${NSD_CreateBrowseButton} 247u 125u 58u 20u "$(BrowseDirectory)"
  Pop $BrowseButton
  SendMessage $BrowseButton ${WM_SETFONT} $ControlFont 0
  ${NSD_OnClick} $BrowseButton BrowseDirectory
  ${If} $ExistingRoot == ""
    !insertmacro Label 22u 154u 283u 32u "$(DirectoryHelp)" "556579" "FFFFFF" $ControlFont
    SendMessage $mui.Button.Next ${WM_SETTEXT} 0 "STR:$(InstallButton)"
  ${Else}
    EnableWindow $DirectoryInput 0
    EnableWindow $BrowseButton 0
    !insertmacro Label 22u 154u 190u 32u "$(UpgradeHelp)" "556579" "FFFFFF" $ControlFont
    ${NSD_CreateButton} 219u 157u 86u 20u "$(UninstallButton)"
    Pop $0
    System::Call 'user32::SetWindowLongW(p$0, i-12, i1032)'
    SendMessage $0 ${WM_SETFONT} $ControlFont 0
    ${NSD_OnClick} $0 UninstallClick
    SendMessage $mui.Button.Next ${WM_SETTEXT} 0 "STR:$(RepairButton)"
  ${EndIf}
FunctionEnd

Function UninstallClick
  Pop $0
  Call LaunchUninstaller
FunctionEnd

Function BrowseDirectory
  Pop $0
  ${NSD_GetText} $DirectoryInput $0
  nsDialogs::SelectFolderDialog "$(DirectoryLabel)" "$0"
  Pop $0
  ${If} $0 != error
    ${NSD_SetText} $DirectoryInput "$0"
  ${EndIf}
FunctionEnd

Function ProgressShow
  ShowWindow $mui.Button.Back ${SW_HIDE}
  !insertmacro ThemeColors $mui.Header.Background "253549" "E7EEF5"
  !insertmacro ThemeColors $mui.Header.Text "253549" "E7EEF5"
  !insertmacro ThemeColors $mui.Header.SubText "556579" "E7EEF5"
  !insertmacro ThemeColors $mui.InstFilesPage "253549" "FFFFFF"
  !insertmacro ThemeColors $mui.InstFilesPage.Text "556579" "FFFFFF"
  SendMessage $mui.Header.Text ${WM_SETFONT} $BodyFont 0
  ; A native smooth blue progress bar keeps standard accessibility semantics.
  ${IfNot} ${IsHighContrastModeActive}
    System::Call 'uxtheme::SetWindowTheme(p$mui.InstFilesPage.ProgressBar, w"", w"")'
    SendMessage $mui.InstFilesPage.ProgressBar ${PBM_SETBARCOLOR} 0 0xBE7846
    SendMessage $mui.InstFilesPage.ProgressBar ${PBM_SETBKCOLOR} 0 0xF5EEE7
  ${EndIf}
FunctionEnd

Function FinishShow
  !insertmacro ThemeColors $mui.FinishPage "253549" "FFFFFF"
  ShowWindow $mui.Button.Back ${SW_HIDE}
  ShowWindow $mui.FinishPage.Image ${SW_HIDE}
  ShowWindow $mui.FinishPage.Title ${SW_HIDE}
  ShowWindow $mui.FinishPage.Text ${SW_HIDE}
  ShowWindow $mui.FinishPage.Run ${SW_HIDE}
  !insertmacro Hero "$(FinishTitle)" "$(FinishSubtitle)"
  !insertmacro Label 22u 107u 283u 30u "$(FinishText)" "253549" "FFFFFF" $BodyFont
  System::Call 'user32::SetWindowLongW(p$0, i-12, i1031)'
  ${NSD_CreateCheckbox} 22u 151u 283u 18u "$(LaunchDao)"
  Pop $mui.FinishPage.Run
  System::Call 'user32::SetWindowLongW(p$mui.FinishPage.Run, i-12, i1030)'
  SendMessage $mui.FinishPage.Run ${WM_SETFONT} $ControlFont 0
  ; Match MUI's high-contrast handling for themed checkbox text.
  ${If} ${IsHighContrastModeActive}
    System::Call 'uxtheme::SetWindowTheme(p$mui.FinishPage.Run, w" ", w" ")'
  ${EndIf}
  !insertmacro ThemeColors $mui.FinishPage.Run "253549" "FFFFFF"
  ${NSD_Check} $mui.FinishPage.Run
  ${NSD_SetFocus} $mui.FinishPage.Run
FunctionEnd

Function ReleasePageIcon
  ${NSD_FreeIcon} $PageIcon
  StrCpy $PageIcon 0
FunctionEnd

Function .onGUIEnd
  System::Call 'gdi32::DeleteObject(p$TitleFont)'
  System::Call 'gdi32::DeleteObject(p$BodyFont)'
  System::Call 'gdi32::DeleteObject(p$LabelFont)'
  System::Call 'gdi32::DeleteObject(p$ControlFont)'
FunctionEnd
