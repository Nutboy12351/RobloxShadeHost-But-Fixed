#ifndef HostExe
  #define HostExe SourcePath + "..\build\Release\RobloxShadeHost.exe"
#endif
#ifndef AppVersion
  #define AppVersion "0.1.0"
#endif
#ifndef DownloadManifestUrl
  #define DownloadManifestUrl "https://github.com/OMouta/RobloxShadeHost/releases/download/dlss5-assets/downloads.ini"
#endif

[Setup]
AppId={{77125AF5-DF0A-485A-A633-E64FBD50E90C}
AppName=RobloxShadeHost
AppVersion={#AppVersion}
AppPublisher=RobloxShadeHost contributors
AppPublisherURL=https://github.com/OMouta/RobloxShadeHost
AppSupportURL=https://github.com/OMouta/RobloxShadeHost/issues
DefaultDirName={localappdata}\Programs\RobloxShadeHost
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.18362
WizardStyle=modern
DisableDirPage=no
DisableProgramGroupPage=yes
InfoBeforeFile=CREDITS.txt
OutputDir=..\build\installer
OutputBaseFilename=RobloxShadeHost-Setup
Compression=lzma2
SolidCompression=yes
CloseApplications=yes
CloseApplicationsFilter=RobloxShadeHost.exe
RestartApplications=no
UninstallDisplayIcon={app}\RobloxShadeHost.exe
#ifdef TestMode
Uninstallable=no
#endif

[Types]
Name: "recommended"; Description: "RobloxShadeHost with ReShade"
Name: "custom"; Description: "Custom installation"; Flags: iscustom

[Components]
Name: "host"; Description: "RobloxShadeHost (required)"; Types: recommended custom; Flags: fixed
Name: "reshade"; Description: "ReShade with full add-on support"; Types: recommended
Name: "reshade\dlss5"; Description: "DLSS5 add-on - RenoDX / clshortfuse and NVIDIA"; Flags: dontinheritcheck

[Files]
Source: "{#HostExe}"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\LICENSE"; DestDir: "{app}"; Flags: ignoreversion
Source: "CREDITS.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "{tmp}\reshade-stage\dxgi.dll"; DestDir: "{app}"; Components: reshade; Flags: external ignoreversion; Check: ReShadeReady
Source: "{tmp}\reshade-stage\ReShade.ini"; DestDir: "{app}"; Components: reshade; Flags: external onlyifdoesntexist uninsneveruninstall; Check: ReShadeReady
Source: "{tmp}\ReShade-LICENSE.txt"; DestDir: "{app}"; Components: reshade; Flags: external ignoreversion; Check: ReShadeReady
Source: "{tmp}\nvngx_dlssnr.dll"; DestDir: "{app}"; ExternalSize: 165840496; Components: reshade\dlss5; Flags: external ignoreversion; Check: DLSSReady
Source: "{tmp}\renodx-dlss.addon64"; DestDir: "{app}"; ExternalSize: 2624512; Components: reshade\dlss5; Flags: external ignoreversion; Check: DLSSReady

[Icons]
#ifndef TestMode
Name: "{userprograms}\RobloxShadeHost"; Filename: "{app}\RobloxShadeHost.exe"; WorkingDir: "{app}"
#endif

[Code]
var
  DownloadPage: TDownloadWizardPage;
  LicensePage: TWizardPage;
  LicenseMemo: TNewMemo;
  AcceptLicense: TNewCheckBox;
  ReShadeVersion, ReShadeUrl, SkippedComponents: String;
  ReShadeInstalled, DLSSDownloaded: Boolean;

function ReShadeReady: Boolean;
begin
  Result := ReShadeInstalled;
end;

function DLSSReady: Boolean;
begin
  Result := ReShadeInstalled and DLSSDownloaded;
end;

procedure Download(const Url, FileName, Hash: String);
begin
  DownloadPage.Clear;
  DownloadPage.Add(Url, FileName, Hash);
  DownloadPage.Download;
end;

procedure LoadReShadeLicense;
var
  Html, Version: String;
  Contents: AnsiString;
  StartIndex, EndIndex, Index: Integer;
begin
  if ReShadeVersion <> '' then
    exit;

  Download('https://reshade.me/', 'reshade.html', '');
  if not LoadStringFromFile(ExpandConstant('{tmp}\reshade.html'), Contents) then
    RaiseException('Could not read the ReShade download page.');
  Html := String(Contents);
  EndIndex := Pos('_Addon.exe', Html);
  if EndIndex = 0 then
    RaiseException('Could not find the full add-on ReShade installer on reshade.me.');
  StartIndex := EndIndex - 1;
  while (StartIndex > 0) and (Html[StartIndex] <> '_') do
    StartIndex := StartIndex - 1;
  Version := Copy(Html, StartIndex + 1, EndIndex - StartIndex - 1);
  if (Length(Version) < 5) or (Length(Version) > 30) then
    RaiseException('The ReShade version is invalid.');
  for Index := 1 to Length(Version) do
    if Pos(Version[Index], '0123456789.') = 0 then
      RaiseException('The ReShade version is invalid.');

  Download('https://raw.githubusercontent.com/crosire/reshade/v' + Version + '/LICENSE.md',
    'ReShade-LICENSE.txt', '');
  LicenseMemo.Lines.LoadFromFile(ExpandConstant('{tmp}\ReShade-LICENSE.txt'));
  if Pos('Redistribution and use', LicenseMemo.Text) = 0 then
    RaiseException('Could not load the ReShade license.');
  ReShadeVersion := Version;
  ReShadeUrl := 'https://reshade.me/downloads/ReShade_Setup_' + Version + '_Addon.exe';
  LicensePage.Description := 'ReShade ' + Version + ' with full add-on support';
end;

procedure InitializeWizard;
begin
  DownloadPage := CreateDownloadPage('Downloading components',
    'Please wait while Setup prepares your selected components.', nil);
  DownloadPage.ShowBaseNameInsteadOfUrl := True;

  LicensePage := CreateCustomPage(wpSelectComponents, 'ReShade license',
    'Read the license before installing ReShade.');
  LicenseMemo := TNewMemo.Create(LicensePage);
  LicenseMemo.Parent := LicensePage.Surface;
  LicenseMemo.SetBounds(0, 0, LicensePage.SurfaceWidth, LicensePage.SurfaceHeight - ScaleY(35));
  LicenseMemo.ReadOnly := True;
  LicenseMemo.ScrollBars := ssVertical;
  AcceptLicense := TNewCheckBox.Create(LicensePage);
  AcceptLicense.Parent := LicensePage.Surface;
  AcceptLicense.SetBounds(0, LicenseMemo.Height + ScaleY(10), LicensePage.SurfaceWidth, ScaleY(20));
  AcceptLicense.Caption := 'I accept the ReShade license';
  AcceptLicense.Checked := ExpandConstant('{param:ACCEPTRESHADELICENSE|0}') = '1';
end;

function ShouldSkipPage(PageID: Integer): Boolean;
begin
  Result := (PageID = LicensePage.ID) and not WizardIsComponentSelected('reshade');
end;

function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;
  if (CurPageID = wpSelectComponents) and WizardIsComponentSelected('reshade') then begin
    DownloadPage.Show;
    try
      try
        LoadReShadeLicense;
      except
        Result := False;
        if not DownloadPage.AbortedByUser then
          SuppressibleMsgBox(GetExceptionMessage, mbError, MB_OK, IDOK);
      end;
    finally
      DownloadPage.Hide;
    end;
  end;
  if (CurPageID = LicensePage.ID) and not AcceptLicense.Checked then begin
    Result := False;
    SuppressibleMsgBox('Accept the ReShade license to continue, or go back and deselect ReShade.',
      mbInformation, MB_OK, IDOK);
  end;
end;

procedure PrepareReShade;
var
  Stage, Parameters: String;
  ExitCode: Integer;
begin
  if ReShadeInstalled then
    exit;
  LoadReShadeLicense;
  Download(ReShadeUrl, 'ReShade-Setup.exe', '');
  Stage := ExpandConstant('{tmp}\reshade-stage');
  if not ForceDirectories(Stage) then
    RaiseException('Could not prepare the ReShade installation folder.');
  ExtractTemporaryFile('RobloxShadeHost.exe');
  if not FileCopy(ExpandConstant('{tmp}\RobloxShadeHost.exe'), Stage + '\RobloxShadeHost.exe', False) then
    RaiseException('Could not prepare RobloxShadeHost for ReShade.');

  // Install in temporary storage so Inno Setup owns the final files and rollback.
  Parameters := '--headless --api dxgi "' + Stage + '\RobloxShadeHost.exe"';
  if FileExists(Stage + '\dxgi.dll') then
    Parameters := '--state update ' + Parameters;
  if not Exec(ExpandConstant('{tmp}\ReShade-Setup.exe'), Parameters, Stage,
    SW_HIDE, ewWaitUntilTerminated, ExitCode) then
    RaiseException('Could not start the ReShade installer.');
  if (ExitCode <> 0) or not FileExists(Stage + '\dxgi.dll') or
    not FileExists(Stage + '\ReShade.ini') then
    RaiseException('ReShade installation failed. Go back to retry or deselect ReShade.');
  ReShadeInstalled := True;
end;

procedure DownloadDLSSFile(const FileName: String);
var
  Manifest, Url, Hash: String;
  Index: Integer;
begin
  Manifest := ExpandConstant('{tmp}\downloads.ini');
  Url := GetIniString(FileName, 'url', '', Manifest);
  Hash := Lowercase(GetIniString(FileName, 'sha256', '', Manifest));
  if (Pos('https://github.com/OMouta/RobloxShadeHost/releases/download/', Url) <> 1) or
    (Length(Hash) <> 64) then
    RaiseException('The DLSS5 download manifest is invalid.');
  for Index := 1 to Length(Hash) do
    if Pos(Hash[Index], '0123456789abcdef') = 0 then
      RaiseException('The DLSS5 checksum is invalid.');
  Download(Url, FileName, Hash);
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  Result := '';
  SkippedComponents := '';
  DLSSDownloaded := False;
  if not WizardIsComponentSelected('reshade') then
    exit;
  if not AcceptLicense.Checked then begin
    Result := 'The ReShade license has not been accepted.';
    exit;
  end;

  DownloadPage.Show;
  try
    try
      PrepareReShade;
    except
      Result := GetExceptionMessage;
      exit;
    end;
    if WizardIsComponentSelected('reshade\dlss5') then begin
      try
        Download('{#DownloadManifestUrl}', 'downloads.ini', '');
        if GetIniString('dlss5', 'enabled', '0', ExpandConstant('{tmp}\downloads.ini')) <> '1' then
          RaiseException('DLSS5 downloads are currently disabled.');
        DownloadDLSSFile('nvngx_dlssnr.dll');
        DownloadDLSSFile('renodx-dlss.addon64');
        DLSSDownloaded := True;
      except
        if DownloadPage.AbortedByUser then begin
          Result := 'The download was cancelled.';
          exit;
        end;
        Log('DLSS5 skipped: ' + GetExceptionMessage);
        SkippedComponents := 'DLSS5 was skipped because its downloads were unavailable or could not be verified. ' +
          'RobloxShadeHost and ReShade were installed.';
      end;
    end;
  finally
    DownloadPage.Hide;
  end;
end;

procedure CurPageChanged(CurPageID: Integer);
begin
  if (CurPageID = wpFinished) and (SkippedComponents <> '') then
    WizardForm.FinishedLabel.Caption := SkippedComponents;
end;
