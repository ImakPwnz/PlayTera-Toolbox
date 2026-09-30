Unicode true
!include "MUI2.nsh"
!include "FileFunc.nsh"
!include "LogicLib.nsh"
!ifndef PAYLOAD
  !error "Specify the verified PAYLOAD directory"
!endif
!ifndef OUTPUT_FILE
  !error "Specify OUTPUT_FILE"
!endif
Name "PlayTera Toolbox"
OutFile "${OUTPUT_FILE}"
InstallDir "$LOCALAPPDATA\PlayTera Toolbox"
RequestExecutionLevel user
SetCompressor /SOLID lzma
CRCCheck force
AutoCloseWindow true
ShowInstDetails show
VIProductVersion "1.0.0.0"
VIAddVersionKey "ProductName" "PlayTera Toolbox"
VIAddVersionKey "FileDescription" "PlayTera Toolbox Setup"
VIAddVersionKey "FileVersion" "1.0.0"
VIAddVersionKey "LegalCopyright" "PlayTera modifications; original Toolbox authors credited in PLAYTERA-NOTICE.txt"
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_LANGUAGE "German"
!insertmacro MUI_LANGUAGE "English"

Function VerifyTarget
  System::Call 'kernel32::GetFileAttributesW(w "$INSTDIR") i.r0'
  ${If} $0 != -1
    IntOp $1 $0 & 0x410
    # LogicLib != compares strings, so use decimal GetFileAttributes output.
    ${If} $1 != 16
      SetErrorLevel 10
      Abort "Keine Dateien oder Verzeichnislinks als Installationsziel verwenden."
    ${EndIf}
    StrCpy $1 ""
    FindFirst $0 $1 "$INSTDIR\*"
    loop:
      ${If} $1 != ""
        ${If} $1 != "."
        ${AndIf} $1 != ".."
          FindClose $0
          SetErrorLevel 11
          Abort "Bitte ein leeres Verzeichnis waehlen. Bestehende Toolbox-/Spieldateien werden nicht ersetzt."
        ${EndIf}
        FindNext $0 $1
        Goto loop
      ${EndIf}
    FindClose $0
  ${EndIf}
  StrCpy $2 "$INSTDIR"
  parents:
    ${GetParent} "$2" $3
    ${If} $3 == ""
      Goto done
    ${EndIf}
    ${If} $3 == $2
      Goto done
    ${EndIf}
    StrCpy $2 $3
    System::Call 'kernel32::GetFileAttributesW(w r2) i.r0'
    ${If} $0 != -1
      IntOp $1 $0 & 0x400
      ${If} $1 != 0
        SetErrorLevel 10
        Abort "Ein uebergeordnetes Verzeichnis ist ein Link."
      ${EndIf}
    ${EndIf}
    Goto parents
  done:
FunctionEnd

Function .onVerifyInstDir
  Call VerifyTarget
FunctionEnd

Section "PlayTera Toolbox"
  Call VerifyTarget
  SetOutPath "$INSTDIR"
  SetOverwrite off
  ClearErrors
  File /r "${PAYLOAD}\*"
  ${If} ${Errors}
    SetErrorLevel 12
    Abort "Installation unvollstaendig. Schreibrechte und freie Kapazitaet pruefen. Keine fremden Dateien wurden ersetzt."
  ${EndIf}
  SetErrorLevel 0
SectionEnd
