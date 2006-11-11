[Setup]
AppName=monotone
AppVersion=0.31
AppVerName=monotone 0.31
OutputBaseFileName=monotone-0.31-setup
AppCopyright=Copyright © 2002-2006 Graydon Hoare et al.
AppPublisher=venge.net
AppPublisherURL=http://venge.net/monotone
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
  DBChecked: Boolean;
  DBDidntExist: Boolean;
  KeyIDPage: TInputQueryWizardPage;
  BranchWorkspacePage: TInputQueryWizardPage;

function InitializeSetup(): Boolean;
begin
  DBChecked := False;
  Result := True;
end;

function GetDBPath(db: String): String;
var path: String;
begin
  path := GetShellFolder(False, sfDocs);
  path := path + '\monotone\' + db;
  Result := path;
end;

function DBDoesntExist(): Boolean;
var path: String;
begin
  if (DBChecked = True) then
    Result := DBDidntExist
  else begin
    path := GetDBPath('monotone.mtn');
    DBDidntExist := not FileOrDirExists(path);
    DBChecked := True;
    Result := DBDidntExist;
  end;
end;

function ForceDBPath(db: String): String;
var path: String;
begin
  path := GetDBPath('');
  ForceDirectories(path);
  Result := path + db;
end;

function GetKeyID(Default: String): String;
begin
  Result := KeyIDPage.Values[0];
end;

function GetBranchName(Default: String): String;
begin
  Result := BranchWorkspacePage.Values[0];
end;

function GetWorkspacePath(Default: String): String;
begin
  Result := BranchWorkspacePage.Values[1];
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
    Result := False;
end;

function NextButtonClick(CurPageID: Integer): Boolean;
begin
  if CurPageID = KeyIDPage.ID then begin
    if KeyIDPage.Values[0] = '' then begin
      MsgBox('You must enter a valid key ID', mbError, MB_OK);
      Result := False;
    end else
      Result := True;
  end else if CurPageId = BranchWorkspacePage.ID then begin
    if BranchWorkspacePage.Values[0] = '' then begin
      MsgBox('You must enter a valid branch name', mbError, MB_OK);
      Result := False;
    end else if BranchWorkspacePage.Values[1] = '' then begin
      MsgBox('You must enter a valid workspace path', mbError, MB_OK);
      Result := False;
    end else
      Result := True;
  end else
    Result := True;
end;

function ModPathDir(): String;
begin
  Result := ExpandConstant('{app}');
end;

#include "modpath.iss"

