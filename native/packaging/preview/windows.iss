; Stable identity belongs only to the C++ client, never Electron.
#ifndef PayloadDir
  #error PayloadDir is required
#endif
#ifndef RelayVersion
  #error RelayVersion is required
#endif
#ifndef OutputDir
  #error OutputDir is required
#endif
[Setup]
AppId=dev.relay.native.preview
AppName=Relay Native
AppVersion={#RelayVersion}
AppPublisher=Relay
AppPublisherURL=https://github.com/ka-capek/relay-desktop
DefaultDirName={localappdata}\Programs\Relay Native
UsePreviousAppDir=yes
DisableDirPage=no
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64os
ArchitecturesInstallIn64BitMode=x64os
OutputDir={#OutputDir}
OutputBaseFilename=Relay-Native-Setup-{#RelayVersion}-x64
SetupIconFile=..\..\..\build\icon.ico
UninstallDisplayIcon={app}\Relay.exe
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
CloseApplications=no
RestartApplications=no
SetupMutex=RelayNativePreviewSetup
LicenseFile=..\..\..\LICENSE

[Files]
Source: "{#PayloadDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[INI]
Filename: "{app}\relay-native-install.ini"; Section: "RelayNative"; Key: "Product"; String: "dev.relay.native.preview"
Filename: "{app}\relay-native-install.ini"; Section: "RelayNative"; Key: "Version"; String: "{#RelayVersion}"

[UninstallDelete]
Type: files; Name: "{app}\relay-native-install.ini"

[Icons]
Name: "{userprograms}\Relay Native"; Filename: "{app}\Relay.exe"; WorkingDir: "{app}"
Name: "{userdesktop}\Relay Native"; Filename: "{app}\Relay.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Shortcuts:"; Flags: unchecked

[Code]
function GetFileAttributesW(Name: String): LongWord;
  external 'GetFileAttributesW@kernel32.dll stdcall';
function CreateFileW(Name: String; Access, Share: LongWord; Security: Integer;
  Creation, Flags: LongWord; Template: Integer): THandle;
  external 'CreateFileW@kernel32.dll stdcall';
function CloseHandle(Handle: THandle): Boolean;
  external 'CloseHandle@kernel32.dll stdcall';

function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  Dir, Parent, Exe: String;
  Legacy: AnsiString;
  Entry: TFindRec;
  Known, HasFiles: Boolean;
  Attributes: LongWord;
  Handle: THandle;
begin
  Result := '';
  Dir := ExpandConstant('{app}');
  if Copy(Dir, 2, 2) <> ':\' then begin
    Result := 'Choose a folder on a local drive.';
    exit;
  end;
  Parent := Dir;
  while Length(Parent) > 3 do begin
    Attributes := GetFileAttributesW(Parent);
    if (Attributes <> $FFFFFFFF) and ((Attributes and $400) <> 0) then begin
      Result := 'Choose an installation folder without symbolic links or junctions.';
      exit;
    end;
    Parent := ExtractFileDir(Parent);
  end;
  Exe := Dir + '\Relay.exe';
  Known := GetIniString('RelayNative', 'Product', '', Dir + '\relay-native-install.ini') = 'dev.relay.native.preview';
  if not Known and LoadStringFromFile(Dir + '\installation.json', Legacy) then
    Known := (Pos('"platform": "windows-x64"', Legacy) > 0) and
             (Pos('"qt": "6.11.1"', Legacy) > 0);
  Known := Known and FileExists(Exe) and not FileExists(Dir + '\resources\app.asar');
  HasFiles := False;
  if FindFirst(Dir + '\*', Entry) then begin
    try
      repeat
        if (Entry.Name <> '.') and (Entry.Name <> '..') then HasFiles := True;
      until not FindNext(Entry);
    finally
      FindClose(Entry);
    end;
  end;
  if HasFiles and not Known then begin
    Result := 'This folder is not a recognized Relay Native installation. Choose an empty folder. Electron installations are not upgraded.';
    exit;
  end;
  if FileExists(Exe) then begin
    { Open for write without changing bytes. A loaded executable cannot be replaced. }
    Handle := CreateFileW(Exe, $40000000, 0, 0, 3, 0, 0);
    if Handle = THandle(-1) then begin
      Result := 'Quit Relay Native before installing an update, and check that its folder is writable.';
      exit;
    end;
    CloseHandle(Handle);
  end;
end;
