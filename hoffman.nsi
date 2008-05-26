; hoffman.nsi
;
; This script is based on example2.nsi
;
; It will install hoffman (and its cygwin DLLs) into a directory that the user selects,
; and create a start menu command prompt shortcut

;--------------------------------

; The name of the installer
Name "Hoffman"

; The file to write
OutFile "setup.exe"

; The default installation directory
InstallDir $PROGRAMFILES\Hoffman

; Registry key to check for directory (so if you install again, it will 
; overwrite the old one automatically)
;InstallDirRegKey SHCTX "Software\freesoft.org\Hoffman" "Install_Dir"
InstallDirRegKey HKLM "Software\freesoft.org\Hoffman" "Install_Dir"

;--------------------------------

; Pages

Page directory
Page instfiles

; Uninstaller doesn't reside in the install directory, so use a pre-function
; to fetch install directory from registry first thing in the uninstaller
;
; Check first if it was installed for the current user, then the local machine

Function un.setINSTDIR
  SetShellVarContext current
  ReadRegStr $INSTDIR HKCU SOFTWARE\freesoft.org\Hoffman "Install_Dir"
  IfErrors 0 +3
    ReadRegStr $INSTDIR HKLM SOFTWARE\freesoft.org\Hoffman "Install_Dir"
    SetShellVarContext all
FunctionEnd

UninstPage uninstConfirm un.setINSTDIR
UninstPage instfiles

;--------------------------------

; The stuff to install
Section "Hoffman"

  SectionIn RO
  
  SetShellVarContext all

  ; Set output path to the installation directory.
  SetOutPath $INSTDIR
  File "genalltb.bat"
  
  ; Put executable and all of its cygwin DLLs into subdir bin
  SetOutPath $INSTDIR\bin
  File "hoffman.exe"
  File "\cygwin\bin\cygwin1.dll"
  File "\cygwin\bin\cygcrypto-0.9.8.dll"
  File "\cygwin\bin\cygcurl-4.dll"
  File "\cygwin\bin\cygiconv-2.dll"
  File "\cygwin\bin\cygncurses-8.dll"
  File "\cygwin\bin\cygreadline6.dll"
  File "\cygwin\bin\cygssl-0.9.8.dll"
  File "\cygwin\bin\cygxml2-2.dll"
  File "\cygwin\bin\cygz.dll"

  ; and the demo XML files into subdir Examples
  SetOutPath $INSTDIR\xml
  File "xml\*.xml"

  SetOutPath $INSTDIR
  File "reference.pdf"
  File "tutorial.pdf"
  
  ; Write the installation path into the registry
  WriteRegStr SHCTX SOFTWARE\freesoft.org\Hoffman "Install_Dir" "$INSTDIR"
  
  ; Write the uninstall keys for Windows
  WriteRegStr SHCTX "Software\Microsoft\Windows\CurrentVersion\Uninstall\Hoffman" "DisplayName" "Hoffman"
  WriteRegStr SHCTX "Software\Microsoft\Windows\CurrentVersion\Uninstall\Hoffman" "UninstallString" '"$INSTDIR\bin\uninstall.exe"'
  WriteRegDWORD SHCTX "Software\Microsoft\Windows\CurrentVersion\Uninstall\Hoffman" "NoModify" 1
  WriteRegDWORD SHCTX "Software\Microsoft\Windows\CurrentVersion\Uninstall\Hoffman" "NoRepair" 1

  WriteUninstaller "bin\uninstall.exe"
  
  ; the Start Menu shortcuts
  CreateDirectory "$SMPROGRAMS\Hoffman"
  CreateShortCut "$SMPROGRAMS\Hoffman\Uninstall.lnk" "$INSTDIR\bin\uninstall.exe" "" "$INSTDIR\bin\uninstall.exe" 0
  CreateShortCut "$SMPROGRAMS\Hoffman\Hoffman Tutorial.lnk" "$INSTDIR\tutorial.pdf"
  CreateShortCut "$SMPROGRAMS\Hoffman\Hoffman Reference Guide.lnk" "$INSTDIR\reference.pdf"
  CreateShortCut "$SMPROGRAMS\Hoffman\Hoffman.lnk" "cmd.exe" "/k set path=%path%;$INSTDIR\bin"

SectionEnd

;--------------------------------

; Uninstaller

Section "Uninstall"
  
  ; Remove registry keys
  DeleteRegKey SHCTX "Software\Microsoft\Windows\CurrentVersion\Uninstall\Hoffman"
  DeleteRegKey SHCTX SOFTWARE\freesoft.org\Hoffman

  ; Remove files and uninstaller, including any tablebases in Examples and/or history files
  Delete "$INSTDIR\bin\hoffman.exe"
  Delete "$INSTDIR\bin\*.dll"
  Delete "$INSTDIR\xml\*.xml"
  Delete "$INSTDIR\genalltb.bat"
  Delete "$INSTDIR\reference.pdf"
  Delete "$INSTDIR\tutorial.pdf"
  Delete "$INSTDIR\xml\.hoffman_history"
  Delete "$INSTDIR\.hoffman_history"
  Delete "$INSTDIR\bin\uninstall.exe"

  ; Remove shortcuts
  Delete "$SMPROGRAMS\Hoffman\*.*"

  ; Remove directories used
  RMDir "$SMPROGRAMS\Hoffman"
  RMDir "$INSTDIR\xml"
  RMDir "$INSTDIR\bin"
  RMDir "$INSTDIR"

SectionEnd
