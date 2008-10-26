-- misc global values
-- where the main testsuite file is
srcdir = get_source_dir()
-- where the individual test dirs are
-- most paths will be testdir.."/something"
-- normally reset by the main testsuite file
testdir = srcdir
-- was the -d switch given?
debugging = false

-- combined logfile; tester.cc will reset this to a filename, which is
-- then opened in run_tests
logfile = nil

-- This is for redirected output from local implementations
-- of shellutils type stuff (ie, grep).
-- Reason: {set,clear}_redirect don't seem to (always?) work
-- for this (at least on Windows).
files = {stdout = nil, stdin = nil, stderr = nil}

-- for convenience, this is the first word of what get_ostype() returns.
ostype = string.sub(get_ostype(), 1, string.find(get_ostype(), " ")-1)

-- table of per-test values
test = {}
-- misc per-test values
test.root = nil
test.name = nil
test.wanted_fail = false
test.partial_skip = false -- set this to true if you skip part of the test

--probably should put these in the error that gets thrown...
test.errfile = ""
test.errline = -1

-- for tracking background processes
test.bgid = 0
test.bglist = {}

test.log = nil -- logfile for this test

-- hook to be overridden by the main testsuite file, if necessary;
-- called after determining the set of tests to run.
-- P may be used to write messages to the user's tty.
function prepare_to_run_tests(P)
   return 0
end

-- hook to be overridden by the main testsuite file, if necessary;
-- called after opening the master logfile, but _before_ parsing
-- arguments or determining the set of tests to run.
-- P may be used to write messages to the user's tty.
function prepare_to_enumerate_tests(P)
   return 0
end

function L(...)
  test.log:write(unpack(arg))
  test.log:flush()
end

function getsrcline()
  local info
  local depth = 1
  repeat
    depth = depth + 1
    info = debug.getinfo(depth)
  until info == nil
  while src == nil and depth > 1 do
    depth = depth - 1
    info = debug.getinfo(depth)
    if string.find(info.source, "^@.*__driver__%.lua") then
      -- return info.source, info.currentline
      return test.name, info.currentline
    end
  end
end

function locheader()
  local _,line = getsrcline()
  if line == nil then line = -1 end
  if test.name == nil then
    return "\n<unknown>:" .. line .. ": "
  else
    return "\n" .. test.name .. ":" .. line .. ": "
  end
end

function err(what, level)
  if level == nil then level = 2 end
  test.errfile, test.errline = getsrcline()
  local e
  if type(what) == "table" then
    e = what
    if e.bt == nil then e.bt = {} end
    table.insert(e.bt, debug.traceback())
  else
    e = {e = what, bt = {debug.traceback()}}
  end
  error(e, level)
end

do -- replace some builtings with logged versions
  unlogged_mtime = mtime
  mtime = function(name)
    local x = unlogged_mtime(name)
    L(locheader(), "mtime(", name, ") = ", tostring(x), "\n")
    return x
  end

  unlogged_mkdir = mkdir
  mkdir = function(name)
    L(locheader(), "mkdir ", name, "\n")
    unlogged_mkdir(name)
  end

  unlogged_existsonpath = existsonpath
  existsonpath = function(name)
    local r = (unlogged_existsonpath(name) == 0)
    local what
    if r then
      what = "exists"
    else
      what = "does not exist"
    end
    L(locheader(), name, " ", what, " on the path\n")
    return r
  end
end

function numlines(filename)
  local n = 0
  for _ in io.lines(filename) do n = n + 1 end
  L(locheader(), "numlines(", filename, ") = ", n, "\n")
  return n
end

function open_or_err(filename, mode, depth)
  local file, e = io.open(filename, mode)
  if file == nil then
    err("Cannot open file " .. filename .. ": " .. e, depth)
  end
  return file
end

function fsize(filename)
  local file = open_or_err(filename, "r", 3)
  local size = file:seek("end")
  file:close()
  return size
end

function readfile_q(filename)
  local file = open_or_err(filename, "rb", 3)
  local dat = file:read("*a")
  file:close()
  return dat
