
-- this is the standard set of lua hooks for monotone;
-- user-provided files can override it or add to it.

function temp_file()
	local tdir
	tdir = os.getenv("TMPDIR")
	if tdir == nil then tdir = os.getenv("TMP") end
	if tdir == nil then tdir = os.getenv("TEMP") end
	if tdir == nil then tdir = "/tmp" end
	return io.mkstemp(string.format("%s/mt.XXXXXX", tdir))
end


-- attributes are persistent metadata about files (such as execute
-- bit, ACLs, various special flags) which we want to have set and
-- re-set any time the files are modified. the attributes themselves
-- are stored in a file .mt-attrs, in the working copy (and
-- manifest). each (f,k,v) triple in an atribute file turns into a
-- call to attr_functions[k](f,v) in lua.

if (attr_functions == nil) then
	attr_functions = {}
end


attr_functions["execute"] = 
  function(filename, value) 
	if (value == "true") then
		os.execute(string.format("chmod +x %s", filename))
	end
  end


function get_http_proxy(host, port)
	val = os.getenv("HTTP_PROXY")
	if (val == nil) then 
		val = os.getenv("http_proxy") 
	end
	if (val == nil) then 
		return nil
	end
	val = string.gsub(val, "http://", "")
	b, e = string.find(val, ":")
	if (b ~= nil and b > 0) then
		chost = string.sub(val, 0, b-1)
		cport = string.sub(val, b+1)
		return { chost, cport }
	end
	return { val, port }
end

function ignore_file(name)
	if (string.find(name, "%.a$")) then return true end
	if (string.find(name, "%.so$")) then return true end
	if (string.find(name, "%.o$")) then return true end
	if (string.find(name, "%.la$")) then return true end
	if (string.find(name, "%.lo$")) then return true end
	if (string.find(name, "%.aux$")) then return true end
	if (string.find(name, "%.bak$")) then return true end
	if (string.find(name, "%.orig$")) then return true end
	if (string.find(name, "%.rej$")) then return true end
	if (string.find(name, "%~$")) then return true end
	if (string.find(name, "/core$")) then return true end
	if (string.find(name, "^CVS/")) then return true end
	if (string.find(name, "^SVN/")) then return true end
	if (string.find(name, "/CVS/")) then return true end
	if (string.find(name, "/SVN/")) then return true end
	return false;
end


function edit_comment(basetext)
        local exe = "vi"
        local visual = os.getenv("VISUAL")
        if (visual ~= nil) then exe = visual end
        local editor = os.getenv("EDITOR")
        if (editor ~= nil) then exe = editor end

	local tmp, tname = temp_file()
        if (tmp == nil) then return nil end
	basetext = "MT: " .. string.gsub(basetext, "\n", "\nMT: ")
        tmp:write(basetext)
        io.close(tmp)

        if (os.execute(string.format("%s %s", exe, tname)) ~= 0) then
                os.remove(tname)
                return nil
        end

        tmp = io.open(tname, "r")
        if (tmp == nil) then os.remove(tname); return nil end
        local res = ""
	local line = tmp:read()
	while(line ~= nil) do 
		if (not string.find(line, "^MT:")) then
			res = res .. line .. "\n"
		end
		line = tmp:read()
	end
        io.close(tmp)
	os.remove(tname)
        return res
end


function non_blocking_rng_ok()
	return true
end


function persist_phrase_ok()
	return true
end

function get_mail_hostname(url)
	return os.getenv("HOSTNAME")
end

function get_author(branchname)
        local user = os.getenv("USER")
        local host = os.getenv("HOSTNAME")
        if ((user == nil) or (host == nil)) then return nil end
        return string.format("%s@%s", user, host)
end


function merge2(left, right)
	local lfile
	local rfile
	local outfile
	local tmp

	-- write out one file
	tmp, lfile = temp_file()
	if (tmp == nil) then 
		return nil 
	end;
	tmp:write(left)
	io.close(tmp)

	-- write out the other file
	tmp, rfile = temp_file()
	if (tmp == nil) then 
		os.remove(lfile)
		return nil
	end
	tmp:write(right)
	io.close(tmp)

	tmp, outfile = temp_file();
	io.close(tmp);

	-- run emacs to merge the files
	local elisp = "'(ediff-merge-files \"%s\" \"%s\" nil \"%s\")'"
	local cmd_fmt = "emacs -no-init-file -eval " .. elisp
	local cmd = string.format(cmd_fmt, lfile, rfile, outfile)
	io.write(string.format("executing external 2-way merge command: %s\n", cmd))
	if (os.execute(cmd) ~= 0) then 
		os.remove(lfile)
		os.remove(rfile) 
		os.remove(outfile)
		return nil
	end

	-- read in the results
	tmp = io.open(outfile, "r")
	if (tmp == nil) then
		return nil
	end
	local data = tmp:read("*a")
	io.close(tmp)

	os.remove(lfile)
	os.remove(rfile)
	os.remove(outfile)

	return data
end


function merge3(ancestor, left, right)
	local afile
	local lfile
	local rfile
	local outfile

	-- write out one file
	tmp, lfile = temp_file()
	if (tmp == nil) then 
		return nil 
	end;
	tmp:write(left)
	io.close(tmp)

	-- write out the other file
	tmp, rfile = temp_file()
	if (tmp == nil) then 
		os.remove(lfile)
		return nil
	end
	tmp:write(right)
	io.close(tmp)

	-- write out the ancestor
	tmp, afile = temp_file()
	if (tmp == nil) then 
		os.remove(lfile)
		os.remove(rfile) 
		return nil
	end
	tmp:write(ancestor)
	io.close(tmp)

	tmp, outfile = temp_file()
	io.close(tmp)

	-- run emacs to merge the files
	local elisp = "'(ediff-merge-files-with-ancestor \"%s\" \"%s\" \"%s\" nil \"%s\")'"
	local cmd_fmt = "emacs -no-init-file -eval " .. elisp
	local cmd = string.format(cmd_fmt, lfile, rfile, afile, outfile)
	io.write(string.format("executing external 3-way merge command: %s\n", cmd))
	if (os.execute(cmd) ~= 0) then 
		os.remove(lfile)
		os.remove(rfile)
		os.remove(ancestor)
		os.remove(outfile) 
		return nil
	end

	-- read in the results
	tmp = io.open(outfile, "r")
	if (tmp == nil) then
		return nil
	end
	local data = tmp:read("*a")
	io.close(tmp)

	os.remove(lfile)
	os.remove(rfile)
	os.remove(outfile)

	return data
end