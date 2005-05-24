[Setup]
AppName=monotone
AppVerName=monotone 0.19
AppCopyright=Copyright © 2002-2005 Graydon Hoare et al.
DefaultDirName={pf}\monotone
DefaultGroupName=monotone
MinVersion=0,5.0
OutputDir=.
OutputBaseFileName=monotone-setup
AllowNoIcons=1
AppPublisher=venge.net
AppPublisherURL=http://venge.net/monotone
AppVersion=0.19
Compression=lzma/ultra
SolidCompression=yes
LicenseFile="..\COPYING"

[Files]
Source: "..\monotone.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\html\*.*"; DestDir: "{app}\documentation"; Flags: ignoreversion
Source: "..\COPYING"; DestDir: "{app}"; Flags: ignoreversion
Source: "\mingw\bin\libiconv-2.dll"; DestDir: "{app}"
Source: "..\figures\*.png"; DestDir: "{app}\documentation\figures"; Flags: ignoreversion

[Tasks]
Name: initdb; Description: "Initialise a new database"; GroupDescription: "Get up and running"; Check: DBDoesntExist
Name: initdb\genkey; Description: "Generate a key for use with monotone"; GroupDescription: "Get up and running"; Check: DBDoesntExist
Name: viewdocs; Description: "View the monotone documentation"; GroupDescription: "Get up and running"

[Run]
Filename: "{app}\monotone.exe"; Tasks: initdb; StatusMsg: "Initialising database..."; WorkingDir: "{app}"; Parameters: "--db=""{code:ForceDBPath|monotone.db}"" db init"
Filename: "{app}\monotone.exe"; Tasks: initdb\genkey; StatusMsg: "Generating key..."; Flags: hidewizard; WorkingDir: "{app}"; Parameters: "--db=""{code:ForceDBPath|monotone.db}"" genkey {code:GetKeyID}"
Filename: "{app}\documentation\index.html"; Tasks: viewdocs; Flags: shellexec nowait; WorkingDir: "{app}\documentation"

[Icons]
Name: "{group}\monotone documentation"; Filename: "{app}\documentation\index.html"; WorkingDir: "{app}"

[Code]
program Setup;

var
  DBChecked : Boolean;
  DBDidntExist : Boolean;
  KeyID : String;

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
  Result := KeyID;
end;

function DBDoesntExist() : Boolean;
var
  path: String;
begin
  if (DBChecked) then begin
    Result := DBDidntExist;
    exit;
  end;
  path := GetDBPath('monotone.db');
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

function ScriptDlgPages(CurPage: Integer; BackClicked: Boolean): Boolean;
var
  Next: Boolean;
begin
  if (ShouldProcessEntry('','initdb\genkey') = srYes) and ((not BackClicked and (CurPage = wpSelectTasks)) or (BackClicked and (CurPage = wpReady))) then begin
    { Insert a custom wizard page between two non custom pages }
    { First open the custom wizard page }
    ScriptDlgPageOpen();
    { Set some captions }
    ScriptDlgPageSetCaption('Key ID for use with monotone');
    ScriptDlgPageSetSubCaption1('Which e-mail address should your key be generated with?');
    ScriptDlgPageSetSubCaption2('Enter your chosen key ID (this is usually an e-mail address, sometimes a special one for monotone work), then click Next.');
    Next := InputQuery('Key ID / e-mail address:', KeyID);
    while Next and (KeyID='') do begin
      MsgBox('In order to generate a key, you must enter a Key ID', mbError, MB_OK);
      Next := InputQuery('Key ID / e-mail address:', KeyID);
    end;
    { See NextButtonClick and BackButtonClick: return True if the click should be allowed }
    if not BackClicked then
      Result := Next
    else
      Result := not Next;
    { Close the wizard page. Do a FullRestore only if the click (see above) is not allowed }
    ScriptDlgPageClose(not Result);
  end else begin
    Result := true;
  end;
end;

function NextButtonClick(CurPage: Integer): Boolean;
begin
  Result := ScriptDlgPages(CurPage, False);
end;

function BackButtonClick(CurPage: Integer): Boolean;
begin
  Result := ScriptDlgPages(CurPage, True);
end;

