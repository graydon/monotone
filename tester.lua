tests = {}
srcdir = get_source_dir()
test_root = nil
testname = nil

logfile = io.open("tester.log", "w") -- combined logfile
test_log = nil -- logfile for this test
failed_testlogs = {}

function fsize(filename)
  local file = io.open(filename, "r")
  if file == nil then error("Cannot open file " .. filename, 2) end
  local size = file:seek("end")
  file:close()
  return size
end

function readfile(filename)
  local file = io.open(filename, "rb")
  if file == nil then error("Cannot open file " .. filename, 2) end
  local dat = file:read("*a")
  file:close()
  return dat
end

function writefile(filename, dat)
  local file = io.open(filename, "wb")
  if file == nil then error("Cannot open file " .. filename, 2) end
  file:write(dat)
  file:close()
  return
end

function getstdfile(name, as)
  local infile = io.open(srcdir .. "/" .. name, "rb")
  if infile == nil then
    error("Cannot open file " .. srcdir .. "/" .. name, 2)
  end
  local outfile = io.open(as, "wb")
  if outfile == nil then
    infile:close()
    error("Cannot open file " .. as, 2)
  end
  local size = 2^13
  while true do
    local block = infile:read(size)
    if not block then break end
    outfile:write(block)
  end
  infile:close()
  outfile:close()
end

function getfile(name, as)
  if as == nil then as = name end
  getstdfile(testname .. "/" .. name, as)
end

function execute(path, ...)   
   local pid
   local ret = -1
   pid = spawn(path, unpack(arg))
   if (pid ~= -1) then ret, pid = wait(pid) end
   return ret
end

function cmd(first, ...)
  if type(first) == "string" then
    test_log:write("\n", first, " ", table.concat(arg, " "), "\n")
    return function () return execute(first, unpack(arg)) end
  elseif type(first) == "function" then
    test_log:write("\n<function> ", table.concat(arg, " "), "\n")
    return function () return first(unpack(arg)) end
  else
    error("cmd() called with argument of unknown type " .. type(first), 2)
  end
end

function cmp(left, right)
  local docmp = function (left, right)
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
                      return 1
                    else
                      ldat = readfile(left)
                      rdat = readfile(right)
                    end
                  end
                  if ldat == rdat then return 0 else return 1 end
                end
  return docmp, left, right
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

-- std{out,err} can be:
--   * false: ignore
--   * true: ignore, copy to stdout
--   * string: check that it matches the contents
--   * nil: must be empty
-- stdin can be:
--   * true: use existing "stdin" file
--   * nil, false: empty input
--   * string: contents of string
function check(func, ret, stdout, stderr, stdin)
  if ret == nil then ret = 0 end
  if stdin ~= true then
    local infile = io.open("stdin", "w")
    if stdin ~= nil and stdin ~= false then
      infile:write(stdin)
    end
    infile:close()
  end
  os.remove("ts-stdin")
  os.rename("stdin", "ts-stdin")
  local i, o, e = set_redirect("ts-stdin", "ts-stdout", "ts-stderr")
  local ok, result = pcall(func)
  clear_redirect(i, o, e)
  if ok == false then
    error(result, 2)
  end
  if result ~= ret then
    error("Check failed (return value): wanted " .. ret .. " got " .. result, 2)
  end

  if stdout == nil then
    if fsize("ts-stdout") ~= 0 then
      error("Check failed (stdout): not empty", 2)
    end
  elseif type(stdout) == "string" then
    local realout = io.open("stdout")
    local contents = realout:read("*a")
    realout:close()
    if contents ~= stdout then
      error("Check failed (stdout): doesn't match", 2)
    end
  elseif stdout == true then
    os.remove("stdout")
    os.rename("ts-stdout", "stdout")
  end

  if stderr == nil then
    if fsize("ts-stderr") ~= 0 then
      error("Check failed (stderr): not empty", 2)
    end
  elseif type(stderr) == "string" then
    local realerr = io.open("stderr")
    local contents = realerr:read("*a")
    realerr:close()
    if contents ~= stderr then
      error("Check failed (stderr): doesn't match", 2)
    end
  elseif stderr == true then
    os.remove("stderr")
    os.rename("ts-stderr", "stderr")
  end
end

function P(...)
  io.write(unpack(arg))
  io.flush()
  logfile:write(unpack(arg))
end

function run_tests(args)
  print("Args:")
  for i,a in pairs(args) do
    print ("\t", i, a)
  end
  P("Running tests...\n")
  local failed = 0
  for i,t in pairs(tests) do
    testname = t
    local shortname = nil
    test_root, shortname = go_to_test_dir(t)
    if i < 100 then P(" ") end
    if i < 10 then P(" ") end
    P(i .. " " .. shortname)
    local spacelen = 60 - string.len(shortname)
    local spaces = string.rep("          ", 7)
    if spacelen > 0 then P(string.sub(spaces, 1, spacelen)) end

    local tlog = test_root .. "/tester.log"
    test_log = io.open(tlog, "w")
    test_log:write("Test number ", i, ", ", shortname, "\n")

    local driverfile = srcdir .. "/" .. testname .. "/__driver__.lua"
    local driver, e = loadfile(driverfile)
    local r
    if driver == nil then
      r = false
      e = "Could not load driver file " .. driverfile .. " .\n" .. e
    else
      r,e = xpcall(driver, debug.traceback)
    end
    if r then
      P("ok\n")
      clean_test_dir(testname)
    else
      P("FAIL\n")
      test_log:write("\n", e, "\n")
      failed = failed + 1
      table.insert(failed_testlogs, tlog)
      leave_test_dir()
    end
    test_log:close()
  end

  for i,log in pairs(failed_testlogs) do
    local tlog = io.open(log, "r")
    if tlog ~= nil then
      local dat = tlog:read("*a")
      tlog:close()
      logfile:write(dat)
    end
  end

  if failed == 0 then return 0 else return 1 end
end
