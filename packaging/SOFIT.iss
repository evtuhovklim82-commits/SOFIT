#ifndef AppVersion
  #error AppVersion must match the CMake project version
#endif

[Setup]
AppId=SOFIT.PatternEditor
AppName=SOFIT
AppVersion={#AppVersion}
DefaultDirName={localappdata}\Programs\SOFIT
DefaultGroupName=SOFIT
UninstallDisplayIcon={app}\SOFIT.exe
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
ChangesAssociations=yes
WizardStyle=modern
OutputDir=..\dist
OutputBaseFilename=SOFIT-Windows-x64-Setup

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; Flags: unchecked
Name: "associate"; Description: "Register SOFIT for .sofit projects"; Flags: checkedonce

[Files]
Source: "..\dist\SOFIT\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{userprograms}\SOFIT"; Filename: "{app}\SOFIT.exe"; WorkingDir: "{app}"
Name: "{userdesktop}\SOFIT"; Filename: "{app}\SOFIT.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[Registry]
Root: HKCU; Subkey: "Software\Classes\.sofit\OpenWithProgids"; ValueType: none; ValueName: "SOFIT.Pattern"; Flags: uninsdeletevalue; Tasks: associate
Root: HKCU; Subkey: "Software\Classes\SOFIT.Pattern"; ValueType: string; ValueData: "SOFIT pattern"; Flags: uninsdeletekey; Tasks: associate
Root: HKCU; Subkey: "Software\Classes\SOFIT.Pattern\DefaultIcon"; ValueType: string; ValueData: "{app}\SOFIT.exe,0"; Tasks: associate
Root: HKCU; Subkey: "Software\Classes\SOFIT.Pattern\shell\open\command"; ValueType: string; ValueData: """{app}\SOFIT.exe"" ""%1"""; Tasks: associate
Root: HKCU; Subkey: "Software\SOFIT\Capabilities"; ValueType: string; ValueName: "ApplicationName"; ValueData: "SOFIT"; Flags: uninsdeletekey; Tasks: associate
Root: HKCU; Subkey: "Software\SOFIT\Capabilities\FileAssociations"; ValueType: string; ValueName: ".sofit"; ValueData: "SOFIT.Pattern"; Tasks: associate
Root: HKCU; Subkey: "Software\RegisteredApplications"; ValueType: string; ValueName: "SOFIT"; ValueData: "Software\SOFIT\Capabilities"; Flags: uninsdeletevalue; Tasks: associate
