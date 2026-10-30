#define MyAppName "RBMK-1000 Control Room"
#define MyAppVersion "1.0.0"
#define MyAppExeName "RBMK-Control-Room.exe"

[Setup]
AppId={{8D4B51C5-0F98-4FDF-ACDF-8AFC67E47AD6}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
DefaultDirName={autopf}\RBMK Control Room
DefaultGroupName={#MyAppName}
UninstallDisplayIcon={app}\{#MyAppExeName}
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
OutputDir=..\out
OutputBaseFilename=RBMK-Control-Room-Setup-{#MyAppVersion}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
; Branding: the RBMK research logo is used for the setup executable, the wizard
; pages that Inno Setup can skin, and the shortcuts/uninstall entry (which take
; the icon embedded in the application executable).
SetupIconFile=..\assets\icons\rbmk.ico
WizardImageFile=wizard-large.bmp
WizardSmallImageFile=wizard-small.bmp
WizardImageStretch=no

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Additional shortcuts:"; Flags: unchecked

[Files]
Source: "..\build\installer-stage\app\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "..\build\installer-stage\vc_redist.x64.exe"; DestDir: "{tmp}"; Flags: deleteafterinstall

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; WorkingDir: "{userdocs}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; WorkingDir: "{userdocs}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "Launch {#MyAppName}"; Flags: nowait postinstall skipifsilent; WorkingDir: "{userdocs}"

[Code]
var
  RedistNeedsRestart: Boolean;

function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  ResultCode: Integer;
begin
  Result := '';
  ExtractTemporaryFile('vc_redist.x64.exe');
  if not Exec(ExpandConstant('{tmp}\vc_redist.x64.exe'), '/install /quiet /norestart', '', SW_HIDE, ewWaitUntilTerminated, ResultCode) then
    Result := 'Could not start the Microsoft Visual C++ Redistributable installer.'
  else if ResultCode = 3010 then
    RedistNeedsRestart := True
  else if (ResultCode <> 0) and (ResultCode <> 1638) then
    Result := 'The Microsoft Visual C++ Redistributable installation failed (exit code ' + IntToStr(ResultCode) + ').';
end;

function NeedRestart(): Boolean;
begin
  Result := RedistNeedsRestart;
end;