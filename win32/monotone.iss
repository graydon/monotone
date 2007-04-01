[Setup]
AppName=monotone
AppVersion=0.34
AppVerName=monotone 0.34
OutputBaseFileName=monotone-0.34-setup
AppCopyright=Copyright © 2002-2007 Graydon Hoare et al.
AppPublisher=venge.net
AppPublisherURL=http://www.monotone.ca/
DefaultDirName={pf}\monotone
DefaultGroupName=monotone
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
Source: "..\html\*.*"; DestDir: "{app}\documentation"; Flags: ignoreversion
Source: "..\COPYING"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\figures\*.png"; DestDir: "{app}\documentation\figures"; Flags: ignoreversion
Source: "\mingw\bin\libiconv-2.dll"; DestDir: "{app}"
Source: "\mingw\bin\zlib1.dll"; DestDir: "{app}"

[Tasks]
Name: modifypath; Description: "Add monotone to your path"; GroupDescription: "Get up and running"; Flags: unchecked
Name: viewdocs; Description: "View the monotone documentation"; GroupDescription: "Get up and running"

[Run]
Filename: "{app}\documentation\index.html"; Tasks: viewdocs; Flags: shellexec nowait; WorkingDir: "{app}\documentation"

[Icons]
Name: "{group}\monotone documentation"; Filename: "{app}\documentation\index.html"; WorkingDir: "{app}"

[Code]
function ModPathDir(): String;
begin
  Result := ExpandConstant('{app}');
end;

#include "modpath.iss"

