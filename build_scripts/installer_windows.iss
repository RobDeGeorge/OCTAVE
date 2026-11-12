; OCTAVE Windows Installer Script (Inno Setup)
; Download Inno Setup from: https://jrsoftware.org/isdl.php

#define MyAppName "OCTAVE"
#define MyAppVersion "0.9.6"
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
Source: "..\dist\OCTAVE\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs; BeforeInstall: EndRunningAppOnce
; NOTE: Don't use "Flags: ignoreversion" on any shared system files

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\{cm:UninstallProgram,{#MyAppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon
Name: "{userappdata}\Microsoft\Internet Explorer\Quick Launch\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: quicklaunchicon

[Run]
; Allow the bundled adb through Windows Firewall on private networks, so the
; first QR pairing (adb listens for the phone over mDNS) doesn't stop on an
; "allow adb, Publisher: Unknown" prompt. Delete first so an upgrade doesn't
; stack duplicate rules. Admin installs only: a per-user install can't change
; the firewall and keeps the prompt.
Filename: "{sys}\netsh.exe"; Parameters: "advfirewall firewall delete rule name=""OCTAVE adb"""; Flags: runhidden; Check: IsAdminInstallMode
Filename: "{sys}\netsh.exe"; Parameters: "advfirewall firewall add rule name=""OCTAVE adb"" dir=in action=allow program=""{app}\platform-tools\adb.exe"" profile=private enable=yes"; Flags: runhidden; Check: IsAdminInstallMode
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(MyAppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent

[UninstallRun]
Filename: "{sys}\netsh.exe"; Parameters: "advfirewall firewall delete rule name=""OCTAVE adb"""; Flags: runhidden; RunOnceId: "DeleteAdbFirewallRule"; Check: IsAdminInstallMode
; ... and the rules Windows itself made for this adb when its own prompt was
; answered (on a public network the prompt still comes; Allow or Cancel both
; leave two "adb.exe" rules behind). They name a file that is gone now, and a
; left-over Block rule would win over "OCTAVE adb" after a reinstall.
Filename: "{sys}\netsh.exe"; Parameters: "advfirewall firewall delete rule name=all dir=in program=""{app}\platform-tools\adb.exe"""; Flags: runhidden; RunOnceId: "DeleteAdbPromptRules"; Check: IsAdminInstallMode

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

// The adb server OCTAVE starts for a phone outlives the app (adb's own habit)
// and keeps platform-tools\adb.exe and its two DLLs open: an uninstall then
// left those three files and a running adb behind (0.9.6-rc5), and an upgrade
// could not replace them. So Setup and the uninstaller end it first. Only the
// adb in this folder, by its path: `adb kill-server` would also end a server
// another SDK's adb started.
//
// Whether one is running is read off the file: a running program's file
// cannot be opened for writing. Not held: nothing to do.
function FileInUse(const Path: String): Boolean;
var
  F: TFileStream;
begin
  Result := False;
  try
    F := TFileStream.Create(Path, fmOpenReadWrite or fmShareExclusive);
    F.Free;
  except
    Result := True;
  end;
end;

// Waits until the file (adb.exe, OCTAVE.exe) is free again and logs how long
// that took. A killed adb does not free its files at once: 0.1 to 0.4 s on an
// idle Windows 11, about 1.3 s while an uninstall was running (measured on
// 0.9.6-rc6).
function FreeWithin(const Path: String; const Seconds: Integer): Boolean;
var
  I: Integer;
begin
  Result := True;
  for I := 0 to Seconds * 5 do
  begin
    if not FileInUse(Path) then
    begin
      Log(ExtractFileName(Path) + ' is free, ' + IntToStr(I * 200) + ' ms later');
      Exit;
    end;
    Sleep(200);
  end;
  Log(ExtractFileName(Path) + ' is still in use ' + IntToStr(Seconds) + ' s later');
  Result := False;
end;

// The processes called Name (adb.exe, OCTAVE.exe) that run from this install,
// by their path: asked of WMI, inside this process, with nothing to start and
// no command line to quote. Each one seen goes to the log with its path.
// Returns how many run from Exe, and ends those when Terminate is set.
function ProcessesFromInstall(const Name, Exe: String; const Terminate: Boolean): Integer;
var
  Locator, Services, Processes, Process: Variant;
  I, Pid: Integer;
  Path: String;
begin
  Result := 0;
  try
    Locator := CreateOleObject('WbemScripting.SWbemLocator');
    Services := Locator.ConnectServer('.', 'root\CIMV2');
    Processes := Services.ExecQuery('SELECT * FROM Win32_Process WHERE Name = "' + Name + '"');
    for I := 0 to Processes.Count - 1 do
    begin
      Process := Processes.ItemIndex(I);
      Pid := Process.ProcessId;
      if VarIsNull(Process.ExecutablePath) then
        Path := ''
      else
        Path := Process.ExecutablePath;
      Log(Name + ', process ' + IntToStr(Pid) + ', from "' + Path + '"');
      if CompareText(Path, Exe) = 0 then
      begin
        if Terminate then
          Process.Terminate();
        Result := Result + 1;
      end;
    end;
  except
    Log('Asking WMI for the ' + Name + ' processes failed: ' + GetExceptionMessage);
  end;
end;

// PowerShell's lines for the log (/LOG). A handler of our own rather than nil:
// with nil and no log file, Inno Setup 6 shows the console window.
procedure LogAdbStep(const S: String; const Error, FirstLine: Boolean);
begin
  Log('  ' + S);
end;

// The same through PowerShell, for when WMI ended none or the file stayed in
// use. It also ends an adb.exe that shows no path at all when none shows this
// one: the file is in use, so one of them runs from it.
procedure EndBundledAdbByPowerShell(const Adb: String);
var
  Quoted: String;
  ResultCode: Integer;
begin
  // The path goes into a single-quoted PowerShell string
  Quoted := Adb;
  StringChangeEx(Quoted, '''', '''''', True);
  if ExecAndLogOutput(ExpandConstant('{sys}\WindowsPowerShell\v1.0\powershell.exe'),
          '-NoProfile -NonInteractive -ExecutionPolicy Bypass -Command "$adb = ''' + Quoted + ''';'
          + ' $all = @(Get-Process adb -ErrorAction SilentlyContinue);'
          + ' foreach ($a in $all) { Write-Output (''adb.exe, process '' + $a.Id + '', from '' + $a.Path) };'
          + ' $mine = @($all | Where-Object { $_.Path -eq $adb });'
          + ' if ($mine.Count -eq 0) { $mine = @($all | Where-Object { -not $_.Path }) };'
          + ' foreach ($a in $mine) { Stop-Process -Id $a.Id -Force };'
          + ' $n = $mine.Count; exit $n"',
          '', SW_HIDE, ewWaitUntilTerminated, ResultCode, @LogAdbStep) then
    Log('PowerShell ended ' + IntToStr(ResultCode) + ' adb')
  else
    Log('PowerShell did not start (' + SysErrorMessage(ResultCode) + ')');
end;

// Ending the adb is half of it; the other half is the wait. The rc6 step
// ended it and went on: the uninstaller, which tries each file once, was at
// the three files 0.9 s later and the adb was gone 0.4 s after that. Every
// step goes to the log, which in rc6 could say none of this.
procedure StopBundledAdb;
var
  Adb: String;
  Ended: Integer;
begin
  Adb := ExpandConstant('{app}\platform-tools\adb.exe');
  if not FileExists(Adb) then
    Exit;
  if not FileInUse(Adb) then
    Exit;
  Log('The bundled adb is running (' + Adb + ' is in use)');
  Ended := ProcessesFromInstall('adb.exe', Adb, True);
  Log('WMI ended ' + IntToStr(Ended) + ' adb');
  if (Ended > 0) and FreeWithin(Adb, 10) then
    Exit;
  EndBundledAdbByPowerShell(Adb);
  FreeWithin(Adb, 10);
end;

// The backstop for the uninstall, which tries each file once: whatever was
// still held when it came to the three adb files. Stops the adb again if it
// is (still, or again) running, then removes the three files and the two
// folders if they are empty by now (RemoveDir leaves a folder that is not).
procedure RemoveAdbLeftovers;
var
  Dir: String;
  I: Integer;
begin
  Dir := ExpandConstant('{app}\platform-tools');
  if not DirExists(Dir) then
    Exit;
  Log('The uninstall left files in ' + Dir);
  StopBundledAdb;
  for I := 1 to 25 do
  begin
    DeleteFile(Dir + '\adb.exe');
    DeleteFile(Dir + '\AdbWinApi.dll');
    DeleteFile(Dir + '\AdbWinUsbApi.dll');
    if not (FileExists(Dir + '\adb.exe') or FileExists(Dir + '\AdbWinApi.dll')
            or FileExists(Dir + '\AdbWinUsbApi.dll')) then
      Break;
    Sleep(200);
  end;
  if RemoveDir(Dir) then
    Log('Removed them')
  else
    Log('Could not remove ' + Dir);
  RemoveDir(ExpandConstant('{app}'));
end;

// A running OCTAVE keeps OCTAVE.exe and its DLLs open. With the wizard Setup
// lists it on "Preparing to Install" and closes it (Restart Manager). A silent
// install does the same where it can, and cannot from another session, which
// is where a deployment tool running as SYSTEM is: the first file then failed
// with "DeleteFile failed; code 5" and Setup exited 5 (0.9.6-rc7). The
// uninstaller has no such step at all. So what Restart Manager left running is
// ended here, by its path as the adb is. Returns how many were ended.
function EndRunningApp: Integer;
var
  Exe: String;
begin
  Exe := ExpandConstant('{app}\{#MyAppExeName}');
  Result := ProcessesFromInstall('{#MyAppExeName}', Exe, True);
  if Result = 0 then
    Exit;
  Log('WMI ended ' + IntToStr(Result) + ' {#MyAppExeName}');
  FreeWithin(Exe, 10);
end;

function HasSwitch(const Name: String): Boolean;
var
  I: Integer;
begin
  Result := False;
  for I := 1 to ParamCount do
    if CompareText(ParamStr(I), Name) = 0 then
      Result := True;
end;

var
  RunningAppSeen: Boolean;

// BeforeInstall of [Files], so before the first file and after Restart Manager
// had its turn (Setup closes applications inside the install step, after
// CurStepChanged(ssInstall)). Only a silent install ends the app: with the
// wizard the user was asked, and for an answer of "do not close" Setup has its
// own Retry box. /NOCLOSEAPPLICATIONS is the same answer on a command line.
// The adb once more either way: an OCTAVE that was showing the QR pairing
// popup starts a new one within seconds of the first being ended.
procedure EndRunningAppOnce;
begin
  if RunningAppSeen then
    Exit;
  RunningAppSeen := True;
  if WizardSilent and not HasSwitch('/NOCLOSEAPPLICATIONS') then
    EndRunningApp;
  StopBundledAdb;
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssInstall then
  begin
    MigrateOld32BitInstall;
    StopBundledAdb;
  end;
end;

// The uninstaller with a window asks for OCTAVE to be closed, in Inno Setup's
// own words for it, and goes on once none runs from this folder. Cancel keeps
// the install. A silent uninstall ends it instead (CurUninstallStepChanged).
function InitializeUninstall: Boolean;
var
  Exe: String;
begin
  Result := True;
  if UninstallSilent then
    Exit;
  Exe := ExpandConstant('{app}\{#MyAppExeName}');
  while ProcessesFromInstall('{#MyAppExeName}', Exe, False) > 0 do
    if SuppressibleMsgBox(FmtMessage(SetupMessage(msgUninstallAppRunningError), ['{#MyAppName}']),
                          mbError, MB_OKCANCEL, IDCANCEL) <> IDOK then
    begin
      Result := False;
      Exit;
    end;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usUninstall then
  begin
    if UninstallSilent then
      EndRunningApp;
    StopBundledAdb;
  end
  else if CurUninstallStep = usPostUninstall then
    RemoveAdbLeftovers;
end;