end

function readfile(filename)
  L(locheader(), "readfile ", filename, "\n")
  return readfile_q(filename)
end

function readstdfile(filename)
  return readfile(testdir.."/"..filename)
end

-- Return all but the first N lines of FILENAME.
-- Note that (unlike readfile()) the result will
-- end with a \n whether or not the file did.
function tailfile(filename, n)
  L(locheader(), "tailfile ", filename, ", ", n, "\n")
  local i = 1
  local t = {}
  for l in io.lines(filename) do
    if i > n then
      table.insert(t, l)
    end
    i = i + 1
  end
  table.insert(t, "")
  return table.concat(t, "\n")
end

function writefile_q(filename, dat)
  local file,e
  if dat == nil then
    file,e = open_or_err(filename, "a+b", 3)
  else
    file,e = open_or_err(filename, "wb", 3)
  end
  if dat ~= nil then
    file:write(dat)
  end
  file:close()
  return true
end

function writefile(filename, dat)
  L(locheader(), "writefile ", filename, "\n")
  return writefile_q(filename, dat)
end

function append(filename, dat)
  L(locheader(), "append to file ", filename, "\n")
  local file,e = open_or_err(filename, "a+", 3)
  file:write(dat)
  file:close()
  return true
end

do
  unlogged_copy = copy_recursive
  copy_recursive = nil
  function copy(from, to)
    L(locheader(), "copy ", from, " -> ", to, "\n")
    local ok, res = unlogged_copy(from, to)
    if not ok then
      L(res, "\n")
      return false
    else
      return true
    end
  end
end

do
  local os_rename = os.rename
  os.rename = nil
  os.remove = nil
  function rename(from, to)
    L(locheader(), "rename ", from, " ", to, "\n")
    if exists(to) and not isdir(to) then
      L("Destination ", to, " exists; removing...\n")
      local ok, res = unlogged_remove(to)
      if not ok then
        L("Could not remove ", to, ": ", res, "\n")
        return false
      end
    end
    local ok,res = os_rename(from, to)
    if not ok then
      L(res, "\n")
      return false
    else
      return true
    end
  end
  function unlogged_rename(from, to)
    if exists(to) and not isdir(to) then
      unlogged_remove(to)
    end
    os_rename(from, to)
  end
  unlogged_remove = remove_recursive
  remove_recursive = nil
  function remove(file)
    L(locheader(), "remove ", file, "\n")
    local ok,res = unlogged_remove(file)
    if not ok then
      L(res, "\n")
      return false
    else
      return true
    end
  end
end


function getstd(name, as)
  if as == nil then as = name end
  local ret = copy(testdir .. "/" .. name, as)
  make_tree_accessible(as)
  return ret
end

function get(name, as)
  if as == nil then as = name end
  return getstd(test.name .. "/" .. name, as)
end

-- include from the main tests directory; there's no reason
-- to want to include from the dir for the current test,
-- since in that case it could just go in the driver file.
function include(name)
  local func, e = loadfile(testdir.."/"..name)
  if func == nil then err(e, 2) end
  setfenv(func, getfenv(2))
  func()
end

function trim(str)
  return string.gsub(str, "^%s*(.-)%s*$", "%1")
end

function getpathof(exe, ext)
  local function gotit(now)
    if test.log == nil then
      logfile:write(exe, " found at ", now, "\n")
    else
      test.log:write(exe, " found at ", now, "\n")
    end
    return now
  end
  local path = os.getenv("PATH")
  local char
  if ostype == "Windows" then
    char = ';'
  else
    char = ':'
  end
  if ostype == "Windows" then
    if ext == nil then ext = ".exe" end
  else
    if ext == nil then ext = "" end
  end
  local now = initial_dir.."/"..exe..ext
  if exists(now) then return gotit(now) end
  for x in string.gmatch(path, "[^"..char.."]*"..char) do
    local dir = string.sub(x, 0, -2)
    if string.find(dir, "[\\/]$") then
      dir = string.sub(dir, 0, -2)
    end
    local now = dir.."/"..exe..ext
    if exists(now) then return gotit(now) end
  end
  if test.log == nil then
    logfile:write("Cannot find ", exe, "\n")
  else
    test.log:write("Cannot find ", exe, "\n")
  end
  return nil
