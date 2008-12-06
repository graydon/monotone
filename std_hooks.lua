
-- this is the standard set of lua hooks for monotone;
-- user-provided files can override it or add to it.

function temp_file(namehint)
   local tdir
   tdir = os.getenv("TMPDIR")
   if tdir == nil then tdir = os.getenv("TMP") end
   if tdir == nil then tdir = os.getenv("TEMP") end
   if tdir == nil then tdir = "/tmp" end
   local filename
   if namehint == nil then
      filename = string.format("%s/mtn.XXXXXX", tdir)
   else
      filename = string.format("%s/mtn.%s.XXXXXX", tdir, namehint)
   end
   local name = mkstemp(filename)
   local file = io.open(name, "r+")
   return file, name
end

function execute(path, ...)
   local pid
   local ret = -1
   pid = spawn(path, unpack(arg))
   if (pid ~= -1) then ret, pid = wait(pid) end
   return ret
end

function execute_redirected(stdin, stdout, stderr, path, ...)
   local pid
   local ret = -1
   io.flush();
   pid = spawn_redirected(stdin, stdout, stderr, path, unpack(arg))
   if (pid ~= -1) then ret, pid = wait(pid) end
   return ret
end

-- Wrapper around execute to let user confirm in the case where a subprocess
-- returns immediately
-- This is needed to work around some brokenness with some merge tools
-- (e.g. on OS X)
function execute_confirm(path, ...)
   ret = execute(path, unpack(arg))

   if (ret ~= 0)
   then
      print(gettext("Press enter"))
   else
      print(gettext("Press enter when the subprocess has completed"))
   end
   io.read()
   return ret
end

-- attributes are persistent metadata about files (such as execute
-- bit, ACLs, various special flags) which we want to have set and
-- re-set any time the files are modified. the attributes themselves
-- are stored in the roster associated with the revision. each (f,k,v)
-- attribute triple turns into a call to attr_functions[k](f,v) in lua.

if (attr_init_functions == nil) then
   attr_init_functions = {}
end

attr_init_functions["mtn:execute"] =
   function(filename)
      if (is_executable(filename)) then
        return "true"
      else
        return nil
      end
   end

attr_init_functions["mtn:manual_merge"] =
   function(filename)
      if (binary_file(filename)) then
        return "true" -- binary files must be merged manually
      else
        return nil
      end
   end

if (attr_functions == nil) then
   attr_functions = {}
end

attr_functions["mtn:execute"] =
   function(filename, value)
      if (value == "true") then
         make_executable(filename)
      end
   end

