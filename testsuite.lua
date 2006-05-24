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
  check(cmd(mtn("commit", "--message=blah-blah", "--branch", branch)), 0, false, false)
end

function sha1(what)
  check(cmd(safe_mtn("identify", what)), 0, false, false)
  return trim(readfile("ts-stdout"))
end

function probe_node(filename, rsha, fsha)
  remove_recursive("_MTN.old")
  os.rename("_MTN", "_MTN.old")
  os.remove(filename)
  check(cmd(mtn("checkout", "--revision", rsha, ".")), 0, false)
  os.rename("_MTN.old/options", "_MTN")
  check(base_revision() == rsha)
  check(sha1(filename) == fsha)
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
  return cmd(grep("-q", what, where))() == 0
end

function addfile(filename)
  check(cmd(mtn("add", filename)), 0, false, false)
end

function canonicalize(filename)
  local ostype = os.getenv("OSTYPE")
  local osenv = os.getenv("OS")
  if osenv ~= nil then osenv = string.find(osenv, "[Ww]in") end
  if ostype == "msys" or osenv then
    local f = io.open(filename, "rb")
    local indat = f:read("*a")
    f:close()
    local outdat = string.gsub(indat, "\r\n", "\n")
    f = io.open(filename, "wb")
    f:write(outdat)
    f:close()
  end
end

------------------------------------------------------------------------
--====================================================================--
------------------------------------------------------------------------

table.insert(tests, "tests/basic_invocation_and_options")
table.insert(tests, "tests/scanning_trees")
table.insert(tests, "tests/importing_a_file")
table.insert(tests, "tests/generating_and_extracting_keys_and_certs")
table.insert(tests, "tests/calculation_of_unidiffs")
table.insert(tests, "tests/persistence_of_passphrase")
table.insert(tests, "tests/multiple_version_committing")
table.insert(tests, "tests/creating_a_fork")
table.insert(tests, "tests/creating_a_fork_and_updating")
table.insert(tests, "tests/creating_a_fork_and_merging")
