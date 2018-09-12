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

; We're going to prompt the user (if he has enough permissions) to see
; if he wants to install system-wide.  If not, we'll install just for
; the current user, but that triggers some kind of backward
; compatibility mode on newer Microsoft systems (at least Vista and
; Server 2008) that causes shortcuts to be installed system-wide when
; we try to install for the current user, so we need this:
;
; http://nsis.sourceforge.net/Shortcuts_removal_fails_on_Windows_Vista

RequestExecutionLevel highest

;--------------------------------

; Pages

!include nsDialogs.nsh

Var CurrentUserOnlyInstallState

Page custom installTypePage installTypePageLeave
Page directory
Page instfiles

; create our initial install page, which tells a little about the
; program, queries to find out what kind of permissions the current
; user has got, and if he can install for anyone (Admin or Power),
; give him that option as a radio button, otherwise gray it out

Function installTypePage
	nsDialogs::Create /NOUNLOAD 1018
	Pop $0

	; ${NSD_CreateLabel} 0 0 100% 12u 'freesoft.org'
	nsDialogs::CreateControl /NOUNLOAD STATIC ${__NSD_Label_STYLE}|${ES_CENTER} ${__NSD_Label_EXSTYLE} 0 0 100% 12u 'freesoft.org'
	Pop $0

	nsDialogs::CreateControl /NOUNLOAD STATIC ${__NSD_Label_STYLE}|${ES_CENTER} ${__NSD_Label_EXSTYLE} 0 15u 100% 12u 'Hoffman 1.4'
	Pop $0

	nsDialogs::CreateControl /NOUNLOAD STATIC ${__NSD_Label_STYLE}|${ES_CENTER} ${__NSD_Label_EXSTYLE} 0 40u 100% 12u 'Chess endgame retrograde analyzer'
	Pop $0

	UserInfo::GetName
	Pop $R0

	StrCpy $1 ${WS_DISABLED}
	UserInfo::GetAccountType
	Pop $0
	${If} $0 == "Admin"
		StrCpy $1 0
	${EndIf}
	${If} $0 == "Power"
		StrCpy $1 0
	${EndIf}

	${NSD_CreateRadioButton} 0 80u 100% 12u 'Install for "$R0"'
	Pop $R1

	; ${NSD_CreateRadioButton} 0 100u 100% 12u "Install for all users"
	nsDialogs::CreateControl /NOUNLOAD BUTTON ${__NSD_RadioButton_STYLE}|$1 0  0 100u 100% 12u "Install for all users"
	Pop $R2

	${If} $CurrentUserOnlyInstallState == ${BST_CHECKED}
		${NSD_Check} $R1
	${ElseIf} $1 == 0
		${NSD_Check} $R2
	${Else}
		${NSD_Check} $R1
	${EndIf}

	nsDialogs::Show
FunctionEnd

Function installTypePageLeave
	${NSD_GetState} $R1 $CurrentUserOnlyInstallState
	${If} $CurrentUserOnlyInstallState == ${BST_CHECKED}
		SetShellVarContext current
	${Else}
		SetShellVarContext all
	${EndIf}
FunctionEnd

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
  
  ; Set output path to the installation directory.
  SetOutPath $INSTDIR
  File "genctlfile.pl"
  
  ; Put executable and all of its cygwin DLLs into subdir bin
  SetOutPath $INSTDIR\bin
  File "hoffman.exe"
  File "pawngen"
  File "\cygwin64\bin\cygwin1.dll"
  File "\cygwin64\bin\cygstdc++-*.dll"
  File "\cygwin64\bin\cyggcc_s-seh-1.dll"
  File "\cygwin64\bin\cygboost_system-*.dll"
  File "\cygwin64\bin\cygboost_filesystem-*.dll"
  File "\cygwin64\bin\cygboost_iostreams-*.dll"
  File "\cygwin64\bin\cygcrypto-*.dll"
  File "\cygwin64\bin\cygglibmm-*.dll"
  File "\cygwin64\bin\cygiconv-*.dll"
  File "\cygwin64\bin\cygncursesw-*.dll"
  File "\cygwin64\bin\cygreadline*.dll"
  File "\cygwin64\bin\cygssl-*.dll"
  File "\cygwin64\bin\cygxml2-*.dll"
  File "\cygwin64\bin\cygxml++-*.dll"
  File "\cygwin64\bin\cygbz2-*.dll"
  File "\cygwin64\bin\cyglzma-*.dll"
  File "\cygwin64\bin\cygglib-*.dll"
  File "\cygwin64\bin\cyggobject-*.dll"
  File "\cygwin64\bin\cygsigc-*.dll"
  File "\cygwin64\bin\cygintl-*.dll"
  File "\cygwin64\bin\cygpcre-*.dll"
  File "\cygwin64\bin\cygffi-*.dll"
  File "\cygwin64\bin\cyggmodule-*.dll"
  File "\cygwin64\bin\cygz.dll"

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
  CreateShortCut "$SMPROGRAMS\Hoffman\Hoffman Reference Guide.lnk" "$INSTDIR\reference.pdf"
  CreateShortCut "$SMPROGRAMS\Hoffman\Hoffman Tutorial.lnk" "$INSTDIR\tutorial.pdf"
  CreateShortCut "$SMPROGRAMS\Hoffman\Hoffman.lnk" "cmd.exe" "/k set path=%path%;$INSTDIR\bin"

SectionEnd

;--------------------------------

; Uninstaller

Section "Uninstall"
  
  ; Remove registry keys
  DeleteRegKey SHCTX "Software\Microsoft\Windows\CurrentVersion\Uninstall\Hoffman"
  DeleteRegKey SHCTX SOFTWARE\freesoft.org\Hoffman

  ; Remove files and uninstaller, including any tablebases and/or history files
  Delete "$INSTDIR\bin\hoffman.exe"
  Delete "$INSTDIR\bin\pawngen"
  Delete "$INSTDIR\bin\*.dll"
  Delete "$INSTDIR\xml\*.xml"
  Delete "$INSTDIR\xml\*.htb"
  Delete "$INSTDIR\genctlfile.pl"
  Delete "$INSTDIR\reference.pdf"
  Delete "$INSTDIR\tutorial.pdf"
  Delete "$INSTDIR\*.htb"
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
