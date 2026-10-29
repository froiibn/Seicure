; Seicure installer (NSIS 3.x, Unicode)
;
; Per-machine install by design. A per-user install under %LOCALAPPDATA% is
; writable by anything running as that user, so malware could swap the binary
; that receives the password. Program Files is ACL-protected, and the one-time
; UAC prompt buys that.
;
; Build:  makensis /DSTAGE=..\build\stage installer\seicure.nsi

Unicode true

!include "MUI2.nsh"
!include "LogicLib.nsh"
!include "x64.nsh"
!include "FileFunc.nsh"

!define APP_NAME     "Seicure"
!define APP_VERSION  "0.1.0"
!define APP_PUBLISHER "Seicure"
!define APP_EXE      "seicure_gui.exe"
!define CLI_EXE      "seicure.exe"
!define PROGID       "Seicure.Container"
!define EXT          ".sef"
!define UNINST_KEY   "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_NAME}"

; Where the built binaries are staged. Override with /DSTAGE=<path>.
!ifndef STAGE
  !define STAGE "..\build\stage"
!endif

; /DTESTMODE builds an unelevated copy that writes to HKCU instead of HKLM, so
; the whole install/uninstall round trip can be exercised without touching a
; real machine. Everything else -- file layout, registry shape, shortcut and
; cleanup logic -- is identical, so a passing test-mode run means the shipping
; installer differs only in which hive it writes to.
!ifdef TESTMODE
  !define ROOT "HKCU"
  Name "${APP_NAME} ${APP_VERSION} (test)"
  OutFile "..\build\Seicure-test-setup.exe"
  RequestExecutionLevel user
!else
  !define ROOT "HKLM"
  Name "${APP_NAME} ${APP_VERSION}"
  OutFile "..\build\Seicure-${APP_VERSION}-x64-setup.exe"
  RequestExecutionLevel admin
!endif

InstallDir "$PROGRAMFILES64\${APP_NAME}"
InstallDirRegKey ${ROOT} "Software\${APP_NAME}" "InstallDir"
SetCompressor /SOLID lzma

VIProductVersion "0.1.0.0"
VIAddVersionKey "ProductName"     "${APP_NAME}"
VIAddVersionKey "FileDescription" "${APP_NAME} installer"
VIAddVersionKey "FileVersion"     "${APP_VERSION}"
VIAddVersionKey "ProductVersion"  "${APP_VERSION}"
VIAddVersionKey "LegalCopyright"  "${APP_PUBLISHER}"

!define MUI_ICON   "..\assets\seicure.ico"
!define MUI_UNICON "..\assets\seicure.ico"
!define MUI_ABORTWARNING
!define MUI_FINISHPAGE_RUN "$INSTDIR\${APP_EXE}"

!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

; Korean first so it wins on a Korean system; English is the fallback.
!insertmacro MUI_LANGUAGE "Korean"
!insertmacro MUI_LANGUAGE "English"

LangString SecCoreName      ${LANG_KOREAN}  "프로그램 파일 (필수)"
LangString SecCoreName      ${LANG_ENGLISH} "Program files (required)"
LangString SecAssocName     ${LANG_KOREAN}  "${EXT} 파일 연결 및 우클릭 메뉴"
LangString SecAssocName     ${LANG_ENGLISH} "${EXT} file association and context menu"
LangString SecDesktopName   ${LANG_KOREAN}  "바탕 화면 바로 가기"
LangString SecDesktopName   ${LANG_ENGLISH} "Desktop shortcut"

LangString SecCoreDesc      ${LANG_KOREAN}  "실행 파일과 시작 메뉴 바로 가기를 설치합니다."
LangString SecCoreDesc      ${LANG_ENGLISH} "Installs the executables and a Start menu shortcut."
LangString SecAssocDesc     ${LANG_KOREAN}  "${EXT} 파일에 아이콘을 붙이고, 파일을 우클릭해 바로 암호화·복호화할 수 있게 합니다."
LangString SecAssocDesc     ${LANG_ENGLISH} "Gives ${EXT} files an icon and adds encrypt/decrypt entries to the right-click menu."
LangString SecDesktopDesc   ${LANG_KOREAN}  "바탕 화면에 바로 가기를 만듭니다."
LangString SecDesktopDesc   ${LANG_ENGLISH} "Creates a shortcut on the desktop."

