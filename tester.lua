tests = {}
srcdir = get_source_dir()
test_root = nil
testname = nil

function getfile(name)
  local infile = io.open(srcdir .. "/" .. testname .. "/" .. name, "rb")
  local outfile = io.open(name, "wb")
  local size = 2^13
  while true do
    local block = infile:read(size)
    if not block then break end
    outfile:write(block)
  end
  infile:close()
  outfile:close()
end

function execute(path, ...)   
   local pid
   local ret = -1
   pid = spawn(path, unpack(arg))
   if (pid ~= -1) then ret, pid = wait(pid) end
   return ret
end

function prepare(...)
  return function () return execute(unpack(arg)) end
end

function check(func, ret, stdout, stderr, stdin)
  if ret == nil then ret = 0 end
  -- local i, o, e = set_redirect("stdin", "stdout", "stderr")
  local result = func()
  -- clear_redirect(i, o, e)
  if result ~= ret then
    error("Check failed: wanted " .. ret .. " got " .. result, 2)
  end
end

function run_tests(args)
  print("Args:")
  for i,a in pairs(args) do
    print ("\t", i, a)
  end
  print("Running tests...")
  for i,t in pairs(tests) do
    testname = t
    io.write(i .. "\t" .. testname .. "\t")
    test_root = go_to_test_dir(testname)
    local driver = srcdir .. "/" .. testname .. "/__driver__.lua"
    local r,e = xpcall(loadfile(driver), debug.traceback)
    if r then
      io.write("OK.\n")
    else
      io.write("Test failed: " .. e .. "\n")
    end
  end
  return 0
end