function dir_matches(name, dir)
   -- helper for ignore_file, matching files within dir, or dir itself.
   -- eg for dir of 'CVS', matches CVS/, CVS/*, */CVS/ and */CVS/*
   if (string.find(name, "^" .. dir .. "/")) then return true end
   if (string.find(name, "^" .. dir .. "$")) then return true end
   if (string.find(name, "/" .. dir .. "/")) then return true end
   if (string.find(name, "/" .. dir .. "$")) then return true end
   return false
end

function portable_readline(f)
    line = f:read()
    if line ~= nil then
        line = string.gsub(line, "\r$","") -- strip possible \r left from windows editing
    end
    return line
end

function ignore_file(name)
   -- project specific
   if (ignored_files == nil) then
      ignored_files = {}
      local ignfile = io.open(".mtn-ignore", "r")
      if (ignfile ~= nil) then
         local line = portable_readline(ignfile)
         while (line ~= nil) do
            if line ~= "" then
                table.insert(ignored_files, line)
            end
            line = portable_readline(ignfile)
         end
         io.close(ignfile)
      end
   end

   local warn_reported_file = false
   for i, line in pairs(ignored_files)
   do
      if (line ~= nil) then
         local pcallstatus, result = pcall(function()
        return regex.search(line, name)
     end)
         if pcallstatus == true then
            -- no error from the regex.search call
            if result == true then return true end
         else
            -- regex.search had a problem, warn the user their
            -- .mtn-ignore file syntax is wrong
        if not warn_reported_file then
           io.stderr:write("mtn: warning: while matching file '"
                       .. name .. "':\n")
           warn_reported_file = true
        end
            io.stderr:write(".mtn-ignore:" .. i .. ": warning: " .. result
                    .. "\n\t- skipping this regex for "
                .. "all remaining files.\n")
            ignored_files[i] = nil
         end
      end
   end

   local file_pats = {
      -- c/c++
      "%.a$", "%.so$", "%.o$", "%.la$", "%.lo$", "^core$",
      "/core$", "/core%.%d+$",
      -- java
      "%.class$",
      -- python
      "%.pyc$", "%.pyo$",
      -- gettext
      "%.g?mo$",
      -- intltool
      "%.intltool%-merge%-cache$",
      -- TeX
      "%.aux$",
      -- backup files
      "%.bak$", "%.orig$", "%.rej$", "%~$",
      -- vim creates .foo.swp files
      "%.[^/]*%.swp$",
      -- emacs creates #foo# files
      "%#[^/]*%#$",
      -- other VCSes (where metadata is stored in named files):
      "%.scc$",
      -- desktop/directory configuration metadata
      "^%.DS_Store$", "/%.DS_Store$", "^desktop%.ini$", "/desktop%.ini$"
   }

   local dir_pats = {
      -- autotools detritus:
      "autom4te%.cache", "%.deps", "%.libs",
      -- Cons/SCons detritus:
      "%.consign", "%.sconsign",
      -- other VCSes (where metadata is stored in named dirs):
      "CVS", "%.svn", "SCCS", "_darcs", "%.cdv", "%.git", "%.bzr", "%.hg"
   }

   for _, pat in ipairs(file_pats) do
      if string.find(name, pat) then return true end
   end
   for _, pat in ipairs(dir_pats) do
      if dir_matches(name, pat) then return true end
   end

   return false;
end

-- return true means "binary", false means "text",
-- nil means "unknown, try to guess"
function binary_file(name)
   -- some known binaries, return true
   local bin_pats = {
      "%.gif$", "%.jpe?g$", "%.png$", "%.bz2$", "%.gz$", "%.zip$",
      "%.class$", "%.jar$", "%.war$", "%.ear$"
   }

   -- some known text, return false
   local txt_pats = {
      "%.cc?$", "%.cxx$", "%.hh?$", "%.hxx$", "%.cpp$", "%.hpp$",
      "%.lua$", "%.texi$", "%.sql$", "%.java$"
   }

   local lowname=string.lower(name)
   for _, pat in ipairs(bin_pats) do
      if string.find(lowname, pat) then return true end
   end
   for _, pat in ipairs(txt_pats) do
      if string.find(lowname, pat) then return false end
   end

   -- unknown - read file and use the guess-binary
   -- monotone built-in function
   return guess_binary_file_contents(name)
end

-- given a file name, return a regular expression which will match
-- lines that name top-level constructs in that file, or "", to disable
-- matching.
function get_encloser_pattern(name)
   -- texinfo has special sectioning commands
   if (string.find(name, "%.texi$")) then
      -- sectioning commands in texinfo: @node, @chapter, @top,
      -- @((sub)?sub)?section, @unnumbered(((sub)?sub)?sec)?,
      -- @appendix(((sub)?sub)?sec)?, @(|major|chap|sub(sub)?)heading
      return ("^@("
              .. "node|chapter|top"
              .. "|((sub)?sub)?section"
              .. "|(unnumbered|appendix)(((sub)?sub)?sec)?"
              .. "|(major|chap|sub(sub)?)?heading"
              .. ")")
   end
   -- LaTeX has special sectioning commands.  This rule is applied to ordinary
   -- .tex files too, since there's no reliable way to distinguish those from
   -- latex files anyway, and there's no good pattern we could use for
   -- arbitrary plain TeX anyway.
   if (string.find(name, "%.tex$")
       or string.find(name, "%.ltx$")
       or string.find(name, "%.latex$")) then
      return ("\\\\("
              .. "part|chapter|paragraph|subparagraph"
              .. "|((sub)?sub)?section"
              .. ")")
   end
   -- There's no good way to find section headings in raw text, and trying
   -- just gives distracting output, so don't even try.
   if (string.find(name, "%.txt$")
       or string.upper(name) == "README") then
      return ""
   end
   -- This default is correct surprisingly often -- in pretty much any text
   -- written with code-like indentation.
   return "^[[:alnum:]$_]"
end

function edit_comment(basetext, user_log_message)
   local exe = nil
   if (program_exists_in_path("vi")) then exe = "vi" end
   if (string.sub(get_ostype(), 1, 6) ~= "CYGWIN" and program_exists_in_path("notepad.exe")) then exe = "notepad.exe" end
   local debian_editor = io.open("/usr/bin/editor")
   if (debian_editor ~= nil) then
      debian_editor:close()
      exe = "/usr/bin/editor"
   end
   local visual = os.getenv("VISUAL")
   if (visual ~= nil) then exe = visual end
   local editor = os.getenv("EDITOR")
   if (editor ~= nil) then exe = editor end

   if (exe == nil) then
      io.write("Could not find editor to enter commit message\n"
               .. "Try setting the environment variable EDITOR\n")
      return nil
   end

   local tmp, tname = temp_file()
   if (tmp == nil) then return nil end
   basetext = "MTN: " .. string.gsub(basetext, "\n", "\nMTN: ") .. "\n"
   tmp:write(user_log_message)
   if user_log_message == "" or string.sub(user_log_message, -1) ~= "\n" then
      tmp:write("\n")
   end
   tmp:write(basetext)
   io.close(tmp)

   if (execute(exe, tname) ~= 0) then
      io.write(string.format(gettext("Error running editor '%s' to enter log message\n"),
                             exe))
      os.remove(tname)
      return nil
   end

   tmp = io.open(tname, "r")
   if (tmp == nil) then os.remove(tname); return nil end
   local res = ""
   local line = tmp:read()
   while(line ~= nil) do
      if (not string.find(line, "^MTN:")) then
         res = res .. line .. "\n"
      end
      line = tmp:read()
   end
   io.close(tmp)
   os.remove(tname)
   return res
end


function persist_phrase_ok()
   return true
end


function use_inodeprints()
   return false
end


-- trust evaluation hooks

function intersection(a,b)
   local s={}
   local t={}
   for k,v in pairs(a) do s[v] = 1 end
   for k,v in pairs(b) do if s[v] ~= nil then table.insert(t,v) end end
   return t
end

function get_revision_cert_trust(signers, id, name, val)
   return true
end

function get_manifest_cert_trust(signers, id, name, val)
   return true
end

function get_file_cert_trust(signers, id, name, val)
   return true
end

function accept_testresult_change(old_results, new_results)
   local reqfile = io.open("_MTN/wanted-testresults", "r")
   if (reqfile == nil) then return true end
   local line = reqfile:read()
   local required = {}
   while (line ~= nil)
   do
      required[line] = true
      line = reqfile:read()
   end
   io.close(reqfile)
   for test, res in pairs(required)
   do
      if old_results[test] == true and new_results[test] ~= true
      then
         return false
      end
   end
   return true
end

-- merger support

-- Fields in the mergers structure:
-- cmd       : a function that performs the merge operation using the chosen
--             program, best try.
-- available : a function that checks that the needed program is installed and
--             in $PATH
-- wanted    : a function that checks if the user doesn't want to use this
--             method, and returns false if so.  This should normally return
--             true, but in some cases, especially when the merger is really
--             an editor, the user might have a preference in EDITOR and we
--             need to respect that.
--             NOTE: wanted is only used when the user has NOT defined the
--             `merger' variable or the MTN_MERGE environment variable.
mergers = {}

-- This merger is designed to fail if there are any conflicts without trying to resolve them
mergers.fail = {
   cmd = function (tbl) return false end,
   available = function () return true end,
   wanted = function () return true end
}

mergers.meld = {
   cmd = function (tbl)
      io.write (string.format("\nWARNING: 'meld' was choosen to perform external 3-way merge.\n"..
          "You should merge all changes to *CENTER* file due to limitation of program\n"..
          "arguments.\n\n"))
      local path = "meld"
      local ret = execute(path, tbl.lfile, tbl.afile, tbl.rfile)
      if (ret ~= 0) then
         io.write(string.format(gettext("Error running merger '%s'\n"), path))
         return false
      end
      return tbl.afile
   end ,
   available = function () return program_exists_in_path("meld") end,
   wanted = function () return true end
}

mergers.tortoise = {
   cmd = function (tbl)
      local path = "tortoisemerge"
      local ret = execute(path,
                          string.format("/base:%s", tbl.afile),
                          string.format("/theirs:%s", tbl.lfile),
                          string.format("/mine:%s", tbl.rfile),
                          string.format("/merged:%s", tbl.outfile))
      if (ret ~= 0) then
         io.write(string.format(gettext("Error running merger '%s'\n"), path))
         return false
      end
      return tbl.outfile
   end ,
   available = function() return program_exists_in_path ("tortoisemerge") end,
   wanted = function () return true end
}

mergers.vim = {
   cmd = function (tbl)
      function execute_diff3(mine, yours, out)
     local diff3_args = {
        "diff3",
        "--merge",
        "--easy-only",
     }
     table.insert(diff3_args, string.gsub(mine, "\\", "/") .. "")
     table.insert(diff3_args, string.gsub(tbl.afile, "\\", "/") .. "")
     table.insert(diff3_args, string.gsub(yours, "\\", "/") .. "")

     return execute_redirected("", string.gsub(out, "\\", "/"), "", unpack(diff3_args))
      end

      io.write (string.format("\nWARNING: 'vim' was choosen to perform external 3-way merge.\n"..
          "You should merge all changes to *LEFT* file due to limitation of program\n"..
          "arguments.\n\n"))

      local vim
      if os.getenv ("DISPLAY") ~= nil and program_exists_in_path ("gvim") then
     vim = "gvim"
      else
     vim = "vim"
      end

      local lfile_merged = tbl.lfile .. ".merged"
      local rfile_merged = tbl.rfile .. ".merged"

      -- first merge lfile using diff3
      local ret = execute_diff3(tbl.lfile, tbl.rfile, lfile_merged)
      if ret == 2 then
         io.write(string.format(gettext("Error running diff3 for merger '%s'\n"), vim))
         os.remove(lfile_merged)
     return false
      end

      -- now merge rfile using diff3
      ret = execute_diff3(tbl.rfile, tbl.lfile, rfile_merged)
      if ret == 2 then
         io.write(string.format(gettext("Error running diff3 for merger '%s'\n"), vim))
         os.remove(lfile_merged)
         os.remove(rfile_merged)
     return false
      end

      os.rename(lfile_merged, tbl.lfile)
      os.rename(rfile_merged, tbl.rfile)

      local ret = execute(vim, "-f", "-d", "-c", string.format("silent file %s", tbl.outfile),
                          tbl.lfile, tbl.rfile)
      if (ret ~= 0) then
         io.write(string.format(gettext("Error running merger '%s'\n"), vim))
         return false
      end
      return tbl.outfile
   end ,
   available =
      function ()
     return program_exists_in_path("diff3") and
            (program_exists_in_path("vim") or
        program_exists_in_path("gvim"))
      end ,
   wanted =
      function ()
     local editor = os.getenv("EDITOR")
     if editor and
        not (string.find(editor, "vim") or
         string.find(editor, "gvim")) then
        return false
     end
     return true
      end
}

mergers.rcsmerge = {
   cmd = function (tbl)
      -- XXX: This is tough - should we check if conflict markers stay or not?
      -- If so, we should certainly give the user some way to still force
      -- the merge to proceed since they can appear in the files (and I saw
      -- that). --pasky
      local merge = os.getenv("MTN_RCSMERGE")
      if execute(merge, tbl.lfile, tbl.afile, tbl.rfile) == 0 then
         copy_text_file(tbl.lfile, tbl.outfile);
         return tbl.outfile
      end
      local ret = execute("vim", "-f", "-c", string.format("file %s", tbl.outfile
),
                          tbl.lfile)
      if (ret ~= 0) then
         io.write(string.format(gettext("Error running merger '%s'\n"), "vim"))
         return false
      end
      return tbl.outfile
   end,
   available =
      function ()
     local merge = os.getenv("MTN_RCSMERGE")
     return merge and
        program_exists_in_path(merge) and program_exists_in_path("vim")
      end ,
   wanted = function () return os.getenv("MTN_RCSMERGE") ~= nil end
}

--  GNU diffutils based merging
mergers.diffutils = {
    --  merge procedure execution
    cmd = function (tbl)
        --  parse options
        local option = {}
        option.partial = false
        option.diff3opts = ""
        option.sdiffopts = ""
        local options = os.getenv("MTN_MERGE_DIFFUTILS")
        if options ~= nil then
            for spec in string.gmatch(options, "%s*(%w[^,]*)%s*,?") do
                local name, value = string.match(spec, "^(%w+)=([^,]*)")
                if name == nil then
                    name = spec
                    value = true
                end
                if type(option[name]) == "nil" then
                    io.write("mtn: " .. string.format(gettext("invalid \"diffutils\" merger option \"%s\""), name) .. "\n")
                    return false
                end
                option[name] = value
            end
        end

        --  determine the diff3(1) command
        local diff3 = {
            "diff3",
            "--merge",
            "--label", string.format("%s [left]",     tbl.left_path ),
            "--label", string.format("%s [ancestor]", tbl.anc_path  ),
            "--label", string.format("%s [right]",    tbl.right_path),
        }
        if option.diff3opts ~= "" then
            for opt in string.gmatch(option.diff3opts, "%s*([^%s]+)%s*") do
                table.insert(diff3, opt)
            end
        end
        table.insert(diff3, string.gsub(tbl.lfile, "\\", "/") .. "")
        table.insert(diff3, string.gsub(tbl.afile, "\\", "/") .. "")
        table.insert(diff3, string.gsub(tbl.rfile, "\\", "/") .. "")

        --  dispatch according to major operation mode
        if option.partial then
            --  partial batch/non-modal 3-way merge "resolution":
            --  simply merge content with help of conflict markers
            io.write("mtn: " .. gettext("3-way merge via GNU diffutils, resolving conflicts via conflict markers") .. "\n")
            local ret = execute_redirected("", string.gsub(tbl.outfile, "\\", "/"), "", unpack(diff3))
            if ret == 2 then
                io.write("mtn: " .. gettext("error running GNU diffutils 3-way difference/merge tool \"diff3\"") .. "\n")
                return false
            end
            return tbl.outfile
        else
            --  real interactive/modal 3/2-way merge resolution:
            --  display 3-way merge conflict and perform 2-way merge resolution
            io.write("mtn: " .. gettext("3-way merge via GNU diffutils, resolving conflicts via interactive prompt") .. "\n")

            --  display 3-way merge conflict (batch)
            io.write("\n")
            io.write("mtn: " .. gettext("---- CONFLICT SUMMARY ------------------------------------------------") .. "\n")
            local ret = execute(unpack(diff3))
            if ret == 2 then
                io.write("mtn: " .. gettext("error running GNU diffutils 3-way difference/merge tool \"diff3\"") .. "\n")
                return false
            end

            --  perform 2-way merge resolution (interactive)
            io.write("\n")
            io.write("mtn: " .. gettext("---- CONFLICT RESOLUTION ---------------------------------------------") .. "\n")
            local sdiff = {
                "sdiff",
                "--diff-program=diff",
                "--suppress-common-lines",
                "--minimal",
                "--output=" .. string.gsub(tbl.outfile, "\\", "/")
            }
            if option.sdiffopts ~= "" then
                for opt in string.gmatch(option.sdiffopts, "%s*([^%s]+)%s*") do
                    table.insert(sdiff, opt)
                end
            end
            table.insert(sdiff, string.gsub(tbl.lfile, "\\", "/") .. "")
            table.insert(sdiff, string.gsub(tbl.rfile, "\\", "/") .. "")
            local ret = execute(unpack(sdiff))
            if ret == 2 then
                io.write("mtn: " .. gettext("error running GNU diffutils 2-way merging tool \"sdiff\"") .. "\n")
                return false
            end
            return tbl.outfile
        end
    end,

    --  merge procedure availability check
    available = function ()
        --  make sure the GNU diffutils tools are available
        return program_exists_in_path("diff3") and
               program_exists_in_path("sdiff") and
               program_exists_in_path("diff");
    end,

    --  merge procedure request check
    wanted = function ()
        --  assume it is requested (if it is available at all)
        return true
    end
}

mergers.emacs = {
   cmd = function (tbl)
      local emacs
      if program_exists_in_path("xemacs") then
         emacs = "xemacs"
      else
         emacs = "emacs"
      end
      local elisp = "(ediff-merge-files-with-ancestor \"%s\" \"%s\" \"%s\" nil \"%s\")"
      -- Converting backslashes is necessary on Win32 MinGW; emacs
      -- lisp string syntax says '\' is an escape.
      local ret = execute(emacs, "--eval",
                          string.format(elisp,
                          string.gsub (tbl.lfile, "\\", "/"),
                          string.gsub (tbl.rfile, "\\", "/"),
                          string.gsub (tbl.afile, "\\", "/"),
                          string.gsub (tbl.outfile, "\\", "/")))
      if (ret ~= 0) then
         io.write(string.format(gettext("Error running merger '%s'\n"), emacs))
         return false
      end
      return tbl.outfile
   end,
   available =
      function ()
     return program_exists_in_path("xemacs") or
        program_exists_in_path("emacs")
      end ,
   wanted =
      function ()
     local editor = os.getenv("EDITOR")
     if editor and
        not (string.find(editor, "emacs") or
         string.find(editor, "gnu")) then
        return false
     end
     return true
      end
}

mergers.xxdiff = {
   cmd = function (tbl)
      local path = "xxdiff"
      local ret = execute(path,
                        "--title1", tbl.left_path,
                        "--title2", tbl.right_path,
                        "--title3", tbl.merged_path,
                        tbl.lfile, tbl.afile, tbl.rfile,
                        "--merge",
                        "--merged-filename", tbl.outfile,
                        "--exit-with-merge-status")
      if (ret ~= 0) then
         io.write(string.format(gettext("Error running merger '%s'\n"), path))
         return false
      end
      return tbl.outfile
   end,
   available = function () return program_exists_in_path("xxdiff") end,
   wanted = function () return true end
}

mergers.kdiff3 = {
   cmd = function (tbl)
      local path = "kdiff3"
      local ret = execute(path,
                          "--L1", tbl.anc_path,
                          "--L2", tbl.left_path,
                          "--L3", tbl.right_path,
                          tbl.afile, tbl.lfile, tbl.rfile,
                          "--merge",
                          "--o", tbl.outfile)
      if (ret ~= 0) then
         io.write(string.format(gettext("Error running merger '%s'\n"), path))
         return false
      end
      return tbl.outfile
   end,
   available = function () return program_exists_in_path("kdiff3") end,
   wanted = function () return true end
}

mergers.opendiff = {
   cmd = function (tbl)
      local path = "opendiff"
      -- As opendiff immediately returns, let user confirm manually
      local ret = execute_confirm(path,
                                  tbl.lfile,tbl.rfile,
                                  "-ancestor",tbl.afile,
                                  "-merge",tbl.outfile)
      if (ret ~= 0) then
         io.write(string.format(gettext("Error running merger '%s'\n"), path))
         return false
      end
      return tbl.outfile
   end,
   available = function () return program_exists_in_path("opendiff") end,
   wanted = function () return true end
}

function write_to_temporary_file(data, namehint)
   tmp, filename = temp_file(namehint)
   if (tmp == nil) then
      return nil
   end;
   tmp:write(data)
   io.close(tmp)
   return filename
end

function copy_text_file(srcname, destname)
   src = io.open(srcname, "r")
   if (src == nil) then return nil end
   dest = io.open(destname, "w")
   if (dest == nil) then return nil end

   while true do
      local line = src:read()
      if line == nil then break end
      dest:write(line, "\n")
   end

   io.close(dest)
   io.close(src)
end

function read_contents_of_file(filename, mode)
   tmp = io.open(filename, mode)
   if (tmp == nil) then
      return nil
   end
   local data = tmp:read("*a")
   io.close(tmp)
   return data
end

function program_exists_in_path(program)
   return existsonpath(program) == 0
end

function get_preferred_merge3_command (tbl)
   local default_order = {"kdiff3", "xxdiff", "opendiff", "tortoise", "emacs", "vim", "meld", "diffutils"}
   local function existmerger(name)
      local m = mergers[name]
      if type(m) == "table" and m.available(tbl) then
         return m.cmd
      end
      return nil
   end
   local function trymerger(name)
      local m = mergers[name]
      if type(m) == "table" and m.available(tbl) and m.wanted(tbl) then
         return m.cmd
      end
      return nil
   end
   -- Check if there's a merger given by the user.
   local mkey = os.getenv("MTN_MERGE")
   if not mkey then mkey = merger end
   if not mkey and os.getenv("MTN_RCSMERGE") then mkey = "rcsmerge" end
   -- If there was a user-given merger, see if it exists.  If it does, return
   -- the cmd function.  If not, return nil.
   local c
   if mkey then c = existmerger(mkey) end
   if c then return c,mkey end
   if mkey then return nil,mkey end
   -- If there wasn't any user-given merger, take the first that's available
   -- and wanted.
   for _,mkey in ipairs(default_order) do
      c = trymerger(mkey) ; if c then return c,mkey end
   end
end

function merge3 (anc_path, left_path, right_path, merged_path, ancestor, left, right)
   local ret = nil
   local tbl = {}

   tbl.anc_path = anc_path
   tbl.left_path = left_path
   tbl.right_path = right_path

   tbl.merged_path = merged_path
   tbl.afile = nil
   tbl.lfile = nil
   tbl.rfile = nil
   tbl.outfile = nil
   tbl.meld_exists = false
   tbl.lfile = write_to_temporary_file (left, "left")
   tbl.afile = write_to_temporary_file (ancestor, "ancestor")
   tbl.rfile = write_to_temporary_file (right, "right")
   tbl.outfile = write_to_temporary_file ("", "merged")

   if tbl.lfile ~= nil and tbl.rfile ~= nil and tbl.afile ~= nil and tbl.outfile ~= nil
   then
      local cmd,mkey = get_preferred_merge3_command (tbl)
      if cmd ~=nil
      then
         io.write ("mtn: " .. string.format(gettext("executing external 3-way merge via \"%s\" merger\n"), mkey))
         ret = cmd (tbl)
         if not ret then
            ret = nil
         else
            ret = read_contents_of_file (ret, "r")
            if string.len (ret) == 0
            then
               ret = nil
            end
         end
      else
     if mkey then
        io.write (string.format("The possible commands for the "..mkey.." merger aren't available.\n"..
                "You may want to check that $MTN_MERGE or the lua variable `merger' is set\n"..
                "to something available.  If you want to use vim or emacs, you can also\n"..
        "set $EDITOR to something appropriate.\n"))
     else
        io.write (string.format("No external 3-way merge command found.\n"..
                "You may want to check that $EDITOR is set to an editor that supports 3-way\n"..
                "merge, set this explicitly in your get_preferred_merge3_command hook,\n"..
                "or add a 3-way merge program to your path.\n"))
     end
      end
   end

   os.remove (tbl.lfile)
   os.remove (tbl.rfile)
   os.remove (tbl.afile)
   os.remove (tbl.outfile)

   return ret
end

-- expansion of values used in selector completion

function expand_selector(str)

   -- something which looks like a generic cert pattern
   if string.find(str, "^[^=]*=.*$")
   then
      return ("c:" .. str)
   end

   -- something which looks like an email address
   if string.find(str, "[%w%-_]+@[%w%-_]+")
   then
      return ("a:" .. str)
   end

   -- something which looks like a branch name
   if string.find(str, "[%w%-]+%.[%w%-]+")
   then
      return ("b:" .. str)
   end

   -- a sequence of nothing but hex digits
   if string.find(str, "^%x+$")
   then
      return ("i:" .. str)
   end

   -- tries to expand as a date
   local dtstr = expand_date(str)
   if  dtstr ~= nil
   then
      return ("d:" .. dtstr)
   end

   return nil
end

-- expansion of a date expression
function expand_date(str)
   -- simple date patterns
   if string.find(str, "^19%d%d%-%d%d")
      or string.find(str, "^20%d%d%-%d%d")
   then
      return (str)
   end

   -- "now"
   if str == "now"
   then
      local t = os.time(os.date('!*t'))
      return os.date("%FT%T", t)
   end

   -- today don't uses the time         # for xgettext's sake, an extra quote
   if str == "today"
   then
      local t = os.time(os.date('!*t'))
      return os.date("%F", t)
   end

   -- "yesterday", the source of all hangovers
   if str == "yesterday"
   then
      local t = os.time(os.date('!*t'))
      return os.date("%F", t - 86400)
   end

   -- "CVS style" relative dates such as "3 weeks ago"
   local trans = {
      minute = 60;
      hour = 3600;
      day = 86400;
      week = 604800;
      month = 2678400;
      year = 31536000
   }
   local pos, len, n, type = string.find(str, "(%d+) ([minutehordaywk]+)s? ago")
   if trans[type] ~= nil
   then
      local t = os.time(os.date('!*t'))
      if trans[type] <= 3600
      then
        return os.date("%FT%T", t - (n * trans[type]))
      else
        return os.date("%F", t - (n * trans[type]))
      end
   end

   return nil
end


external_diff_default_args = "-u"

-- default external diff, works for gnu diff
function external_diff(file_path, data_old, data_new, is_binary, diff_args, rev_old, rev_new)
   local old_file = write_to_temporary_file(data_old);
   local new_file = write_to_temporary_file(data_new);

   if diff_args == nil then diff_args = external_diff_default_args end
   execute("diff", diff_args, "--label", file_path .. "\told", old_file, "--label", file_path .. "\tnew", new_file);

   os.remove (old_file);
   os.remove (new_file);
end

-- netsync permissions hooks (and helper)

function globish_match(glob, str)
      local pcallstatus, result = pcall(function() if (globish.match(glob, str)) then return true else return false end end)
      if pcallstatus == true then
          -- no error
          return result
      else
          -- globish.match had a problem
          return nil
      end
end

function get_netsync_read_permitted(branch, ident)
   local permfile = io.open(get_confdir() .. "/read-permissions", "r")
   if (permfile == nil) then return false end
   local dat = permfile:read("*a")
   io.close(permfile)
   local res = parse_basic_io(dat)
   if res == nil then
      io.stderr:write("file read-permissions cannot be parsed\n")
      return false
   end
   local matches = false
   local cont = false
   for i, item in pairs(res)
   do
      -- legal names: pattern, allow, deny, continue
      if item.name == "pattern" then
         if matches and not cont then return false end
         matches = false
         cont = false
         for j, val in pairs(item.values) do
            if globish_match(val, branch) then matches = true end
         end
      elseif item.name == "allow" then if matches then
         for j, val in pairs(item.values) do
            if val == "*" then return true end
            if val == "" and ident == nil then return true end
            if globish_match(val, ident) then return true end
         end
      end elseif item.name == "deny" then if matches then
         for j, val in pairs(item.values) do
            if val == "*" then return false end
            if val == "" and ident == nil then return false end
            if globish_match(val, ident) then return false end
         end
      end elseif item.name == "continue" then if matches then
         cont = true
         for j, val in pairs(item.values) do
            if val == "false" or val == "no" then cont = false end
         end
      end elseif item.name ~= "comment" then
         io.stderr:write("unknown symbol in read-permissions: " .. item.name .. "\n")
         return false
      end
   end
   return false
end

function get_netsync_write_permitted(ident)
   local permfile = io.open(get_confdir() .. "/write-permissions", "r")
   if (permfile == nil) then
      return false
   end
   local matches = false
   local line = permfile:read()
   while (not matches and line ~= nil) do
      local _, _, ln = string.find(line, "%s*([^%s]*)%s*")
      if ln == "*" then matches = true end
      if globish_match(ln, ident) then matches = true end
      line = permfile:read()
   end
   io.close(permfile)
   return matches
end

-- This is a simple function which assumes you're going to be spawning
-- a copy of mtn, so reuses a common bit at the end for converting
-- local args into remote args. You might need to massage the logic a
-- bit if this doesn't fit your assumptions.

function get_netsync_connect_command(uri, args)

        local argv = nil

        if uri["scheme"] == "ssh"
                and uri["host"]
                and uri["path"] then

                argv = { "ssh" }
                if uri["user"] then
                        table.insert(argv, "-l")
                        table.insert(argv, uri["user"])
                end
                if uri["port"] then
                        table.insert(argv, "-p")
                        table.insert(argv, uri["port"])
                end

                -- ssh://host/~/dir/file.mtn or
                -- ssh://host/~user/dir/file.mtn should be home-relative
                if string.find(uri["path"], "^/~") then
                        uri["path"] = string.sub(uri["path"], 2)
                end

                table.insert(argv, uri["host"])
        end

        if uri["scheme"] == "file" and uri["path"] then
                argv = { }
        end

        if uri["scheme"] == "ssh+ux"
                and uri["host"]
                and uri["path"] then

                argv = { "ssh" }
                if uri["user"] then
                        table.insert(argv, "-l")
                        table.insert(argv, uri["user"])
                end
                if uri["port"] then
                        table.insert(argv, "-p")
                        table.insert(argv, uri["port"])
                end

                -- ssh://host/~/dir/file.mtn or
                -- ssh://host/~user/dir/file.mtn should be home-relative
                if string.find(uri["path"], "^/~") then
                        uri["path"] = string.sub(uri["path"], 2)
                end

                table.insert(argv, uri["host"])
                table.insert(argv, get_remote_unix_socket_command(uri["host"]))
                table.insert(argv, "-")
                table.insert(argv, "UNIX-CONNECT:" .. uri["path"])
        else
            -- start remote monotone process
            if argv then

                    table.insert(argv, get_mtn_command(uri["host"]))

                    if args["debug"] then
                            table.insert(argv, "--debug")
                    else
                            table.insert(argv, "--quiet")
                    end

                    table.insert(argv, "--db")
                    table.insert(argv, uri["path"])
                    table.insert(argv, "serve")
                    table.insert(argv, "--stdio")
                    table.insert(argv, "--no-transport-auth")

            end
        end
        return argv
end

function use_transport_auth(uri)
        if uri["scheme"] == "ssh"
        or uri["scheme"] == "ssh+ux"
        or uri["scheme"] == "file" then
                return false
        else
                return true
        end
end

function get_mtn_command(host)
        return "mtn"
end

function get_default_command_options(command)
   local default_args = {}
   return default_args
end

dump                = {}
dump.depth          = 0
dump._string        = function(s) return string.format("%q", s) end
dump._number        = function(n) return tonumber(n) end
dump._boolean       = function(b) if (b) then return "true" end return "false" end
dump._userdata      = function(u) return "<userdata>" end
dump._function      = function(f) return "<function>" end
dump._nil           = function(n) return "nil" end
dump._thread        = function(t) return "<thread>" end
dump._lightuserdata = function(l) return "<lightuserdata>" end

dump._table = function(t)
    local buf = ''
    if (dump.depth > 0) then
        buf = buf .. '{\n'
    end
    dump.depth = dump.depth + 1;
    for k,v in pairs(t) do
        buf = buf..string.format('%s[%s] = %s;\n',
              string.rep("\t", dump.depth - 1),
              dump["_" .. type(k)](k),
              dump["_" .. type(v)](v))
    end
    dump.depth = dump.depth - 1;
    if (dump.depth > 0) then
        buf = buf .. string.rep("\t", dump.depth - 1) .. '}'
    end
    return buf
end

function hook_wrapper(func_name, ...)
    -- we have to ensure that nil arguments are restored properly for the
    -- function call, see http://lua-users.org/wiki/StoringNilsInTables
    local args = { n=select('#', ...), ... }
    for i=1,args.n do
        local val = assert(loadstring("return " .. args[i]),
                         "argument "..args[i].." could not be evaluated")()
        assert(val or args[i] == "nil",
               "argument "..args[i].." was evaluated to nil")
        args[i] = val
    end
    local res = { _G[func_name](unpack(args, 1, args.n)) }
    return dump._table(res)
end


function get_remote_unix_socket_command(host)
    return "socat"
end

do
   -- Hook functions are tables containing any of the following 6 items
   -- with associated functions:
   --
   --   startup			Corresponds to note_mtn_startup()
   --   start			Corresponds to note_netsync_start()
   --   revision_received	Corresponds to note_netsync_revision_received()
   --   cert_received		Corresponds to note_netsync_cert_received()
   --   pubkey_received		Corresponds to note_netsync_pubkey_received()
   --   end			Corresponds to note_netsync_end()
   --
   -- Those functions take exactly the same arguments as the corresponding
   -- global functions, but return a different kind of value, a tuple
   -- composed of a return code and a value to be returned back to monotone.
   -- The codes are strings:
   -- "continue" and "stop"
   -- When the code "continue" is returned and there's another notifier, the
   -- second value is ignored and the next notifier is called.  Otherwise,
   -- the second value is returned immediately.
   local hook_functions = {}
   local supported_items = {
      "startup",
      "start", "revision_received", "revision_sent", "cert_received", "cert_sent",
      "pubkey_received", "pubkey_sent", "end"
   }

   function _hook_functions_helper(f,...)
      local s = "continue"
      local v = nil
      for _,n in pairs(hook_functions) do
     if n[f] then
        s,v = n[f](...)
     end
     if s ~= "continue" then
        break
     end
      end
      return v
   end
   function note_mtn_startup(...)
      return _hook_functions_helper("startup",...)
   end
   function note_netsync_start(...)
      return _hook_functions_helper("start",...)
   end
   function note_netsync_revision_received(...)
      return _hook_functions_helper("revision_received",...)
   end
   function note_netsync_revision_sent(...)
      return _hook_functions_helper("revision_sent",...)
   end
   function note_netsync_cert_received(...)
      return _hook_functions_helper("cert_received",...)
   end
   function note_netsync_cert_sent(...)
      return _hook_functions_helper("cert_sent",...)
   end
   function note_netsync_pubkey_received(...)
      return _hook_functions_helper("pubkey_received",...)
   end
   function note_netsync_pubkey_sent(...)
      return _hook_functions_helper("pubkey_sent",...)
   end
   function note_netsync_end(...)
      return _hook_functions_helper("end",...)
   end

   function add_hook_functions(functions, precedence)
      if type(functions) ~= "table" or type(precedence) ~= "number" then
     return false, "Invalid type"
      end
      if hook_functions[precedence] then
     return false, "Precedence already taken"
      end

      local unknown_items = ""
      local warning = nil
      local is_member =
     function (s,t)
        for k,v in pairs(t) do if s == v then return true end end
        return false
     end

      for n,f in pairs(functions) do
     if type(n) == "string" then
        if not is_member(n, supported_items) then
           if unknown_items ~= "" then
          unknown_items = unknown_items .. ","
           end
           unknown_items = unknown_items .. n
        end
        if type(f) ~= "function" then
           return false, "Value for functions item "..n.." isn't a function"
        end
     else
        warning = "Non-string item keys found in functions table"
     end
      end

      if warning == nil and unknown_items ~= "" then
     warning = "Unknown item(s) " .. unknown_items .. " in functions table"
      end

      hook_functions[precedence] = functions
      return true, warning
   end
   function push_hook_functions(functions)
      local n = table.maxn(hook_functions) + 1
      return add_hook_functions(functions, n)
   end

   -- Kept for backward compatibility
   function add_netsync_notifier(notifier, precedence)
      return add_hook_functions(notifier, precedence)
   end
   function push_netsync_notifier(notifier)
      return push_hook_functions(notifier)
   end
end