LangString VerbEncrypt      ${LANG_KOREAN}  "Seicure로 암호화"
LangString VerbEncrypt      ${LANG_ENGLISH} "Encrypt with Seicure"
LangString VerbDecrypt      ${LANG_KOREAN}  "Seicure로 복호화"
LangString VerbDecrypt      ${LANG_ENGLISH} "Decrypt with Seicure"
LangString TypeName         ${LANG_KOREAN}  "Seicure 암호화 컨테이너"
LangString TypeName         ${LANG_ENGLISH} "Seicure encrypted container"

LangString MsgNeed64        ${LANG_KOREAN}  "${APP_NAME}는 64비트 Windows에서만 동작합니다."
LangString MsgNeed64        ${LANG_ENGLISH} "${APP_NAME} requires 64-bit Windows."
LangString MsgOldVersion    ${LANG_KOREAN}  "이전 버전이 설치되어 있습니다.$\n$\n제거한 뒤 계속하시겠습니까?"
LangString MsgOldVersion    ${LANG_ENGLISH} "An earlier version is already installed.$\n$\nRemove it and continue?"
LangString MsgKeepFiles     ${LANG_KOREAN}  "${APP_NAME}를 제거합니다.$\n$\n이미 암호화해 둔 ${EXT} 파일은 삭제되지 않습니다."
LangString MsgKeepFiles     ${LANG_ENGLISH} "${APP_NAME} will be removed.$\n$\nYour existing ${EXT} files are left untouched."

; ---------------------------------------------------------------------------

Function .onInit
    ${IfNot} ${RunningX64}
        MessageBox MB_ICONSTOP "$(MsgNeed64)"
        Abort
    ${EndIf}
    SetRegView 64

    ; An in-place overwrite would orphan the old uninstaller's registry entries,
    ; so take the previous version out first.
    ReadRegStr $R0 ${ROOT} "${UNINST_KEY}" "UninstallString"
    ${If} $R0 != ""
        MessageBox MB_OKCANCEL|MB_ICONEXCLAMATION "$(MsgOldVersion)" /SD IDOK IDOK remove
        Abort
        remove:
        ReadRegStr $R1 ${ROOT} "${UNINST_KEY}" "InstallLocation"
        ExecWait '"$R0" /S _?=$R1'
        Delete "$R1\uninstall.exe"
        RMDir "$R1"
    ${EndIf}
FunctionEnd

Section "$(SecCoreName)" SecCore
    SectionIn RO
    SetRegView 64
    SetOutPath "$INSTDIR"

    File "${STAGE}\${APP_EXE}"
    File "${STAGE}\${CLI_EXE}"
    File "/oname=seicure.ico" "..\assets\seicure.ico"

    WriteRegStr ${ROOT} "Software\${APP_NAME}" "InstallDir" "$INSTDIR"
    WriteRegStr ${ROOT} "Software\${APP_NAME}" "Version"    "${APP_VERSION}"

    CreateDirectory "$SMPROGRAMS\${APP_NAME}"
    CreateShortcut  "$SMPROGRAMS\${APP_NAME}\${APP_NAME}.lnk" "$INSTDIR\${APP_EXE}"

    WriteUninstaller "$INSTDIR\uninstall.exe"

    ; Add/Remove Programs entry.
    ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
    WriteRegStr   ${ROOT} "${UNINST_KEY}" "DisplayName"     "${APP_NAME}"
    WriteRegStr   ${ROOT} "${UNINST_KEY}" "DisplayVersion"  "${APP_VERSION}"
    WriteRegStr   ${ROOT} "${UNINST_KEY}" "Publisher"       "${APP_PUBLISHER}"
    WriteRegStr   ${ROOT} "${UNINST_KEY}" "DisplayIcon"     "$INSTDIR\${APP_EXE},0"
    WriteRegStr   ${ROOT} "${UNINST_KEY}" "InstallLocation" "$INSTDIR"
    WriteRegStr   ${ROOT} "${UNINST_KEY}" "UninstallString" "$\"$INSTDIR\uninstall.exe$\""
    WriteRegStr   ${ROOT} "${UNINST_KEY}" "QuietUninstallString" "$\"$INSTDIR\uninstall.exe$\" /S"
    WriteRegDWORD ${ROOT} "${UNINST_KEY}" "EstimatedSize"   "$0"
    WriteRegDWORD ${ROOT} "${UNINST_KEY}" "NoModify"        1
    WriteRegDWORD ${ROOT} "${UNINST_KEY}" "NoRepair"        1