end

function prepare_redirect(fin, fout, ferr)
  local cwd = chdir(".").."/"
  redir = {fin = cwd..fin, fout = cwd..fout, ferr = cwd..ferr}
end
do
  oldspawn = spawn
  function spawn(...)
   if redir == nil then
     return oldspawn(unpack(arg))
   else
     return spawn_redirected(redir.fin, redir.fout, redir.ferr, unpack(arg))
   end
  end
end
function execute(path, ...)
   local pid
   local ret = -1
   pid = spawn(path, unpack(arg))
   redir = nil
   if (pid ~= -1) then ret, pid = wait(pid) end
   return ret
end

function cmd_as_str(cmd_table)
  local str = ""
  for i,x in ipairs(cmd_table) do
    if str ~= "" then str = str .. " " end
    if type(x) == "function" then
      str = str .. "<function>"
    else
      local s = tostring(x)
      if string.find(s, " ") then
        str = str .. '"'..s..'"'
      else
        str = str .. s
      end
    end
  end
  return str
end

function runcmd(cmd, prefix, bgnd)
  if prefix == nil then prefix = "ts-" end
  if type(cmd) ~= "table" then err("runcmd called with bad argument") end
  local local_redir = cmd.local_redirect
  if cmd.local_redirect == nil then
    if type(cmd[1]) == "function" then
      local_redir = true
    else
      local_redir = false
    end
  end
  if bgnd == true and type(cmd[1]) == "string" then local_redir = false end
  L("\nruncmd: ", tostring(cmd[1]), ", local_redir = ", tostring(local_redir), ", requested = ", tostring(cmd.local_redirect))
  local redir
  if local_redir then
    files.stdin = open_or_err(prefix.."stdin", nil, 2)
    files.stdout = open_or_err(prefix.."stdout", "w", 2)
    files.stderr = open_or_err(prefix.."stderr", "w", 2)
  else
    prepare_redirect(prefix.."stdin", prefix.."stdout", prefix.."stderr")
  end
  
  local result
  if cmd.logline ~= nil then
    L(locheader(), cmd.logline, "\n")
  else
    L(locheader(), cmd_as_str(cmd), "\n")
  end

  local oldexec = execute
  if bgnd then
     execute = spawn
  end
  if type(cmd[1]) == "function" then
    result = {pcall(unpack(cmd))}
  elseif type(cmd[1]) == "string" then
     result = {pcall(execute, unpack(cmd))}
  else
     execute = oldexec
    err("runcmd called with bad command table " ..
	"(first entry is a " .. type(cmd[1]) ..")")
 end
 execute = oldexec
  
  if local_redir then
    files.stdin:close()
    files.stdout:close()
    files.stderr:close()
  end
  return unpack(result)
end

function samefile(left, right)
  if left == "-" or right == "-" then 
    err("tests may not rely on standard input") 
  end
  if fsize(left) ~= fsize(right) then
    return false
  else
    local ldat = readfile(left)
    local rdat = readfile(right)
    return ldat == rdat
 end
end

function samefilestd(left, right)
   return samefile(testdir .. "/" .. test.name .. "/" .. left, right)
end

function samelines(f, t)
  local fl = {}
  for l in io.lines(f) do table.insert(fl, l) end
  if not table.getn(fl) == table.getn(t) then
    L(locheader(), string.format("file has %s lines; table has %s\n",
                                 table.getn(fl), table.getn(t)))
    return false
  end
  for i=1,table.getn(t) do
    if fl[i] ~= t[i] then
      if fl[i] then
        L(locheader(), string.format("file[i] = '%s'; table[i] = '%s'\n",
                                     fl[i], t[i]))
      else
        L(locheader(), string.format("file[i] = ''; table[i] = '%s'\n",
                                     t[i]))
      end
      return false
    end
  end
  return true
