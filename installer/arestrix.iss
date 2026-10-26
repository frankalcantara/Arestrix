; Arestrix Windows installer (Inno Setup 6).
;
; The release pipeline passes the portable staging folder, product version and
; output name as preprocessor defines; see scripts/package_release.py.
;
;   ISCC.exe /DAppVersion=2.0.0-beta.1 /DAppVersionNumeric=2.0.0.0 ^
;     /DStageDir=...\arestrix-2.0.0-beta.1-windows-x64 ^
;     /DOutputDir=...\release /DOutputBaseFilename=arestrix-2.0.0-beta.1-windows-x64-setup ^
;     installer\arestrix.iss

#ifndef AppVersion
  #error AppVersion define is required
#endif

#ifndef AppVersionNumeric
  #define AppVersionNumeric "2.0.0.0"
#endif

#ifndef StageDir
  #error StageDir define is required
#endif

#ifndef OutputDir
  #define OutputDir "..\release"
#endif

#ifndef OutputBaseFilename
  #define OutputBaseFilename "arestrix-windows-setup"
#endif

#ifndef AppArch
  #define AppArch "x64"
#endif

#ifndef IconFile
  #define IconFile "arestrix.ico"
#endif

#define AppName "Arestrix"
#define AppPublisher "Frank de Alcantara"
#define AppURL "https://github.com/frankalcantara/Arestrix"
#define AppExeName "arestrix.exe"
#define AppId "{{43AE949C-FBF4-46BF-A878-EB6653AA24A1}"

[Setup]
AppId={#AppId}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}
AppPublisherURL={#AppURL}
AppSupportURL={#AppURL}/discussions
AppUpdatesURL={#AppURL}/releases
DefaultDirName={localappdata}\Programs\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
LicenseFile={#StageDir}\LICENSE
OutputDir={#OutputDir}
OutputBaseFilename={#OutputBaseFilename}
SetupIconFile={#IconFile}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
UninstallDisplayIcon={app}\arestrix.ico
CloseApplications=yes
RestartApplications=no
UsePreviousAppDir=yes
SetupLogging=yes
VersionInfoVersion={#AppVersionNumeric}
VersionInfoProductName={#AppName}
VersionInfoProductVersion={#AppVersionNumeric}
VersionInfoCompany={#AppPublisher}
VersionInfoDescription={#AppName} Studio installer

[Languages]
Name: "brazilianportuguese"; MessagesFile: "compiler:Languages\BrazilianPortuguese.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[CustomMessages]
brazilianportuguese.MissingDependencies=O Arestrix precisa dos seguintes componentes:
english.MissingDependencies=Arestrix requires the following components:
brazilianportuguese.OpenDownloads=Deseja abrir as páginas de download agora? Depois de instalar os componentes, execute este instalador novamente.
english.OpenDownloads=Open the download pages now? After installing the components, run this installer again.
brazilianportuguese.WebView2Name=Microsoft Edge WebView2 Runtime
english.WebView2Name=Microsoft Edge WebView2 Runtime
brazilianportuguese.VCRedistName=Microsoft Visual C++ Redistributable x64
english.VCRedistName=Microsoft Visual C++ Redistributable x64

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#StageDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#IconFile}"; DestDir: "{app}"; DestName: "arestrix.ico"; Flags: ignoreversion

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExeName}"; WorkingDir: "{app}"; IconFilename: "{app}\arestrix.ico"
Name: "{group}\{cm:UninstallProgram,{#AppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExeName}"; WorkingDir: "{app}"; IconFilename: "{app}\arestrix.ico"; Tasks: desktopicon

[Registry]
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\App Paths\arestrix.exe"; ValueType: string; ValueName: ""; ValueData: "{app}\{#AppExeName}"; Flags: uninsdeletekey

[Run]
Filename: "{app}\{#AppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(AppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
Type: files; Name: "{app}\arestrix-recent-projects.json"

[Code]
const
  WebView2Client = '{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}';
  WebView2Url = 'https://go.microsoft.com/fwlink/p/?LinkId=2124703';
  VCRedistUrl = 'https://aka.ms/vs/17/release/vc_redist.x64.exe';

function HasRegString(Root: Integer; const SubKey, ValueName: string): Boolean;
var
  Value: string;
begin
  Result := RegQueryStringValue(Root, SubKey, ValueName, Value) and (Value <> '');
end;

function HasRegDwordOne(Root: Integer; const SubKey, ValueName: string): Boolean;
var
  Value: Cardinal;
begin
  Result := RegQueryDWordValue(Root, SubKey, ValueName, Value) and (Value = 1);
end;

function WebView2Installed(): Boolean;
begin
  Result :=
    HasRegString(HKLM32, 'SOFTWARE\Microsoft\EdgeUpdate\Clients\' + WebView2Client, 'pv') or
    HasRegString(HKLM64, 'SOFTWARE\Microsoft\EdgeUpdate\Clients\' + WebView2Client, 'pv') or
    HasRegString(HKCU, 'Software\Microsoft\EdgeUpdate\Clients\' + WebView2Client, 'pv');
end;

function VCRedistInstalled(): Boolean;
begin
  Result :=
    HasRegDwordOne(HKLM64, 'SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64', 'Installed') or
    HasRegDwordOne(HKLM32, 'SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64', 'Installed');
end;

function InitializeSetup(): Boolean;
var
  Missing: string;
  ResultCode: Integer;
begin
  Missing := '';
  if not WebView2Installed() then
    Missing := Missing + '- ' + CustomMessage('WebView2Name') + #13#10;
  if not VCRedistInstalled() then
    Missing := Missing + '- ' + CustomMessage('VCRedistName') + #13#10;

  if Missing = '' then
  begin
    Result := True;
    Exit;
  end;

  if MsgBox(CustomMessage('MissingDependencies') + #13#10#13#10 + Missing + #13#10 +
            CustomMessage('OpenDownloads'),
            mbError, MB_YESNO) = IDYES then
  begin
    if not WebView2Installed() then
      ShellExec('open', WebView2Url, '', '', SW_SHOWNORMAL, ewNoWait, ResultCode);
    if not VCRedistInstalled() then
      ShellExec('open', VCRedistUrl, '', '', SW_SHOWNORMAL, ewNoWait, ResultCode);
  end;

  Result := False;
end;
