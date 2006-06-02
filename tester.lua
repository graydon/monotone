tests = {}
srcdir = get_source_dir()
debugging = false

test_root = nil
testname = nil
wanted_fail = false
partial_skip = false -- set this to true if you skip part of the test

errfile = ""
errline = -1

logfile = io.open("tester.log", "w") -- combined logfile
test_log = nil -- logfile for this test
failed_testlogs = {}
bgid = 0
bglist = {}

function P(...)
  io.write(unpack(arg))
  io.flush()
  logfile:write(unpack(arg))
end

function L(...)
  test_log:write(unpack(arg))
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
      return testname, info.currentline
    end
  end
end

function locheader()
  local _,line = getsrcline()
  if line == nil then line = -1 end
  if testname == nil then
    return "\n<unknown>:" .. line .. ": "
  else
    return "\n" .. testname .. ":" .. line .. ": "
  end
end

function err(...)
    errfile,errline = getsrcline()
    error(unpack(arg))
end

old_mkdir = mkdir
mkdir = function(name)
  L(locheader(), "mkdir ", name, "\n")
  old_mkdir(name)
end

old_existsonpath = existsonpath
existsonpath = function(name)
  local r = (old_existsonpath(name) == 0)
  local what
  if r then
    what = "exists"
  else
    what = "does not exist"
  end
  L(locheader(), name, " ", what, " on the path\n")
  return r
end

function fsize(filename)
  local file = io.open(filename, "r")
  if file == nil then error("Cannot open file " .. filename, 2) end
  local size = file:seek("end")
  file:close()
  return size
end

function readfile_q(filename)
  local file = io.open(filename, "rb")
  if file == nil then
    error("Cannot open file " .. filename)
  end
  local dat = file:read("*a")
  file:close()
  return dat
end

function readfile(filename)
  L(locheader(), "readfile ", filename, "\n")
  return readfile_q(filename)
end

function writefile_q(filename, dat)
  local file,e
  if dat == nil then
    file,e = io.open(filename, "a+b")
  else
    file,e = io.open(filename, "wb")
  end
  if file == nil then
    L("Cannot open file ", filename, ": ", e, "\n")
    return false
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

function copyfile(from, to)
  L(locheader(), "copyfile ", from, " ", to, "\n")
  local infile = io.open(from, "rb")
  if infile == nil then
    L("Cannot open file " .. from)
    return false
  end
  local outfile = io.open(to, "wb")
  if outfile == nil then
    infile:close()
    L("Cannot open file " .. to)
    return false
  end
  local size = 2^13
  while true do
    local block = infile:read(size)
    if not block then break end
    outfile:write(block)
  end
  infile:close()
  outfile:close()
  return true
end

function rename(from, to)
  L(locheader(), "rename ", from, " ", to, "\n")
  local ok,res = os.rename(from, to)
  if ok == nil then
    L(res, "\n")
    return false
  else
    return true
  end
end

function remove(file)
  L(locheader(), "remove ", file, "\n")
  local ok,res = os.remove(file)
  if ok == nil then
    L(res, "\n")
    return false
  else
    return true
  end
end

function rename_over(from, to)
  remove(to)
  return rename(from, to)
end

function getstdfile(name, as)
  copyfile(srcdir .. "/" .. name, as)
end

function getfile(name, as)
  if as == nil then as = name end
  getstdfile(testname .. "/" .. name, as)
end

function trim(str)
  return string.gsub(str, "^%s*(.-)%s*$", "%1")
end

function execute(path, ...)   
   local pid
   local ret = -1
   pid = spawn(path, unpack(arg))
   if (pid ~= -1) then ret, pid = wait(pid) end
   return ret
end

function background(path, ...)
  local ret = {}
  local pid = spawn(path, unpack(arg))
  if (pid == -1) then return false end
  ret.pid = pid
  local mt = {}
  mt.__index = mt
  mt.finish = function (obj, timeout)
                if timeout == nil then timeout = 0 end
                local ret, res = timed_wait(obj.pid, timeout)
                if (res == -1) then
                  kill(obj.pid, 15) -- TERM
                  ret, res = timed_wait(obj.pid, 2)
                  if (res == -1) then
                    kill(obj.pid, 9) -- KILL
                    ret, res = timed_wait(obj.pid, 2)
                  end
                end
                return ret
              end
  mt.wait = function (obj)
              local ret,_ = wait(obj.pid)
              return ret
            end
  return setmetatable(ret, mt)
end

function cmd(first, ...)
  if type(first) == "string" then
    L(locheader(), first, " ", table.concat(arg, " "), "\n")
    return function () return execute(first, unpack(arg)) end
  elseif type(first) == "function" then
    local info = debug.getinfo(first)
    local name
    if info.name ~= nil then
      name  = info.name
    else
      name = "<function>"
    end
    L(locheader(), name, " ", table.concat(arg, " "), "\n")
    return function () return first(unpack(arg)) end
  else
    error("cmd() called with argument of unknown type " .. type(first), 2)
  end
