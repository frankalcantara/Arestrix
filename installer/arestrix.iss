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
UsePreviousTasks=yes
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
brazilianportuguese.PreflightTitle=Verificação do ambiente
english.PreflightTitle=Environment check
brazilianportuguese.PreflightContinue=Itens marcados com [--] são opcionais ou podem ser configurados depois no Studio. Deseja continuar com a instalação?
english.PreflightContinue=Items marked [--] are optional or can be configured later in Studio. Continue with the installation?
brazilianportuguese.GitName=Git instalado (sincronização de projetos)
english.GitName=Git installed (project sync)
brazilianportuguese.GitIdentityName=Identidade Git (user.name/user.email)
english.GitIdentityName=Git identity (user.name/user.email)
brazilianportuguese.GitHubCredentialName=Credencial do GitHub (HTTPS, SSH ou gh)
english.GitHubCredentialName=GitHub credential (HTTPS, SSH or gh)
brazilianportuguese.MSVCName=MSVC Build Tools (compilar pipelines)
english.MSVCName=MSVC Build Tools (compile pipelines)
brazilianportuguese.ApiKeyName=Chave de API de provedor
english.ApiKeyName=Provider API key
brazilianportuguese.DownloadHint=Download:
english.DownloadHint=Download:

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"

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
  MSVCUrl = 'https://visualstudio.microsoft.com/visual-cpp-build-tools/';
  GitUrl = 'https://git-scm.com/download/win';

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

function RunAndCapture(const CommandLine: string; var Output: string): Boolean;
var
  TempFile, TempScript: string;
  ResultCode: Integer;
  Lines: TArrayOfString;
  Index: Integer;