SectionEnd

Section "$(SecAssocName)" SecAssoc
    SetRegView 64

    ; File type. "open" doubles as the decrypt verb so a double-click and the
    ; right-click entry lead to the same place.
    WriteRegStr ${ROOT} "Software\Classes\${EXT}" "" "${PROGID}"
    WriteRegStr ${ROOT} "Software\Classes\${PROGID}" "" "$(TypeName)"
    WriteRegStr ${ROOT} "Software\Classes\${PROGID}\DefaultIcon" "" "$INSTDIR\${APP_EXE},0"
    WriteRegStr ${ROOT} "Software\Classes\${PROGID}\shell" "" "open"
    WriteRegStr ${ROOT} "Software\Classes\${PROGID}\shell\open" "MUIVerb" "$(VerbDecrypt)"
    WriteRegStr ${ROOT} "Software\Classes\${PROGID}\shell\open" "Icon" "$INSTDIR\${APP_EXE},0"
    WriteRegStr ${ROOT} "Software\Classes\${PROGID}\shell\open\command" "" '"$INSTDIR\${APP_EXE}" --decrypt "%1"'

    ; Encrypt verb on every file type.
    WriteRegStr ${ROOT} "Software\Classes\*\shell\SeicureEncrypt" "MUIVerb" "$(VerbEncrypt)"
    WriteRegStr ${ROOT} "Software\Classes\*\shell\SeicureEncrypt" "Icon" "$INSTDIR\${APP_EXE},0"
    WriteRegStr ${ROOT} "Software\Classes\*\shell\SeicureEncrypt\command" "" '"$INSTDIR\${APP_EXE}" --encrypt "%1"'

    ; Tell the shell to reread associations, otherwise the icon only appears
    ; after a reboot.
    System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, i 0, i 0)'
SectionEnd

Section /o "$(SecDesktopName)" SecDesktop
    CreateShortcut "$DESKTOP\${APP_NAME}.lnk" "$INSTDIR\${APP_EXE}"
SectionEnd

!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
    !insertmacro MUI_DESCRIPTION_TEXT ${SecCore}    "$(SecCoreDesc)"
    !insertmacro MUI_DESCRIPTION_TEXT ${SecAssoc}   "$(SecAssocDesc)"
    !insertmacro MUI_DESCRIPTION_TEXT ${SecDesktop} "$(SecDesktopDesc)"
!insertmacro MUI_FUNCTION_DESCRIPTION_END

; ---------------------------------------------------------------------------

Function un.onInit
    SetRegView 64
    ; /SD makes NSIS answer this automatically when running silently. Without it
    ; the upgrade path -- which runs the previous uninstaller with /S under
    ; ExecWait -- blocks on a modal dialog forever.
    MessageBox MB_OKCANCEL|MB_ICONINFORMATION "$(MsgKeepFiles)" /SD IDOK IDOK proceed
    Abort
    proceed:
FunctionEnd

Section "Uninstall"
    SetRegView 64

    Delete "$INSTDIR\${APP_EXE}"
    Delete "$INSTDIR\${CLI_EXE}"
    Delete "$INSTDIR\seicure.ico"
    Delete "$INSTDIR\uninstall.exe"
    RMDir  "$INSTDIR"

    Delete   "$SMPROGRAMS\${APP_NAME}\${APP_NAME}.lnk"
    RMDir    "$SMPROGRAMS\${APP_NAME}"
    Delete   "$DESKTOP\${APP_NAME}.lnk"

    ; Only drop the extension mapping if it still points at us: another program
    ; may have claimed .sef in the meantime.
    ReadRegStr $R0 ${ROOT} "Software\Classes\${EXT}" ""
    ${If} $R0 == "${PROGID}"
        DeleteRegKey ${ROOT} "Software\Classes\${EXT}"
    ${EndIf}
    DeleteRegKey ${ROOT} "Software\Classes\${PROGID}"
    DeleteRegKey ${ROOT} "Software\Classes\*\shell\SeicureEncrypt"

    DeleteRegKey ${ROOT} "Software\${APP_NAME}"
    DeleteRegKey ${ROOT} "${UNINST_KEY}"

    System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, i 0, i 0)'
SectionEnd