end

function samefile(left, right)
  local ldat = nil
  local rdat = nil
  if left == "-" then
    ldat = io.input:read("*a")
    rdat = readfile(right)
  elseif right == "-" then
    rdat = io.input:read("*a")
    ldat = readfile(left)
  else
    if fsize(left) ~= fsize(right) then
      return false
    else
      ldat = readfile(left)
      rdat = readfile(right)
    end
  end
  return ldat == rdat
end

function grep(...)
  local dogrep = function (flags, what, where)
                   if where == nil and string.sub(flags, 1, 1) ~= "-" then
                     where = what
                     what = flags
                     flags = ""
                   end
                   local quiet = string.find(flags, "q") ~= nil
                   local reverse = string.find(flags, "v") ~= nil
                   local out = 1
                   for line in io.lines(where) do
                     local matched = regex.search(what, line)
                     if reverse then matched = not matched end
                     if matched then
                       if not quiet then print(line) end
                       out = 0
                     end
                   end
                   return out
                 end
  return dogrep, unpack(arg)
end

function log_file_contents(filename)
  L(readfile_q(filename))
end

function pre_cmd(stdin, ident)
  if ident == nil then ident = "ts-" end
  if stdin ~= true then
    local infile = io.open("stdin", "w")
    if stdin ~= nil and stdin ~= false then
      infile:write(stdin)
    end
    infile:close()
  end
  os.remove(ident .. "stdin")
  os.rename("stdin", ident .. "stdin")
  L("stdin:\n")
  log_file_contents(ident .. "stdin")
  return set_redirect(ident .. "stdin", ident .. "stdout", ident .. "stderr")
end

function post_cmd(result, ret, stdout, stderr, ident)
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
    local realout = io.open(ident .. "stdout")
    local contents = realout:read("*a")
    realout:close()
    if contents ~= stdout then
      err("Check failed (stdout): doesn't match", 3)
    end
  elseif stdout == true then
    os.remove("stdout")
    os.rename(ident .. "stdout", "stdout")
  end

  if stderr == nil then
    if fsize(ident .. "stderr") ~= 0 then
      err("Check failed (stderr): not empty", 3)
    end
  elseif type(stderr) == "string" then
    local realerr = io.open(ident .. "stderr")
    local contents = realerr:read("*a")
    realerr:close()
    if contents ~= stderr then
      err("Check failed (stderr): doesn't match", 3)
    end
  elseif stderr == true then
    os.remove("stderr")
    os.rename(ident .. "stderr", "stderr")
  end
end

-- std{out,err} can be:
--   * false: ignore
--   * true: ignore, copy to stdout
--   * string: check that it matches the contents
--   * nil: must be empty
-- stdin can be:
--   * true: use existing "stdin" file
--   * nil, false: empty input
--   * string: contents of string

function bg(torun, ret, stdout, stderr, stdin)
  bgid = bgid + 1
  local out = {}
  out.prefix = "ts-" .. bgid .. "-"
  local redir = pre_cmd(stdin, out.prefix)
  out.process = background(unpack(torun))
  redir:restore()
  if out.process == false then
    err("Failed to start background process\n", 2)
  end
  bglist[bgid] = out
  out.id = bgid
  out.retval = nil
  out.locstr = locheader()
  out.cmd = torun
  out.expret = ret
  out.expout = stdout
  out.experr = stderr
  L(out.locstr, "starting background command: ", table.concat(out.cmd, " "), "\n")
  local mt = {}
  mt.__index = mt
  mt.finish = function(obj, timeout)
                if obj.retval ~= nil then return end
                obj.retval = obj.process:finish(timeout)
                table.remove(bglist, obj.id)
                L(locheader(), "checking background command from ", out.locstr,
                  table.concat(out.cmd, " "))
                post_cmd(obj.retval, out.expret, out.expout, out.experr, obj.prefix)
              end
  mt.wait = function(obj)
              if obj.retval ~= nil then return end
              obj.retval = obj.process:wait()
              table.remove(bglist, obj.id)
              L(locheader(), "checking background command from ", out.locstr,
                table.concat(out.cmd, " "), "\n")
              post_cmd(obj.retval, out.expret, out.expout, out.experr, obj.prefix)
            end
  return setmetatable(out, mt)
end

function check_func(func, ret, stdout, stderr, stdin)
  if ret == nil then ret = 0 end
  local redir = pre_cmd(stdin)
  local ok, result = pcall(func)
  redir:restore()
  if ok == false then
    err(result, 2)
  end
  post_cmd(result, ret, stdout, stderr)
  return result
end

function check(first, ...)
  if type(first) == "function" then
    return check_func(first, unpack(arg))
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
      wanted_fail = true
      L("UNEXPECTED SUCCESS\n")
    end
  end
