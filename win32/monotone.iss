#define APPNAME="monotone"
#define APPVERS="0.42"

[Setup]
AppName={#APPNAME}
AppVersion={#APPVERS}
AppVerName={#APPNAME} {#APPVERS}
OutputBaseFileName={#APPNAME}-{#APPVERS}-setup
AppCopyright=Copyright © 2002-2008 Graydon Hoare et al.
AppPublisher=The monotone Development Team
AppPublisherURL=http://www.monotone.ca/
DefaultDirName={pf}\{#APPNAME}
DefaultGroupName={#APPNAME}
MinVersion=4.0,4.0
OutputDir=.
AllowNoIcons=1
Compression=lzma/ultra
SolidCompression=yes
LicenseFile="..\COPYING"
ChangesEnvironment=true
WizardImageFile=monotone.bmp

[Files]
Source: "..\mtn.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\monotone.html"; DestDir: "{app}\documentation"; Flags: ignoreversion
Source: "..\figures\*.png"; DestDir: "{app}\documentation\figures"; Flags: ignoreversion
Source: "..\COPYING"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\po\de.gmo"; DestDir: "{app}\locale\de\LC_MESSAGES"; DestName: "monotone.mo"; Flags: ignoreversion
Source: "..\po\es.gmo"; DestDir: "{app}\locale\es\LC_MESSAGES"; DestName: "monotone.mo"; Flags: ignoreversion
Source: "..\po\it.gmo"; DestDir: "{app}\locale\it\LC_MESSAGES"; DestName: "monotone.mo"; Flags: ignoreversion
Source: "..\po\sv.gmo"; DestDir: "{app}\locale\sv\LC_MESSAGES"; DestName: "monotone.mo"; Flags: ignoreversion
Source: "\mingw\bin\libiconv-2.dll"; DestDir: "{app}"
Source: "\mingw\bin\libintl-8.dll"; DestDir: "{app}"
Source: "\mingw\bin\zlib1.dll"; DestDir: "{app}"

[Tasks]
Name: modifypath; Description: "Add monotone to your path"; GroupDescription: "Get up and running"; Flags: unchecked
Name: viewdocs; Description: "View the monotone documentation"; GroupDescription: "Get up and running"

[Run]
Filename: "{app}\documentation\monotone.html"; Tasks: viewdocs; Flags: shellexec nowait; WorkingDir: "{app}\documentation"

[Icons]
Name: "{group}\monotone documentation"; Filename: "{app}\documentation\monotone.html"; WorkingDir: "{app}"

[Code]
function ModPathDir(): String;
begin
  Result := ExpandConstant('{app}');
end;

#include "modpath.iss"