end

function greplines(f, t)
  local fl = {}
  for l in io.lines(f) do table.insert(fl, l) end
  if not table.getn(fl) == table.getn(t) then
    L(locheader(), string.format("file has %s lines; table has %s\n",
                                 table.getn(fl), table.getn(t)))
    return false
  end
  for i=1,table.getn(t) do
    if not regex.search(t[i], fl[i]) then
      L(locheader(), string.format("file[i] = '%s'; table[i] = '%s'\n",
                                   fl[i], t[i]))
      return false
    end
  end
  return true
end

function grep(...)
  local flags, what, where = unpack(arg)
  local dogrep = function ()
                   if where == nil and string.sub(flags, 1, 1) ~= "-" then
                     where = what
                     what = flags
                     flags = ""
                   end
                   local quiet = string.find(flags, "q") ~= nil
                   local reverse = string.find(flags, "v") ~= nil
                   if not quiet and files.stdout == nil then err("non-quiet grep not redirected") end
                   local out = 1
                   local infile = files.stdin
                   if where ~= nil then infile = open_or_err(where) end
                   for line in infile:lines() do
                     local matched = regex.search(what, line)
                     if reverse then matched = not matched end
                     if matched then
                       if not quiet then files.stdout:write(line, "\n") end
                       out = 0
                     end
                   end
                   if where ~= nil then infile:close() end
                   return out
                 end
  return {dogrep, logline = "grep "..cmd_as_str(arg)}
end

function cat(...)
  local arguments = arg
  local function docat()
    local bsize = 8*1024
    for _,x in ipairs(arguments) do
      local infile
      if x == "-" then
        infile = files.stdin
      else
        infile = open_or_err(x, "rb", 3)
      end
      local block = infile:read(bsize)
      while block do
        files.stdout:write(block)
        block = infile:read(bsize)
      end
      if x ~= "-" then
        infile:close()
      end
    end
    return 0
  end
  return {docat, logline = "cat "..cmd_as_str(arg)}
end

function tail(...)
  local file, num = unpack(arg)
  local function dotail()
    if num == nil then num = 10 end
    local mylines = {}
    for l in io.lines(file) do
      table.insert(mylines, l)
      if table.getn(mylines) > num then
        table.remove(mylines, 1)
      end
    end
    for _,x in ipairs(mylines) do
      files.stdout:write(x, "\n")
    end
    return 0
  end
  return {dotail, logline = "tail "..cmd_as_str(arg)}
end

function sort(file)
  local function dosort(file)
    local infile
    if file == nil then
      infile = files.stdin
    else
      infile = open_or_err(file)
    end
    local lines = {}
    for l in infile:lines() do
      table.insert(lines, l)
    end
    if file ~= nil then infile:close() end
    table.sort(lines)
    for _,l in ipairs(lines) do
      files.stdout:write(l, "\n")
    end
    return 0
  end
  return {dosort, file, logline = "sort "..file}
end

function log_file_contents(filename)
  L(readfile_q(filename), "\n")
end

function pre_cmd(stdin, ident)
  if ident == nil then ident = "ts-" end
  if stdin == true then
    unlogged_copy("stdin", ident .. "stdin")
  elseif type(stdin) == "table" then
    unlogged_copy(stdin[1], ident .. "stdin")
  else
    local infile = open_or_err(ident .. "stdin", "w", 3)
    if stdin ~= nil and stdin ~= false then
      infile:write(stdin)
    end
    infile:close()
  end
  L("stdin:\n")
  log_file_contents(ident .. "stdin")
end

