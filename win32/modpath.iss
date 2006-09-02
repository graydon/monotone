// ----------------------------------------------------------------------------
//
// Inno Setup Ver:  5.1.5
// Script Version:  1.2.2
// Author:          Jared Breland <jbreland@legroom.net>
// Homepage:		http://www.legroom.net/mysoft
//
// Script Function:
//	Enable modification of system path directly from Inno Setup packages
//
// Instructions:
//	Copy modpath.iss to the same directory as your setup script
//
//	Add this statement to your [Setup] section:
//		ChangesEnvironment=true
//
//	Add the following [Task] (you can change the Description, but the Name must be modifypath)
//		Name: modifypath; Description: &Add application directory to your system path; Flags: unchecked
//
//	Add the following to the end of your [Code] section
//	Result should be set to the path that you want to add
//		function ModPathDir(): String;
//		begin
//			Result := ExpandConstant('{app}');
//		end;
//		#include "modpath.iss"
//
// ----------------------------------------------------------------------------

procedure ModPath();
var
	oldpath:	String;
	newpath:	String;
	pathArr:	TArrayOfString;
	aExecFile:	String;
	aExecArr:	TArrayOfString;
	i:			Integer;
	pathdir:	String;
begin
	pathdir := ModPathDir();
	// Modify WinNT path
	if UsingWinNT() = true then begin

		// Get current path, split into an array
		RegQueryStringValue(HKEY_LOCAL_MACHINE, 'SYSTEM\CurrentControlSet\Control\Session Manager\Environment', 'Path', oldpath);
		oldpath := oldpath + ';';
		i := 0;
		while (Pos(';', oldpath) > 0) do begin
			SetArrayLength(pathArr, i+1);
			pathArr[i] := Copy(oldpath, 0, Pos(';', oldpath)-1);
			oldpath := Copy(oldpath, Pos(';', oldpath)+1, Length(oldpath));
			i := i + 1;

			// Check if current directory matches app dir
			if pathdir = pathArr[i-1] then begin
				// if uninstalling, remove dir from path
				if IsUninstaller() = true then begin
					continue;
				// if installing, abort because dir was already in path
				end else begin
					abort;
				end;
			end;

			// Add current directory to new path
			if i = 1 then begin
				newpath := pathArr[i-1];
			end else begin
				newpath := newpath + ';' + pathArr[i-1];
			end;
		end;

		// Append app dir to path if not already included
		if IsUninstaller() = false then
			newpath := newpath + ';' + pathdir;

		// Write new path
		RegWriteStringValue(HKEY_LOCAL_MACHINE, 'SYSTEM\CurrentControlSet\Control\Session Manager\Environment', 'Path', newpath);

	// Modify Win9x path
	end else begin

		// Convert to shortened dirname
		pathdir := GetShortName(pathdir);

		// If autoexec.bat exists, check if app dir already exists in path
		aExecFile := 'C:\AUTOEXEC.BAT';
		if FileExists(aExecFile) then begin
			LoadStringsFromFile(aExecFile, aExecArr);
			for i := 0 to GetArrayLength(aExecArr)-1 do begin
				if IsUninstaller() = false then begin
					// If app dir already exists while installing, abort add
					if (Pos(pathdir, aExecArr[i]) > 0) then
						abort;
				end else begin
					// If app dir exists and = what we originally set, then delete at uninstall
					if aExecArr[i] = 'SET PATH=%PATH%;' + pathdir then
						aExecArr[i] := '';
				end;
			end;
		end;

		// If app dir not found, or autoexec.bat didn't exist, then (create and) append to current path
		if IsUninstaller() = false then begin
			SaveStringToFile(aExecFile, #13#10 + 'SET PATH=%PATH%;' + pathdir, True);

		// If uninstalling, write the full autoexec out
		end else begin
			SaveStringsToFile(aExecFile, aExecArr, False);
		end;
	end;

	// Write file to flag modifypath was selected
	//   Workaround since IsTaskSelected() cannot be called at uninstall and AppName and AppId cannot be "read" in Code section
	if IsUninstaller() = false then
		SaveStringToFile(ExpandConstant('{app}') + '\uninsTasks.txt', WizardSelectedTasks(False), False);
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
	if CurStep = ssPostInstall then
		if IsTaskSelected('modifypath') then
			ModPath();
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
	appdir:			String;
	selectedTasks:	String;
begin
	appdir := ExpandConstant('{app}')
	if CurUninstallStep = usUninstall then begin
		if LoadStringFromFile(appdir + '\uninsTasks.txt', selectedTasks) then
			if Pos('modifypath', selectedTasks) > 0 then
				ModPath();
		DeleteFile(appdir + '\uninsTasks.txt')
	end;
end;