begin
  TempFile := ExpandConstant('{tmp}\arestrix-preflight.txt');
  TempScript := ExpandConstant('{tmp}\arestrix-preflight.cmd');
  DeleteFile(TempFile);
  DeleteFile(TempScript);
  Output := '';
  SaveStringToFile(TempScript,
    '@echo off' + #13#10 + CommandLine + ' > "' + TempFile + '" 2>&1' + #13#10, False);
  Result := Exec(ExpandConstant('{cmd}'), '/c "' + TempScript + '"',
                 '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
  if LoadStringsFromFile(TempFile, Lines) then
    for Index := 0 to GetArrayLength(Lines) - 1 do
      Output := Output + Lines[Index] + #13#10;
  DeleteFile(TempFile);
  DeleteFile(TempScript);
  Result := Result and (ResultCode = 0);
end;

function GitInstalled(): Boolean;
var
  Output: string;
begin
  Result := RunAndCapture('git --version', Output) and (Trim(Output) <> '');
end;

function GitIdentityConfigured(): Boolean;
var
  Name, Email: string;
begin
  Result :=
    RunAndCapture('git config --global --get user.name', Name) and (Trim(Name) <> '') and
    RunAndCapture('git config --global --get user.email', Email) and (Trim(Email) <> '');
end;

function GitHubCredentialPresent(): Boolean;
var
  Output: string;
begin
  Result := (GetEnv('GH_TOKEN') <> '') or (GetEnv('GITHUB_TOKEN') <> '');

  if not Result then
    if RunAndCapture('cmdkey /list', Output) then
      Result := Pos('github.com', Lowercase(Output)) > 0;

  if not Result then
    Result :=
      FileExists(ExpandConstant('{userprofile}\.ssh\id_ed25519')) or
      FileExists(ExpandConstant('{userprofile}\.ssh\id_rsa'));

  if not Result then
    Result := RunAndCapture('gh auth status', Output);
end;

function MSVCInstalled(): Boolean;
var
  Vswhere, Output: string;
begin
  Vswhere := GetEnv('ProgramFiles(x86)') + '\Microsoft Visual Studio\Installer\vswhere.exe';
  if not FileExists(Vswhere) then
    Vswhere := GetEnv('ProgramFiles') + '\Microsoft Visual Studio\Installer\vswhere.exe';
  if not FileExists(Vswhere) then
  begin
    Result := False;
    Exit;
  end;

  Result :=
    RunAndCapture('"' + Vswhere + '" -latest -products * -requires ' +
                  'Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath', Output) and
    (Trim(Output) <> '');

  if not Result then
    Result := RunAndCapture('where cl', Output) and (Trim(Output) <> '');
end;

function AddApiKeyIfPresent(const EnvName, LabelName: string; var Names: string): Boolean;
begin
  Result := GetEnv(EnvName) <> '';
  if Result then
  begin
    if Names <> '' then Names := Names + ', ';
    Names := Names + LabelName;
  end;
end;

function CollectApiKeys(var Names: string): Boolean;
begin
  Names := '';
  Result := False;
  if AddApiKeyIfPresent('DEEPSEEK_API_KEY', 'DeepSeek', Names) then Result := True;
  if AddApiKeyIfPresent('OPENAI_API_KEY', 'OpenAI', Names) then Result := True;
  if AddApiKeyIfPresent('ANTHROPIC_API_KEY', 'Anthropic', Names) then Result := True;
  if AddApiKeyIfPresent('GEMINI_API_KEY', 'Gemini', Names) then Result := True;
  if AddApiKeyIfPresent('MISTRAL_API_KEY', 'Mistral', Names) then Result := True;
  if AddApiKeyIfPresent('JEV_API_KEY', 'JEV', Names) then Result := True;
  if AddApiKeyIfPresent('LOCAL_LLM_API_KEY', 'Local', Names) then Result := True;
  if AddApiKeyIfPresent('ARESTRIX_AI_GATEWAY_KEY', 'Gateway', Names) then Result := True;
end;

function StatusLine(Ok: Boolean; const LabelText: string): string;
begin
  if Ok then
    Result := '  [OK] ' + LabelText + #13#10
  else
    Result := '  [--] ' + LabelText + #13#10;
end;

function BuildPreflightReport(var NeedsAttention: Boolean): string;
var
  Report, ApiLabel, ApiNames: string;
  GitOk, GitIdOk, GitHubOk, MsvcOk, ApiOk: Boolean;
begin
  GitOk := GitInstalled();
  GitIdOk := GitIdentityConfigured();
  GitHubOk := GitHubCredentialPresent();
  MsvcOk := MSVCInstalled();
  ApiOk := CollectApiKeys(ApiNames);

  ApiLabel := CustomMessage('ApiKeyName');
  if ApiOk then ApiLabel := ApiLabel + ' (' + ApiNames + ')';

  Report :=
    '  [OK] ' + CustomMessage('WebView2Name') + #13#10 +
    '  [OK] ' + CustomMessage('VCRedistName') + #13#10 +
    StatusLine(GitOk, CustomMessage('GitName')) +
    StatusLine(GitIdOk, CustomMessage('GitIdentityName')) +
    StatusLine(GitHubOk, CustomMessage('GitHubCredentialName')) +
    StatusLine(MsvcOk, CustomMessage('MSVCName')) +
    StatusLine(ApiOk, ApiLabel);

  if not MsvcOk then
    Report := Report + '       ' + CustomMessage('DownloadHint') + ' ' + MSVCUrl + #13#10;
  if not GitOk then
    Report := Report + '       ' + CustomMessage('DownloadHint') + ' ' + GitUrl + #13#10;

  NeedsAttention := not (GitOk and GitIdOk and GitHubOk and MsvcOk and ApiOk);
  Result := Report;
  Log('Preflight report:' + #13#10 + Report);
end;

function InitializeSetup(): Boolean;
var
  Missing, Report: string;
  ResultCode: Integer;
  NeedsAttention, ForcePreflight: Boolean;
begin
  Missing := '';
  if not WebView2Installed() then
    Missing := Missing + '- ' + CustomMessage('WebView2Name') + #13#10;
  if not VCRedistInstalled() then
    Missing := Missing + '- ' + CustomMessage('VCRedistName') + #13#10;

  if Missing <> '' then
  begin
    Log('Missing required prerequisites: ' + Missing);
    if not WizardSilent then
    begin
      if MsgBox(CustomMessage('MissingDependencies') + #13#10#13#10 + Missing + #13#10 +
                CustomMessage('OpenDownloads'),
                mbError, MB_YESNO) = IDYES then
      begin
        if not WebView2Installed() then
          ShellExec('open', WebView2Url, '', '', SW_SHOWNORMAL, ewNoWait, ResultCode);
        if not VCRedistInstalled() then
          ShellExec('open', VCRedistUrl, '', '', SW_SHOWNORMAL, ewNoWait, ResultCode);
      end;
    end;
    Result := False;
    Exit;
  end;

  Report := BuildPreflightReport(NeedsAttention);
  ForcePreflight := ExpandConstant('{param:preflight|0}') = '1';

  if not WizardSilent and (NeedsAttention or ForcePreflight) then
  begin
    if MsgBox(CustomMessage('PreflightTitle') + #13#10#13#10 + Report + #13#10 +
              CustomMessage('PreflightContinue'),
              mbInformation, MB_YESNO) = IDNO then
    begin
      Result := False;
      Exit;
    end;
  end;

  Result := True;
end;

