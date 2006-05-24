#!./tester

function safe_mtn(...)
  return "mtn", "--norc", "--root=" .. test_root, unpack(arg)
end

-- function preexecute(...)
--   return "valgrind", "--tool=memcheck", unpack(arg)
-- end

function raw_mtn(...)
  if preexecute ~= nil then
    return preexecute(safe_mtn(unpack(arg)))
  else
    return safe_mtn(unpack(arg))
  end
end

function mtn(...)
  return raw_mtn("--rcfile", test_root .. "/test_hooks.lua",
         "--nostd", "--db=" .. test_root .. "/test.db",
         "--keydir", test_root .. "/keys",
         "--key=tester@test.net", unpack(arg))
end

function commit(branch)
  if branch == nil then branch = "testbranch" end
  return mtn("commit", "--message=blah-blah", "--branch", branch)
end

function sha1(what)
  return safe_mtn("identify", what)
end

function mtn_setup()
  getstdfile("tests/test_keys", "test_keys")
  getstdfile("tests/test_hooks.lua", "test_hooks.lua")
  getstdfile("tests/min_hooks.lua", "min_hooks.lua")
  port = math.random(20000, 50000)
  
  check(cmd(mtn("db", "init")), 0, false, false)
  check(cmd(mtn("read", "test_keys")), 0, false, false)
  check(cmd(mtn("setup", "--branch=testbranch", ".")), 0, false, false)
  os.remove("test_keys")
end

function base_revision()
  return string.gsub(readfile("_MTN/revision"), "%s*$", "")
end

function qgrep(what, where)
  return grep("-q", what, where)
end

function canonicalize(filename)
  local func = function (fn)
                 local f = io.open(filename, "rb")
                 local indat = f:read("*a")
                 f:close()
                 local outdat = string.gsub(indat, "\r\n", "\n")
                 f = io.open(filename, "wb")
                 f:write(outdat)
                 f:close()
                 return 0
               end
  local nullfunc = function (fn) return 0 end
  local ostype = os.getenv("OSTYPE")
  local osenv = os.getenv("OS")
  if osenv ~= nil then osenv = string.find(osenv, "[Ww]in") end
  if ostype == "msys" or osenv then
    return func, filename
  else
    return nullfunc, filename
  end
end

------------------------------------------------------------------------
--====================================================================--
------------------------------------------------------------------------

table.insert(tests, "tests/basic_invocation_and_options")
table.insert(tests, "tests/scanning_trees")
table.insert(tests, "tests/importing_a_file")
table.insert(tests, "tests/generating_and_extracting_keys_and_certs")
