; OCTAVE Windows Installer Script (Inno Setup)
; Download Inno Setup from: https://jrsoftware.org/isdl.php

#define MyAppName "OCTAVE"
#define MyAppVersion "0.9.4"
#define MyAppPublisher "Rob DeGeorge"
#define MyAppURL "https://github.com/RobDeGeorge/OCTAVE"
#define MyAppExeName "OCTAVE.exe"

[Setup]
; Unique identifier for this application
AppId={{A1B2C3D4-E5F6-7890-ABCD-EF1234567890}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
AppUpdatesURL={#MyAppURL}
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
AllowNoIcons=yes
; Output settings
OutputDir=..\dist
OutputBaseFilename=OCTAVE-{#MyAppVersion}-windows-x86_64
; Compression
Compression=lzma2/ultra64
SolidCompression=yes
; Modern installer style
WizardStyle=modern
; Require admin for Program Files installation
PrivilegesRequired=admin
PrivilegesRequiredOverridesAllowed=dialog
; Minimum Windows version (Windows 10)
MinVersion=10.0
; Uninstall settings
UninstallDisplayIcon={app}\{#MyAppExeName}
; Icon (uncomment when you have an icon)
; SetupIconFile=..\build_resources\icon.ico

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "quicklaunchicon"; Description: "{cm:CreateQuickLaunchIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked; OnlyBelowVersion: 6.1; Check: not IsAdminInstallMode

[Files]
; Main application files
Source: "..\dist\OCTAVE\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
; NOTE: Don't use "Flags: ignoreversion" on any shared system files

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\{cm:UninstallProgram,{#MyAppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon
Name: "{userappdata}\Microsoft\Internet Explorer\Quick Launch\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: quicklaunchicon

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(MyAppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
; Clean up user data on uninstall (optional - remove if you want to preserve settings)
Type: filesandordirs; Name: "{userappdata}\OCTAVE"

; No Visual C++ Redistributable check: CI deploys the VC runtime DLLs
; (msvcp140, vcruntime140, ...) app-local next to OCTAVE.exe, so a clean
; Windows needs nothing extra. The old check showed a plain MsgBox, which
; /SUPPRESSMSGBOXES cannot silence, so every /VERYSILENT install hung on it.
