#ifndef Stage
  #error Stage is required
#endif
#ifndef Version
  #error Version is required
#endif
[Setup]
AppId={{E714BF50-8F04-4EC5-94DC-2C49736C46A7}
AppName=Dishonored 2 VR (Indurhra)
AppVersion={#Version}
AppPublisher=Indurhra
AppPublisherURL=https://github.com/Indurhra/dishonored-2-vr
DefaultDirName={localappdata}\Programs\DishonoredVR
DefaultGroupName=Dishonored 2 VR
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
WizardStyle=modern
DisableProgramGroupPage=yes
DisableDirPage=yes
LicenseFile={#Stage}\LICENSE.txt
OutputDir={#Stage}\..\output
OutputBaseFilename=DishonoredVR-Setup-{#Version}
Compression=lzma2
SolidCompression=yes
UninstallDisplayIcon={app}\DishonoredVR-Manager.exe
CloseApplications=yes
CloseApplicationsFilter=DishonoredVR-Manager.exe
SetupLogging=yes

[Files]
Source: "{#Stage}\DishonoredVR-Manager.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Stage}\payload.zip"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Stage}\DishonoredVR-Manager.exe"; Flags: dontcopy

[Tasks]
Name: "autoupdate"; Description: "Automatically install stable mod updates when using the Play Dishonored 2 VR shortcut"; Flags: checkedonce
Name: "desktopicon"; Description: "Create a Play Dishonored 2 VR desktop shortcut"; Flags: unchecked

[Icons]
Name: "{group}\Play Dishonored 2 VR"; Filename: "{app}\DishonoredVR-Manager.exe"; Parameters: "--play"
Name: "{group}\Manage Dishonored 2 VR"; Filename: "{app}\DishonoredVR-Manager.exe"
Name: "{group}\Uninstall Dishonored 2 VR"; Filename: "{uninstallexe}"
Name: "{autodesktop}\Play Dishonored 2 VR"; Filename: "{app}\DishonoredVR-Manager.exe"; Parameters: "--play"; Tasks: desktopicon

[Code]
var
  GamePage: TInputDirWizardPage;
  HeadsetPage: TInputOptionWizardPage;
  LibraryPage: TInputOptionWizardPage;
  Libraries: TArrayOfString;
  Manager: String;
  ToolkitLabel: TNewStaticText;
  ToolkitButton: TNewButton;

procedure ToolkitClick(Sender: TObject);
var Code: Integer;
begin
  ShellExec('open','https://github.com/BnuuySolutions/PSVR2Toolkit','','',SW_SHOWNORMAL,ewNoWait,Code);
end;

function Q(Value: String): String;
begin
  Result := '"' + Value + '"';
end;

procedure InitializeWizard;
var Code, I: Integer; Initial, FileName, Choice: String; ToolkitText: AnsiString;
begin
  ExtractTemporaryFile('DishonoredVR-Manager.exe');
  Manager := ExpandConstant('{tmp}\DishonoredVR-Manager.exe');
  FileName := ExpandConstant('{tmp}\games.txt');
  if Exec(Manager, '--discover '+Q(FileName), '', SW_HIDE, ewWaitUntilTerminated, Code) and (Code=0) then
    LoadStringsFromFile(FileName, Libraries);
  LibraryPage := CreateInputOptionPage(wpLicense, 'Choose your game', 'Multiple Steam installations were found.', 'Select the installation to use.', True, False);
  for I:=0 to GetArrayLength(Libraries)-1 do LibraryPage.Add(Libraries[I]);
  if GetArrayLength(Libraries)>0 then LibraryPage.SelectedValueIndex:=0;
  GamePage := CreateInputDirPage(LibraryPage.ID, 'Dishonored 2 location', 'Your Steam game folder', 'The installer checks the supported game build. Existing settings and saves are preserved.', False, '');
  GamePage.Add('Game folder:');
  Initial := ExpandConstant('{param:GAMEDIR|}');
  if Initial='' then Initial:=GetPreviousData('GameDir','');
  if (Initial='') and (GetArrayLength(Libraries)=1) then Initial:=Libraries[0];
  GamePage.Values[0]:=Initial;
  HeadsetPage:=CreateInputOptionPage(GamePage.ID, 'Your headset', 'Choose your setup', 'PSVR2 support requires PSVR2 Toolkit to be installed separately. The helper is bundled; no system runtime or SteamVR settings will be changed. Existing INI settings always take priority.',True,False);
  HeadsetPage.Add('PSVR2 through SteamVR - adaptive triggers');
  HeadsetPage.Add('Other headset - SteamVR, VDXR or another OpenXR runtime');
  HeadsetPage.Add('Not sure - detect the headset at game startup');
  Choice:=GetPreviousData('Headset','2');
  HeadsetPage.SelectedValueIndex:=StrToIntDef(Choice,2);
  ToolkitLabel:=TNewStaticText.Create(WizardForm);ToolkitLabel.Parent:=HeadsetPage.Surface;
  ToolkitLabel.SetBounds(0,ScaleY(130),HeadsetPage.SurfaceWidth,ScaleY(48));ToolkitLabel.AutoSize:=False;ToolkitLabel.WordWrap:=True;
  FileName:=ExpandConstant('{tmp}\toolkit.txt');
  if Exec(Manager,'--toolkit '+Q(FileName),'',SW_HIDE,ewWaitUntilTerminated,Code) and (Code=0) and LoadStringFromFile(FileName,ToolkitText) then ToolkitLabel.Caption:=String(ToolkitText);
  ToolkitButton:=TNewButton.Create(WizardForm);ToolkitButton.Parent:=HeadsetPage.Surface;ToolkitButton.SetBounds(0,ScaleY(184),ScaleX(180),ScaleY(25));ToolkitButton.Caption:='PSVR2 Toolkit setup';ToolkitButton.OnClick:=@ToolkitClick;
end;

function ShouldSkipPage(PageID: Integer): Boolean;
begin
  Result:=(PageID=LibraryPage.ID) and (GetArrayLength(Libraries)<2);
end;

function NextButtonClick(CurPageID: Integer): Boolean;
var Code: Integer;
begin
  Result:=True;
  if (CurPageID=LibraryPage.ID) and (LibraryPage.SelectedValueIndex>=0) then GamePage.Values[0]:=Libraries[LibraryPage.SelectedValueIndex];
  if CurPageID=GamePage.ID then Result:=Exec(Manager,'--check '+Q(GamePage.Values[0]),'',SW_HIDE,ewWaitUntilTerminated,Code) and (Code=0);
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
var Code: Integer;
begin
  Result:='';
  if not Exec(Manager,'--check '+Q(GamePage.Values[0]),'',SW_HIDE,ewWaitUntilTerminated,Code) or (Code<>0) then
    Result:='Close the game and select a supported Steam Dishonored 2 installation.';
end;

procedure CurStepChanged(CurStep: TSetupStep);
var Code: Integer; Headset, Auto: String;
begin
  if CurStep=ssPostInstall then begin
    Headset:='auto';if HeadsetPage.SelectedValueIndex=0 then Headset:='psvr2';if HeadsetPage.SelectedValueIndex=1 then Headset:='other';
    Auto:='0';if WizardIsTaskSelected('autoupdate') then Auto:='1';
    if not Exec(ExpandConstant('{app}\DishonoredVR-Manager.exe'),'--install '+Q(GamePage.Values[0])+' '+Headset+' '+Auto,'',SW_HIDE,ewWaitUntilTerminated,Code) or (Code<>0) then
      RaiseException('The mod could not be installed. Its transaction was rolled back. Reopen setup after resolving the reported problem.');
  end;
end;

procedure RegisterPreviousData(PreviousDataKey: Integer);
begin
  SetPreviousData(PreviousDataKey,'GameDir',GamePage.Values[0]);
  SetPreviousData(PreviousDataKey,'Headset',IntToStr(HeadsetPage.SelectedValueIndex));
end;

function InitializeUninstall: Boolean;
begin
  Result:=True;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var Code: Integer;
begin
  if CurUninstallStep=usUninstall then
    if not Exec(ExpandConstant('{app}\DishonoredVR-Manager.exe'),'--uninstall','',SW_HIDE,ewWaitUntilTerminated,Code) or (Code<>0) then
      RaiseException('The mod could not be removed safely. Close the game or resolve the modified-file warning and try again.');
end;
