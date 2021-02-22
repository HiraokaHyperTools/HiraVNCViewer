; example1.nsi
;
; This script is perhaps one of the simplest NSIs you can make. All of the
; optional settings are left to their default settings. The installer simply 
; prompts the user asking them where to install, and drops a copy of example1.nsi
; there. 

;--------------------------------

Unicode true

!define APP "HiraVNCViewer"

!finalize 'MySign "%1"'
!system 'MySign "Release_Unicode\${APP}.exe"'

!system 'DefineAsmVer.exe "Release_Unicode\${APP}.exe" "!define VER ""[SFVER]"" " > Appver.tmp'
!include "Appver.tmp"
!searchreplace V_E_R ${VER} "." "_"

; The name of the installer
Name "${APP} ${VER} portable"

; The file to write
OutFile "Setup_${APP}_${V_E_R}.exe"

; The default installation directory
InstallDir "$APPDATA\${APP}"

; Request application privileges for Windows Vista
RequestExecutionLevel user

XPStyle on

;--------------------------------

; Pages

Page license
Page directory
Page components
Page instfiles

LicenseData "GNUGPL2.rtf"

;--------------------------------

; The stuff to install
Section "" ;No components page, name is not important

  ; Set output path to the installation directory.
  SetOutPath $INSTDIR
  
  ; Put file there
  File "Release_Unicode\hiravncviewer.exe"

SectionEnd ; end the section

Section "実行"
  Exec "$INSTDIR\hiravncviewer.exe"
SectionEnd

Section "デスクトップ アイコン 作成"
  CreateShortCut "$DESKTOP\${APP}.lnk" "$INSTDIR\${APP}.exe"
SectionEnd

Section ""
  IfErrors +2
    SetAutoClose true
SectionEnd
