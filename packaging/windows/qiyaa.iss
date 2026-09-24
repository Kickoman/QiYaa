; Inno Setup script for the Windows installer.
; Build: iscc /DAppVersion=0.2.0 /DSourceDir=dist\QiYaa /DOutputDir=dist packaging\windows\qiyaa.iss
; SourceDir is the folder prepared by windeployqt (QiYaa.exe, Qt DLLs, plugins, MSVC runtime).

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef SourceDir
  #define SourceDir "..\..\dist\QiYaa"
#endif
#ifndef OutputDir
  #define OutputDir "..\..\dist"
#endif

[Setup]
AppId={{6B2D4C3E-8F1A-4E7B-9C55-2A1F0D7E4B91}
AppName=QiYaa
AppVersion={#AppVersion}
AppVerName=QiYaa {#AppVersion}
AppPublisher=QiYaa contributors
AppPublisherURL=https://github.com/Kickoman/QiYaa
AppSupportURL=https://github.com/Kickoman/QiYaa/issues
DefaultDirName={autopf}\QiYaa
DefaultGroupName=QiYaa
DisableProgramGroupPage=yes
; Per-user by default (no admin prompt); the user may choose all users.
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir={#OutputDir}
OutputBaseFilename=QiYaa-{#AppVersion}-windows-x64-setup
SetupIconFile=..\..\resources\icons\qiyaa.ico
UninstallDisplayIcon={app}\QiYaa.exe
LicenseFile=..\..\LICENSE
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern

[Languages]
Name: "ru"; MessagesFile: "compiler:Languages\Russian.isl"
Name: "en"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\QiYaa"; Filename: "{app}\QiYaa.exe"
Name: "{autodesktop}\QiYaa"; Filename: "{app}\QiYaa.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\QiYaa.exe"; Description: "{cm:LaunchProgram,QiYaa}"; Flags: nowait postinstall skipifsilent