function post_cmd(result, ret, stdout, stderr, ident)
  if ret == nil then ret = 0 end
  if ident == nil then ident = "ts-" end
  L("stdout:\n")
  log_file_contents(ident .. "stdout")
  L("stderr:\n")
  log_file_contents(ident .. "stderr")
  if result ~= ret and ret ~= false then
    err("Check failed (return value): wanted " .. ret .. " got " .. result, 3)
  end

  if stdout == nil then
    if fsize(ident .. "stdout") ~= 0 then
      err("Check failed (stdout): not empty", 3)
    end
  elseif type(stdout) == "string" then
    local realout = open_or_err(ident .. "stdout", nil, 3)
    local contents = realout:read("*a")
    realout:close()
    if contents ~= stdout then
      err("Check failed (stdout): doesn't match", 3)
    end
  elseif type(stdout) == "table" then
    if not samefile(ident .. "stdout", stdout[1]) then
      err("Check failed (stdout): doesn't match", 3)
    end
  elseif stdout == true then
    unlogged_remove("stdout")
    unlogged_rename(ident .. "stdout", "stdout")
  end

  if stderr == nil then
    if fsize(ident .. "stderr") ~= 0 then
      err("Check failed (stderr): not empty", 3)
    end
  elseif type(stderr) == "string" then
    local realerr = open_or_err(ident .. "stderr", nil, 3)
    local contents = realerr:read("*a")
    realerr:close()
    if contents ~= stderr then
      err("Check failed (stderr): doesn't match", 3)
    end
  elseif type(stderr) == "table" then
    if not samefile(ident .. "stderr", stderr[1]) then
      err("Check failed (stderr): doesn't match", 3)
    end
  elseif stderr == true then
    unlogged_remove("stderr")
    unlogged_rename(ident .. "stderr", "stderr")
  end
end

-- std{out,err} can be:
--   * false: ignore
--   * true: ignore, copy to stdout
--   * string: check that it matches the contents
--   * nil: must be empty
--   * {string}: check that it matches the named file
-- stdin can be:
--   * true: use existing "stdin" file
--   * nil, false: empty input
--   * string: contents of string
--   * {string}: contents of the named file

function bg(torun, ret, stdout, stderr, stdin)
  test.bgid = test.bgid + 1
  local out = {}
  out.prefix = "ts-" .. test.bgid .. "-"
  pre_cmd(stdin, out.prefix)
  L("Starting background command...")
  local ok,pid = runcmd(torun, out.prefix, true)
  if not ok then err(pid, 2) end
  if pid == -1 then err("Failed to start background process\n", 2) end
  out.pid = pid
  test.bglist[test.bgid] = out
  out.id = test.bgid
  out.retval = nil
  out.locstr = locheader()
  out.cmd = torun
  out.expret = ret
  out.expout = stdout
  out.experr = stderr
  local mt = {}
  mt.__index = mt
  mt.finish = function(obj, timeout)
                if obj.retval ~= nil then return end
                
                if timeout == nil then timeout = 0 end
                if type(timeout) ~= "number" then
                  err("Bad timeout of type "..type(timeout))
                end
                local res
                obj.retval, res = timed_wait(obj.pid, timeout)
                if (res == -1) then
                  if (obj.retval ~= 0) then
                    L(locheader(), "error in timed_wait ", obj.retval, "\n")
                  end
                  kill(obj.pid, 15) -- TERM
                  obj.retval, res = timed_wait(obj.pid, 2)
                  if (res == -1) then
                    kill(obj.pid, 9) -- KILL
                    obj.retval, res = timed_wait(obj.pid, 2)
                  end
                end
                
                test.bglist[obj.id] = nil
                L(locheader(), "checking background command from ", out.locstr,
		  cmd_as_str(out.cmd), "\n")
                post_cmd(obj.retval, out.expret, out.expout, out.experr, obj.prefix)
                return true
              end
  mt.wait = function(obj, timeout)
              if obj.retval ~= nil then return end
              if timeout == nil then
                obj.retval = wait(obj.pid)
              else
                local res
                obj.retval, res = timed_wait(obj.pid, timeout)
                if res == -1 then
                  obj.retval = nil
                  return false
                end
              end
              test.bglist[obj.id] = nil
              L(locheader(), "checking background command from ", out.locstr,
                table.concat(out.cmd, " "), "\n")
              post_cmd(obj.retval, out.expret, out.expout, out.experr, obj.prefix)
              return true
            end
  return setmetatable(out, mt)
