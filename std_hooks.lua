
-- this is the standard set of lua hooks for monotone;
-- user-provided files can override it or add to it.

function ignore_file(name)
	if (strfind(name, "%.o$")) then return 1 end
	if (strfind(name, "%.aux$")) then return 1 end
	if (strfind(name, "%.bak$")) then return 1 end
	if (strfind(name, "%.orig$")) then return 1 end
	if (strfind(name, "%.rej$")) then return 1 end
	if (strfind(name, "/core$")) then return 1 end
	if (strfind(name, "^CVS/")) then return 1 end
	if (strfind(name, "^SVN/")) then return 1 end
	if (strfind(name, "/CVS/")) then return 1 end
	if (strfind(name, "/SVN/")) then return 1 end
	return nil;
end


function edit_comment(basetext)
        local exe = "vi"
        local visual = getenv("VISUAL")
        if (visual ~= nil) then exe = visual end
        local editor = getenv("EDITOR")
        if (editor ~= nil) then exe = editor end

        tname = tmpname()
        local tmp = openfile(tname, "w+")
        if (tmp == nil) then return nil end
	basetext = "MT: " .. gsub(basetext, "\n", "\nMT: ")
        write(tmp, basetext)
        closefile(tmp)

        if (execute(format("%s %s", exe, tname)) ~= 0) then
                remove(tname)
                return nil
        end

        tmp = openfile(tname, "r")
        if (tmp == nil) then remove(tname); return nil end
        local res = ""
	local line = read(tmp)
	while(line ~= nil) do 
		if (not strfind(line, "^MT:")) then
			res = res .. line .. "\n"
		end
		line = read(tmp)
	end
        closefile(tmp)
        remove(tname)
        return res
end


function non_blocking_rng_ok()
	return 1
end


function persist_phrase_ok()
	return 1
end


function get_author(branchname)
        local user = getenv("USER")
        local host = getenv("HOSTNAME")
        if ((user == nil) or (host == nil)) then return nil end
        return format("%s@%s", user, host)
end


function merge2(left, right)
	local lfile = tmpname()
	local rfile = tmpname()
	local outfile = tmpname()

	-- write out one file
	local tmp = openfile(lfile, "w+")
	if (tmp == nil) then 
		return nil 
	end;
	write(tmp, left)
	closefile(tmp)

	-- write out the other file
	tmp = openfile(rfile, "w+")
	if (tmp == nil) then 
		remove(lfile)
		return nil
	end
	write(tmp, right)
	closefile(tmp)

	-- run emacs to merge the files
	local elisp = "'(ediff-merge-files \"%s\" \"%s\" nil \"%s\")'"
	local cmd_fmt = "emacs -no-init-file -eval " .. elisp
	local cmd = format(cmd_fmt, lfile, rfile, outfile)
	write(format("executing external 2-way merge command: %s\n", cmd))
	if (execute(cmd) ~= 0) then 
		remove(lfile)
		remove(rfile) 
		remove(outfile)
		return nil
	end

	-- read in the results
	tmp = openfile(outfile, "w+")
	if (tmp == nil) then
		return nil
	end
	local data = read(tmp, "*a")
	closefile(tmp)

	remove(lfile)
	remove(rfile)
	remove(outfile)

	return data
end


function merge3(ancestor, left, right)
	local afile = tmpname()
	local lfile = tmpname()
	local rfile = tmpname()
	local outfile = tmpname()

	-- write out one file
	local tmp = openfile(lfile, "w+")
	if (tmp == nil) then 
		return nil 
	end;
	write(tmp, left)
	closefile(tmp)

	-- write out the other file
	tmp = openfile(rfile, "w+")
	if (tmp == nil) then 
		remove(lfile)
		return nil
	end
	write(tmp, right)
	closefile(tmp)

	-- write out the ancestor
	tmp = openfile(afile, "w+")
	if (tmp == nil) then 
		remove(lfile)
		remove(rfile) 
		return nil
	end
	write(tmp, ancestor)
	closefile(tmp)

	-- run emacs to merge the files
	local elisp = "'(ediff-merge-files-with-ancestor \"%s\" \"%s\" \"%s\" nil \"%s\")'"
	local cmd_fmt = "emacs -no-init-file -eval " .. elisp
	local cmd = format(cmd_fmt, lfile, rfile, afile, outfile)
	write(format("executing external 3-way merge command: %s\n", cmd))
	if (execute(cmd) ~= 0) then 
		remove(lfile)
		remove(rfile)
		remove(ancestor)
		remove(outfile) 
		return nil
	end

	-- read in the results
	tmp = openfile(outfile, "w+")
	if (tmp == nil) then
		return nil
	end
	local data = read(tmp, "*a")
	closefile(tmp)

	remove(lfile)
	remove(rfile)
	remove(outfile)

	return data
end