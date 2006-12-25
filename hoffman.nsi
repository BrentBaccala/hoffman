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
InstallDirRegKey HKLM "Software\freesoft.org\Hoffman" "Install_Dir"

;--------------------------------

; Pages

Page directory
Page instfiles

; Uninstaller doesn't reside in the install directory, so use a pre-function
; to fetch install directory from registry first thing in the uninstaller

Function un.setINSTDIR
  ReadRegStr $INSTDIR HKLM SOFTWARE\freesoft.org\Hoffman "Install_Dir"
FunctionEnd

UninstPage uninstConfirm un.setINSTDIR
UninstPage instfiles

;--------------------------------

; The stuff to install
Section "Hoffman"

  SectionIn RO
  
  ; Set output path to the installation directory.
  SetOutPath $INSTDIR
  
  ; Put executable and all of its cygwin DLLs into subdir bin
  SetOutPath $INSTDIR\bin
  File "hoffman.exe"
  File "\cygwin\bin\cygwin1.dll"
  File "\cygwin\bin\cygcrypto-0.9.8.dll"
  File "\cygwin\bin\cygcurl-3.dll"
  File "\cygwin\bin\cygiconv-2.dll"
  File "\cygwin\bin\cygncurses-8.dll"
  File "\cygwin\bin\cygreadline6.dll"
  File "\cygwin\bin\cygssl-0.9.8.dll"
  File "\cygwin\bin\cygxml2-2.dll"
  File "\cygwin\bin\cygz.dll"

  ; and the demo XML files into subdir Examples
  SetOutPath $INSTDIR\Examples
  File "kk.xml"
  File "k?k.xml"
  File "k??k.xml"
  File "k?k?.xml"
  File "lasker1901.xml"
  File "fortress*.xml"
  File "genalltb.bat"

  SetOutPath $INSTDIR
  File "hoffman.pdf"
  
  ; Write the installation path into the registry
  WriteRegStr HKLM SOFTWARE\freesoft.org\Hoffman "Install_Dir" "$INSTDIR"
  
  ; Write the uninstall keys for Windows
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Hoffman" "DisplayName" "Hoffman"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Hoffman" "UninstallString" '"$INSTDIR\bin\uninstall.exe"'
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Hoffman" "NoModify" 1
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Hoffman" "NoRepair" 1

  WriteUninstaller "bin\uninstall.exe"
  
  ; the Start Menu shortcuts
  CreateDirectory "$SMPROGRAMS\Hoffman"
  CreateShortCut "$SMPROGRAMS\Hoffman\Uninstall.lnk" "$INSTDIR\bin\uninstall.exe" "" "$INSTDIR\bin\uninstall.exe" 0
  CreateShortCut "$SMPROGRAMS\Hoffman\Hoffman User's Guide.lnk" "$INSTDIR\hoffman.pdf"
  CreateShortCut "$SMPROGRAMS\Hoffman\Hoffman.lnk" "cmd.exe" "/k set path=%path%;$INSTDIR\bin"

SectionEnd

;--------------------------------

; Uninstaller

Section "Uninstall"
  
  ; Remove registry keys
  DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Hoffman"
  DeleteRegKey HKLM SOFTWARE\freesoft.org\Hoffman

  ; Remove files and uninstaller, including any tablebases in Examples and/or history files
  Delete "$INSTDIR\bin\hoffman.exe"
  Delete "$INSTDIR\bin\*.dll"
  Delete "$INSTDIR\Examples\*.xml"
  Delete "$INSTDIR\Examples\*.htb"
  Delete "$INSTDIR\Examples\genalltb.bat"
  Delete "$INSTDIR\Examples\.hoffman_history"
  Delete "$INSTDIR\hoffman.pdf"
  Delete "$INSTDIR\.hoffman_history"
  Delete "$INSTDIR\bin\uninstall.exe"

  ; Remove shortcuts
  Delete "$SMPROGRAMS\Hoffman\*.*"

  ; Remove directories used
  RMDir "$SMPROGRAMS\Hoffman"
  RMDir "$INSTDIR\Examples"
  RMDir "$INSTDIR\bin"
  RMDir "$INSTDIR"

SectionEnd
