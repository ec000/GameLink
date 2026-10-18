; Per-user GameLink setup. Only the independent MSI prerequisite requests UAC.
#ifndef MyAppVersion
  #define MyAppVersion "0.5.0"
#endif
#ifndef StageDir
  #define StageDir "..\build\stage"
#endif
#ifndef ZeroTierMsi
  #error ZeroTierMsi must point to the validated official MSI
#endif
#ifndef ZeroTierVersion
  #error ZeroTierVersion is required
#endif
#ifndef ZeroTierSHA256
  #error ZeroTierSHA256 is required
#endif
[Setup]
AppId={{CE1ED329-EFCA-4A78-92C6-ACF348558893}
AppName=GameLink
AppVersion={#MyAppVersion}
DefaultDirName={localappdata}\Programs\GameLink
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
MinVersion=10.0.17763
WizardStyle=modern
LicenseFile=..\third_party\zerotier\INSTALLER-LICENSE.txt
OutputDir=..\build\installer
OutputBaseFilename=GameLink-{#MyAppVersion}-Setup
Compression=lzma2
SolidCompression=yes
UninstallDisplayIcon={app}\GameLink.exe
RestartIfNeededByRun=no
UsePreviousTasks=no
[Languages]
Name: "chinesesimp"; MessagesFile: "languages\ChineseSimplified.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"
[Tasks]
Name: "zerotier"; Description: "安装/升级官方 ZeroTier One {#ZeroTierVersion}（服务与虚拟网卡；相同或更新版本自动跳过）"
[Files]
; Keep the prerequisite first for early extraction with solid compression.
Source: "{#ZeroTierMsi}"; DestDir: "{app}\prerequisites"; DestName: "ZeroTier-One.msi"; Flags: ignoreversion
Source: "{#StageDir}\*"; DestDir: "{app}"; Excludes: "prerequisites\ZeroTier-One.msi"; Flags: ignoreversion recursesubdirs createallsubdirs
[Icons]
Name: "{userprograms}\GameLink"; Filename: "{app}\GameLink.exe"
[Run]
Filename: "{app}\GameLink.exe"; Description: "启动 GameLink"; Flags: nowait postinstall skipifsilent
#include "zerotier-prerequisite.iss"