end

function runcheck(cmd, ret, stdout, stderr, stdin)
  if ret == nil then ret = 0 end
  pre_cmd(stdin)
  local ok, result = runcmd(cmd)
  if ok == false then
    err(result, 2)
  end
  post_cmd(result, ret, stdout, stderr)
  return result
end

function indir(dir, what)
  if type(what) ~= "table" then
    err("bad argument of type "..type(what).." to indir()")
  end
  local function do_indir()
    local savedir = chdir(dir)
    if savedir == nil then
      err("Cannot chdir to "..dir)
    end
    local ok, res
    if type(what[1]) == "function" then
      ok, res = pcall(unpack(what))
    elseif type(what[1]) == "string" then
      ok, res = pcall(execute, unpack(what))
    else
      err("bad argument to indir(): cannot execute a "..type(what[1]))
    end
    chdir(savedir)
    if not ok then err(res) end
    return res
  end
  local want_local
  if type(what[1]) == "function" then
    if type(what.local_redirect) == "nil" then
      want_local = true
    else
      want_local = what.local_redirect
    end
  else
    want_local = false
  end
  local ll = "In directory "..dir..": "
  if what.logline ~= nil then ll = ll .. tostring(what.logline)
  else
    ll = ll .. cmd_as_str(what)
  end
  return {do_indir, local_redirect = want_local, logline = ll}
end

function check(first, ...)
  if type(first) == "table" then
    return runcheck(first, unpack(arg))
  elseif type(first) == "boolean" then
    if not first then err("Check failed: false", 2) end
  elseif type(first) == "number" then
    if first ~= 0 then
      err("Check failed: " .. first .. " ~= 0", 2)
    end
  else
    err("Bad argument to check() (" .. type(first) .. ")", 2)
  end
  return first
end

function skip_if(chk)
  if chk then
    err(true, 2)
  end
end

function xfail_if(chk, ...)
  local ok,res = pcall(check, unpack(arg))
  if ok == false then
    if chk then err(false, 2) else err(err, 2) end
  else
    if chk then
      test.wanted_fail = true
      L("UNEXPECTED SUCCESS\n")
    end
  end
end

function xfail(...)
   xfail_if(true, unpack(arg))
end

function log_error(e)
  if type(e) == "table" then
    L("\n", tostring(e.e), "\n")
    for i,bt in ipairs(e.bt) do
      if i ~= 1 then L("Rethrown from:") end
      L(bt)
    end
  else
    L("\n", tostring(e), "\n")
  end
end

