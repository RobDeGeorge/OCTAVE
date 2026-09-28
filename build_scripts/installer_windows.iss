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
; 64-bit install mode: {autopf} is Program Files (not "Program Files (x86)")
; and the uninstall key goes to the 64-bit registry view. Before 0.9.4 the
; x64 app installed in 32-bit mode; see MigrateOld32BitInstall below.
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
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

[Code]
const
  UninstallKey = 'Software\Microsoft\Windows\CurrentVersion\Uninstall\{A1B2C3D4-E5F6-7890-ABCD-EF1234567890}_is1';

// Installers before 0.9.4 ran in 32-bit mode: the app went to
// "Program Files (x86)\OCTAVE" with its uninstall key in the 32-bit registry
// view, where this 64-bit-mode setup does not look, so an upgrade would leave
// a second copy and a second Add/Remove entry. Remove the old files and key
// directly. Running the old uninstaller is not an option: its
// [UninstallDelete] wipes %APPDATA%\OCTAVE, i.e. the user's settings.
procedure MigrateOld32BitInstall;
var
  OldDir: String;
begin
  if not RegQueryStringValue(HKLM32, UninstallKey, 'Inno Setup: App Path', OldDir) then
    Exit;
  // Only ever delete an old (x86) OCTAVE folder, never the new location.
  if (Pos('(x86)', OldDir) > 0)
     and (CompareText(ExtractFileName(RemoveBackslashUnlessRoot(OldDir)), '{#MyAppName}') = 0)
     and (CompareText(RemoveBackslashUnlessRoot(OldDir), ExpandConstant('{app}')) <> 0) then
  begin
    Log('Removing 32-bit install from ' + OldDir);
    DelTree(OldDir, True, True, True);
  end;
  RegDeleteKeyIncludingSubkeys(HKLM32, UninstallKey);
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssInstall then
    MigrateOld32BitInstall;
end;