end

function run_tests(args)
  local torun = {}
  local run_all = true
  local list_only = false
  for i,a in pairs(args) do
    local _1,_2,l,r = string.find(a, "^(-?%d+)%.%.(-?%d+)$")
    if _1 then
      l = l + 0
      r = r + 0
      if l < 1 then l = table.getn(tests) + l + 1 end
      if r < 1 then r = table.getn(tests) + r + 1 end
      if l > r then l,r = r,l end
      for j = l,r do
        torun[j]=j
      end
      run_all = false
    elseif string.find(a, "^-?%d+$") then
      r = a + 0
      if r < 1 then r = table.getn(tests) + r + 1 end
      torun[r] = r
      run_all = false
    elseif a == "-d" then
      debugging = true
    elseif a == "-l" then
      list_only = true
    else
      -- pattern
      local matched = false
      for i,t in pairs(tests) do
        if regex.search(a, t) then
          torun[i] = i
          matched = true
        end
      end
      if matched then
        run_all = false
      else
        print(string.format("Warning: pattern '%s' does not match any tests.", a))
      end
    end
  end
  if not list_only then P("Running tests...\n") end
  local counts = {}
  counts.success = 0
  counts.skip = 0
  counts.xfail = 0
  counts.noxfail = 0
  counts.fail = 0
  counts.total = 0

  local function runtest(i, tname)
    bgid = 0
    testname = tname
    wanted_fail = false
    partial_skip = false
    local shortname = nil
    test_root, shortname = go_to_test_dir(testname)
    errfile = ""
    errline = -1
    
    if i < 100 then P(" ") end
    if i < 10 then P(" ") end
    P(i .. " " .. shortname)
    local spacelen = 46 - string.len(shortname)
    local spaces = string.rep(" ", 50)
    if spacelen > 0 then P(string.sub(spaces, 1, spacelen)) end

    local tlog = test_root .. "/tester.log"
    test_log = io.open(tlog, "w")
    L("Test number ", i, ", ", shortname, "\n")

    local driverfile = srcdir .. "/" .. testname .. "/__driver__.lua"
    local driver, e = loadfile(driverfile)
    local r
    if driver == nil then
      r = false
      e = "Could not load driver file " .. driverfile .. " .\n" .. e
    else
      bglist = {}
      r,e = xpcall(driver, debug.traceback)
      for i,b in pairs(bglist) do
        b:finish(0)
      end
      restore_env()
    end
    if r then
      if wanted_fail then
        P("unexpected success\n")
        test_log:close()
        leave_test_dir()
        counts.noxfail = counts.noxfail + 1
      else
        if partial_skip then
          P("partial skip\n")
        else
          P("ok\n")
        end
        test_log:close()
        if not debugging then clean_test_dir(testname) end
        counts.success = counts.success + 1
      end
    else
      if e == true then
        P(string.format("skipped (line %i)\n", errline))
        test_log:close()
        if not debugging then clean_test_dir(testname) end
        counts.skip = counts.skip + 1
      elseif e == false then
        P(string.format("expected failure (line %i)\n", errline))
        test_log:close()
        leave_test_dir()
        counts.xfail = counts.xfail + 1
      else
        P(string.format("FAIL (line %i)\n", errline))
        test_log:write("\n", e, "\n")
        table.insert(failed_testlogs, tlog)
        test_log:close()
        leave_test_dir()
        counts.fail = counts.fail + 1
      end
    end
    counts.total = counts.total + 1
  end

  save_env()
  if run_all then
    for i,t in pairs(tests) do
      if list_only then
        if i < 10 then P(" ") end
        if i < 100 then P(" ") end
        P(i .. " " .. t .. "\n")
      else
        runtest(i, t)
      end
    end
  else
    for i,_ in pairs(torun) do
      if list_only then
        if i < 10 then P(" ") end
        if i < 100 then P(" ") end
        P(i .. " " .. tests[i] .. "\n")
      else
        runtest(i, tests[i])
      end
    end
  end
  
  if list_only then
    logfile:close()
    return 0
  end
  
  P("\n")
  P(string.format("Of %i tests run:\n", counts.total))
  P(string.format("\t%i succeeded\n", counts.success))
  P(string.format("\t%i failed\n", counts.fail))
  P(string.format("\t%i had expected failures\n", counts.xfail))
  P(string.format("\t%i succeeded unexpectedly\n", counts.noxfail))
  P(string.format("\t%i were skipped\n", counts.skip))

  for i,log in pairs(failed_testlogs) do
    local tlog = io.open(log, "r")
    if tlog ~= nil then
      local dat = tlog:read("*a")
      tlog:close()
      logfile:write("\n", string.rep("*", 50), "\n")
      logfile:write(dat)
    end
  end
  logfile:close()

  if counts.success + counts.skip + counts.xfail == counts.total then
    return 0
  else
    return 1
  end
end