function run_tests(debugging, list_only, run_dir, logname, args, progress)
  local torun = {}
  local run_all = true

  local function P(...)
     progress(unpack(arg))
     logfile:write(unpack(arg))
  end

  -- NLS nuisances.
  for _,name in pairs({  "LANG",
			 "LANGUAGE",
			 "LC_ADDRESS",
			 "LC_ALL",
			 "LC_COLLATE",
			 "LC_CTYPE",
			 "LC_IDENTIFICATION",
			 "LC_MEASUREMENT",
			 "LC_MESSAGES",
			 "LC_MONETARY",
			 "LC_NAME",
			 "LC_NUMERIC",
			 "LC_PAPER",
			 "LC_TELEPHONE",
			 "LC_TIME"  }) do
     set_env(name,"C")
  end

  -- no test suite should touch the user's ssh agent
  unset_env("SSH_AUTH_SOCK")

  logfile = io.open(logname, "w")
  chdir(run_dir);

  do
     local s = prepare_to_enumerate_tests(P)
     if s ~= 0 then
	P("Enumeration of tests failed.\n")
	return s
     end
  end

  -- testdir is set by the testsuite definition
  -- any directory in testdir with a __driver__.lua inside is a test case
  local tests = {}
  for _,candidate in ipairs(read_directory(testdir)) do
     -- n.b. it is not necessary to throw out directories before doing
     -- this check, because exists(nondirectory/__driver__.lua) will
     -- never be true.
     if exists(testdir .. "/" .. candidate .. "/__driver__.lua") then
	table.insert(tests, candidate)
     end
  end
  table.sort(tests)

  for i,a in pairs(args) do
    local _1,_2,l,r = string.find(a, "^(-?%d+)%.%.(-?%d+)$")
    if _1 then
      l = l + 0
      r = r + 0
      if l < 1 then l = table.getn(tests) + l + 1 end
      if r < 1 then r = table.getn(tests) + r + 1 end
      if l > r then l,r = r,l end
      for j = l,r do
        torun[j] = tests[j]
      end
      run_all = false
    elseif string.find(a, "^-?%d+$") then
      r = a + 0
      if r < 1 then r = table.getn(tests) + r + 1 end
      torun[r] = tests[r]
      run_all = false
    else
      -- pattern
      run_all = false
      local matched = false
      for i,t in pairs(tests) do
        if regex.search(a, t) then
          torun[i] = t
          matched = true
        end
      end
      if not matched then
        print(string.format("Warning: pattern '%s' does not match any tests.", a))
      end
    end
  end

  if run_all then torun = tests end

  if list_only then
    for i,t in pairs(torun) do
      if i < 10 then P(" ") end
      if i < 100 then P(" ") end
      P(i .. " " .. t .. "\n")
    end
    logfile:close()
    return 0
  end

  logfile:write("Running on ", get_ostype(), "\n\n")
  local s = prepare_to_run_tests(P)
  if s ~= 0 then
    P("Test suite preparation failed.\n")
    return s 
  end
  P("Running tests...\n")

  local counts = {}
  counts.success = 0
  counts.skip = 0
  counts.xfail = 0
  counts.noxfail = 0
  counts.fail = 0
  counts.total = 0
  counts.of_interest = 0
  local of_interest = {}
  local failed_testlogs = {}

  -- exit codes which indicate failure at a point in the process-spawning
  -- code where it is impossible to give more detailed diagnostics
  local magic_exit_codes = {
     [121] = "error creating test directory",
     [122] = "error spawning test process",
     [123] = "error entering test directory",
     [124] = "unhandled exception in child process"
  }

  -- callback closure passed to run_tests_in_children
  local function report_one_test(tno, tname, status)
     local tdir = run_dir .. "/" .. tname
     local test_header = string.format("%3d %-45s ", tno, tname)
     local what
     local can_delete
     -- the child should always exit successfully, just to avoid
     -- headaches.  if we get any other code we report it as a failure.
     if status ~= 0 then
	if status < 0 then
	   what = string.format("FAIL (signal %d)", -status)
	elseif magic_exit_codes[status] ~= nil then
	   what = string.format("FAIL (%s)", magic_exit_codes[status])
	else
	   what = string.format("FAIL (exit %d)", status)
	end
     else
	local wfile, err = io.open(tdir .. "/STATUS", "r")
	if wfile ~= nil then
	   what = string.gsub(wfile:read("*a"), "\n$", "")
	   wfile:close()
	else
	   what = string.format("FAIL (status file: %s)", err)
	end
     end
     if what == "unexpected success" then
	counts.noxfail = counts.noxfail + 1
	counts.of_interest = counts.of_interest + 1
	table.insert(of_interest, test_header .. "unexpected success")
	can_delete = false
     elseif what == "partial skip" or what == "ok" then
	counts.success = counts.success + 1
	can_delete = true
     elseif string.find(what, "skipped ") == 1 then
	counts.skip = counts.skip + 1
	can_delete = true
     elseif string.find(what, "expected failure ") == 1 then
	counts.xfail = counts.xfail + 1
	can_delete = false
     elseif string.find(what, "FAIL ") == 1 then
	counts.fail = counts.fail + 1
	table.insert(of_interest, test_header .. what)
	table.insert(failed_testlogs, tdir .. "/tester.log")
	can_delete = false
     else
	counts.fail = counts.fail + 1
	what = "FAIL (gobbledygook: " .. what .. ")"
	table.insert(of_interest, test_header .. what)
	table.insert(failed_testlogs, tdir .. "/tester.log")
	can_delete = false
     end

     counts.total = counts.total + 1
     P(string.format("%s%s\n", test_header, what))
     return (can_delete and not debugging)
  end

  run_tests_in_children(torun, report_one_test)

  if counts.of_interest ~= 0 and (counts.total / counts.of_interest) > 4 then
   P("\nInteresting tests:\n")
   for i,x in ipairs(of_interest) do
     P(x, "\n")
   end
  end
  P("\n")

  for i,log in pairs(failed_testlogs) do
    local tlog = io.open(log, "r")
    if tlog ~= nil then
      local dat = tlog:read("*a")
      tlog:close()
      logfile:write("\n", string.rep("*", 50), "\n")
      logfile:write(dat)
    end
  end

  -- Write out this summary in one go so that it does not get interrupted
  -- by concurrent test suites' summaries.
  P(string.format("Of %i tests run:\n"..
		  "\t%i succeeded\n"..
		  "\t%i failed\n"..
		  "\t%i had expected failures\n"..
		  "\t%i succeeded unexpectedly\n"..
		  "\t%i were skipped\n",
		  counts.total, counts.success, counts.fail,
		  counts.xfail, counts.noxfail, counts.skip))

  logfile:close()
  if counts.success + counts.skip + counts.xfail == counts.total then
    return 0
  else
    return 1
  end
