#define MyAppName "Forge Runtime"
#define MyAppVersion "0.1.0"
#define MyAppPublisher "Varad"
#define MyAppExeName "forge.exe"

[Setup]
AppId={{C7D32C18-5A5D-4D4E-A2E8-123456789ABC}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={autopf}\Forge
DefaultGroupName=Forge
OutputDir=Output
OutputBaseFilename=ForgeSetup
Compression=lzma
SolidCompression=yes
WizardStyle=modern

[Tasks]
Name: "addtopath"; Description: "Add Forge to PATH"; Flags: checkedonce

[Files]
Source: "forge.exe"; DestDir: "{app}\bin"; Flags: ignoreversion
Source: "*.dll"; DestDir: "{app}\bin"; Flags: ignoreversion

[Icons]
Name: "{group}\Forge"; Filename: "{app}\bin\forge.exe"

[Run]
Filename: "{app}\bin\forge.exe"; Description: "Run Forge"; Flags: nowait postinstall skipifsilent

[Registry]
Root: HKCU; \
Subkey: "Environment"; \
ValueType: expandsz; \
ValueName: "Path"; \
ValueData: "{olddata};{app}\bin"; \
Tasks: addtopath