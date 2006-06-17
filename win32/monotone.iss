[Setup]
AppName=monotone
AppVersion=0.27
AppVerName=monotone 0.27
OutputBaseFileName=monotone-0.27-setup
AppCopyright=Copyright © 2002-2006 Graydon Hoare et al.
AppPublisher=venge.net
AppPublisherURL=http://venge.net/monotone
DefaultDirName={pf}\monotone
DefaultGroupName=monotone
MinVersion=4.0,4.0
OutputDir=.
AllowNoIcons=1
Compression=lzma/fast
SolidCompression=yes
LicenseFile="..\COPYING"
ChangesEnvironment=true

[Files]
Source: "..\..\monotone.release\mtn.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\..\monotone.release\html\*.*"; DestDir: "{app}\documentation"; Flags: ignoreversion
Source: "..\COPYING"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\..\monotone.release\figures\*.png"; DestDir: "{app}\documentation\figures"; Flags: ignoreversion
Source: "\mingw\bin\libiconv-2.dll"; DestDir: "{app}"
Source: "\mingw\bin\zlib1.dll"; DestDir: "{app}"

[Tasks]
Name: initdb; Description: "Initialise a new database"; GroupDescription: "Get up and running"; Check: DBDoesntExist
Name: initdb\genkey; Description: "Generate a key for use with monotone"; GroupDescription: "Get up and running"; Check: DBDoesntExist
Name: initdb\createws; Description: "Create an initial workspace"; GroupDescription: "Get up and running"; Check: DBDoesntExist
Name: modifypath; Description: "Add monotone to your path"; GroupDescription: "Get up and running"; Flags: unchecked
Name: viewdocs; Description: "View the monotone documentation"; GroupDescription: "Get up and running"

[Run]
Filename: "{app}\mtn.exe"; Tasks: initdb; StatusMsg: "Initialising database..."; WorkingDir: "{app}"; Parameters: "--db=""{code:ForceDBPath|monotone.mtn}"" db init"
Filename: "{app}\mtn.exe"; Tasks: initdb\genkey; StatusMsg: "Generating key..."; Flags: hidewizard; WorkingDir: "{app}"; Parameters: "--db=""{code:ForceDBPath|monotone.mtn}"" genkey {code:GetKeyID}"
Filename: "{app}\mtn.exe"; Tasks: initdb\createws; StatusMsg: "Creating inital workspace..."; WorkingDir: "{app}"; Parameters: "--db=""{code:ForceDBPath|monotone.mtn}"" --branch=""{code:GetBranchName}"" setup ""{code:GetWorkspacePath}"""
Filename: "{app}\documentation\index.html"; Tasks: viewdocs; Flags: shellexec nowait; WorkingDir: "{app}\documentation"

[Icons]
Name: "{group}\monotone documentation"; Filename: "{app}\documentation\index.html"; WorkingDir: "{app}"

[Code]
program Setup;

var
  DBChecked : Boolean;
  DBDidntExist : Boolean;
  KeyIDPage : TInputQueryWizardPage;
  BranchWorkspacePage : TInputQueryWizardPage;

function InitializeSetup(): Boolean;
begin
  DBChecked := false;
  Result := true;
end;

function GetDBPath(db: String) : String;
var
  path: String;
begin
  path := GetShellFolder(false, sfDocs);
  path := path + '\monotone\' + db;
  Result := path;
end;

function GetKeyID(Default: String) : String;
begin
  Result := KeyIDPage.Values[0];
end;

function GetBranchName(Default: String) : String;
begin
  Result := BranchWorkspacePage.Values[0];
end;

function GetWorkspacePath(Default: String) : String;
begin
  Result := BranchWorkspacePage.Values[1];
end;

function DBDoesntExist() : Boolean;
var
  path: String;
begin
  if (DBChecked) then begin
    Result := DBDidntExist;
    exit;
  end;
  path := GetDBPath('monotone.mtn');
  DBDidntExist := not FileOrDirExists(path);
  DBChecked := true;
  Result := DBDidntExist;
end;

function ForceDBPath(db: String) : String;
var path: String;
begin
  path := GetDBPath('');
  ForceDirectories(path);
  Result := path + db;
end;

procedure InitializeWizard;
begin
  KeyIDPage := CreateInputQueryPage(wpSelectTasks, 'Key ID', 'Key ID for use with monotone',
                                    'Which email address should your key be generated with?');
  KeyIDPage.Add('Key ID:', False);

  BranchWorkspacePage := CreateInputQueryPage(KeyIDPage.ID, 'Workspace initialisation',
                                              'Workspace path and branch name',
                                              'What branch name and workspace path would you'
                                              + ' like to use for your initial workspace?');
  BranchWorkspacePage.Add('Branch name:', False);
  BranchWorkspacePage.Add('Workspace path:', False);
end;

function ShouldSkipPage(PageID: Integer): Boolean;
begin
  if (IsTaskSelected('initdb\genkey') = False) and (PageID = KeyIDPage.ID) then
    Result := True
  else if (IsTaskSelected('initdb\createws') = False) and (PageID = BranchWorkspacePage.ID) then
    Result := True
  else
    Result := False
end;

function ModPathDir(): String;
begin
  Result := ExpandConstant('{app}');
end;

#include "modpath.iss"