end

function run_one_test(tname)
   test.bgid = 0
   test.name = tname
   test.wanted_fail = false
   test.partial_skip = false
   test.root = chdir(".")
   test.errfile = ""
   test.errline = -1
   test.bglist = {}
   test.log = io.open("tester.log", "w")

   -- Sanitize $HOME.  This is done here so that each test gets its
   -- very own empty directory (in case some test writes stuff inside).
   unlogged_mkdir("emptyhomedir")
   test.home = test.root .. "/emptyhomedir"
   if ostype == "Windows" then
      set_env("APPDATA", test.home)
   else
      set_env("HOME", test.home)
   end

   L("Test ", test.name, "\n")

   local driverfile = testdir .. "/" .. test.name .. "/__driver__.lua"
   local driver, e = loadfile(driverfile)
   local r
   if driver == nil then
      r = false
      e = "Could not load driver file " .. driverfile .. ".\n" .. e
   else
      local oldmask = posix_umask(0)
      posix_umask(oldmask)
      r,e = xpcall(driver, debug.traceback)
      local errline = test.errline
      for i,b in pairs(test.bglist) do
	 local a,x = pcall(function () b:finish(0) end)
	 if r and not a then
	    r = a
	    e = x
	 elseif not a then
	    L("Error cleaning up background processes: ",
	      tostring(b.locstr), "\n")
	 end
      end
      if type(cleanup) == "function" then
	 local a,b = pcall(cleanup)
	 if r and not a then
	    r = a
	    e = b
	 end
      end
      test.errline = errline
      posix_umask(oldmask)
   end

   if not r then
      if test.errline == nil then test.errline = -1 end
      if type(e) ~= "table" then
	 local tbl = {e = e, bt = {"no backtrace; type(err) = "..type(e)}}
	 e = tbl
      end
      if type(e.e) ~= "boolean" then
	 log_error(e)
      end
   end
   test.log:close()
    
   -- record the short status where report_one_test can find it
   local s = io.open(test.root .. "/STATUS", "w")
   if r then
      if test.wanted_fail then
	 s:write("unexpected success\n")
      else
	 if test.partial_skip then
	    s:write("partial skip\n")
	 else
	    s:write("ok\n")
	 end
      end
   else
      if e.e == true then
	 s:write(string.format("skipped (line %i)\n", test.errline))
      elseif e.e == false then
	 s:write(string.format("expected failure (line %i)\n",
			       test.errline))
      else
	 s:write(string.format("FAIL (line %i)\n", test.errline))
      end
   end
   s:close()
   return 0
end
