; Inno Setup script (6 or 7) for Modeless IME Helper. Build with installer.bat, which builds the exe
; first, or: ISCC installer\ModelessImeHelper.iss [/DAppVersion=x.y.z]
; Output: build\installer\ModelessImeHelper-Setup-<version>.exe
;
; Per-user install (no admin): the app refuses to run elevated, and an elevated Setup would start it
; elevated from the "launch now" checkbox. Saved as UTF-8 with BOM (Japanese strings).

#define AppName "Modeless IME Helper"
#define AppExe "ModelessImeHelper.exe"
#ifndef AppVersion
  #define AppVersion "0.1.0"
#endif
#define ExeSource SourcePath + "..\build\" + AppExe
#if !FileExists(ExeSource)
  #error build\ModelessImeHelper.exe not found: run build.bat first
#endif

[Setup]
AppId={{09C76F6D-D1F1-4E18-B71A-759D44E37081}
AppName={#AppName}
AppVersion={#AppVersion}
VersionInfoVersion={#AppVersion}
VersionInfoProductName={#AppName}
VersionInfoDescription={#AppName} セットアップ
PrivilegesRequired=lowest
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
WizardStyle=modern
SetupIconFile=..\res\app.ico
UninstallDisplayIcon={app}\{#AppExe}
OutputDir=..\build\installer
OutputBaseFilename=ModelessImeHelper-Setup-{#AppVersion}
Compression=lzma2
SolidCompression=yes
; A running copy is closed in [Code] (WM_CLOSE to its hidden window, so it restores the IME);
; Restart Manager would send WM_ENDSESSION, which the app does not act on.
CloseApplications=no

[Languages]
Name: "ja"; MessagesFile: "compiler:Languages\Japanese.isl"

[Tasks]
Name: "autostart"; Description: "Windows 起動時に自動実行する"

[Files]
Source: "{#ExeSource}"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\{#AppExe}"

[Registry]
; Same value the tray menu's 自動実行 item writes and checks (src/main.cpp kRunValue).
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "ModelessImeHelper"; ValueData: """{app}\{#AppExe}"""; Tasks: autostart
; Settings the app saves (TriggerShift); removed on uninstall.
Root: HKCU; Subkey: "Software\ModelessImeHelper"; Flags: uninsdeletekey dontcreatekey

[Run]
Filename: "{app}\{#AppExe}"; Description: "{#AppName} を起動する"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
; Written next to the exe by --selftest.
Type: files; Name: "{app}\selftest.txt"

[Code]
const
  WM_CLOSE = $0010;
  MainWndClass = 'ModelessImeHelper.Main';
  AppMutex = 'Local\ModelessImeHelper.single-instance';
  RunKey = 'Software\Microsoft\Windows\CurrentVersion\Run';
  RunValue = 'ModelessImeHelper';

// Asks a running copy to quit (WM_CLOSE to its hidden main window) and waits up to 5 s for it to
// release its single-instance mutex. True if none is running afterwards.
function CloseRunningApp: Boolean;
var
  Wnd: HWND;
  I: Integer;
begin
  Wnd := FindWindowByClassName(MainWndClass);
  if Wnd <> 0 then
    PostMessage(Wnd, WM_CLOSE, 0, 0);
  for I := 1 to 50 do
  begin
    if (FindWindowByClassName(MainWndClass) = 0) and not CheckForMutexes(AppMutex) then
    begin
      Sleep(200);  // the mutex goes before the process (and its lock on the exe) is gone
      Result := True;
      Exit;
    end;
    Sleep(100);
  end;
  Result := False;
end;

// PrivilegesRequired=lowest never requests elevation but can't drop it: started via "Run as
// administrator", Setup would launch the app elevated from the last page (where it refuses to run),
// and an over-the-shoulder elevation would install into the admin account's profile and HKCU.
function InitializeSetup: Boolean;
begin
  Result := not IsAdmin;
  if not Result then
    SuppressibleMsgBox('{#AppName} のセットアップは管理者として実行できません。' + #13#10 +
                       'ファイルをダブルクリックして、通常の権限で実行してください。',
                       mbError, MB_OK, IDOK);
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  if CloseRunningApp then
    Result := ''
  else
    Result := '{#AppName} を終了できませんでした。タスクトレイのアイコンを右クリックして「終了」を選んでから、もう一度実行してください。';
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  Value: String;
begin
  // usUninstall comes after the "remove completely?" confirmation (InitializeUninstall comes before
  // it, so closing there would quit the app even when the user answers No) and before files go.
  if CurUninstallStep = usUninstall then
  begin
    if not CloseRunningApp then
      SuppressibleMsgBox('{#AppName} を終了できませんでした。一部のファイルが削除されずに残ることがあります。タスクトレイのアイコンから終了してから、もう一度アンインストールしてください。',
                         mbError, MB_OK, IDOK);
    Exit;
  end;
  // The Run value may have been written by Setup or later by the tray menu: remove it whenever it
  // points at this installation (a copy run from elsewhere keeps its own entry).
  if CurUninstallStep <> usPostUninstall then Exit;
  if RegQueryStringValue(HKCU, RunKey, RunValue, Value) and
     (Pos(Lowercase(ExpandConstant('{app}\{#AppExe}')), Lowercase(Value)) > 0) then
    RegDeleteValue(HKCU, RunKey, RunValue);
end;
