; Inno Setup 6. Manual opt-in installation; does not bundle or reconfigure ZeroTier.
#define MyAppVersion "0.1.0"
#ifndef StageDir
  #define StageDir "..\build\stage"
#endif
[Setup]
AppId={{CE1ED329-EFCA-4A78-92C6-ACF348558893}
AppName=GameLink
AppVersion={#MyAppVersion}
DefaultDirName={localappdata}\Programs\GameLink
PrivilegesRequired=lowest
OutputDir=..\build\installer
OutputBaseFilename=GameLink-0.1.0-Setup
Compression=lzma2
SolidCompression=yes
UninstallDisplayIcon={app}\GameLink.exe
[Files]
Source: "{#StageDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
[Icons]
Name: "{userprograms}\GameLink"; Filename: "{app}\GameLink.exe"
[Run]
Filename: "{app}\GameLink.exe"; Description: "Launch GameLink"; Flags: nowait postinstall skipifsilent
