#!./tester

monotone_path = nil

function safe_mtn(...)
  if monotone_path == nil then
    monotone_path = os.getenv("mtn")
    if monotone_path == nil then
      err("'mtn' environment variable not set")
    end
  end
  return {monotone_path, "--debug", "--norc", "--root=" .. test.root,
          "--confdir="..test.root, unpack(arg)}
end

-- function preexecute(x)
--   return {"valgrind", "--tool=memcheck", unpack(x)}
-- end

function raw_mtn(...)
  if preexecute ~= nil then
    return preexecute(safe_mtn(unpack(arg)))
  else
    return safe_mtn(unpack(arg))
  end
end

function mtn(...)
  return raw_mtn("--rcfile", test.root .. "/test_hooks.lua", -- "--nostd",
         "--db=" .. test.root .. "/test.db",
         "--keydir", test.root .. "/keys",
         "--key=tester@test.net", unpack(arg))
end

function nodb_mtn(...)
  return raw_mtn("--rcfile", test.root .. "/test_hooks.lua", -- "--nostd",
         "--keydir", test.root .. "/keys",
         "--key=tester@test.net", unpack(arg))
end

function nokey_mtn(...)
  return raw_mtn("--rcfile", test.root .. "/test_hooks.lua", -- "--nostd",
         "--db=" .. test.root .. "/test.db",
         "--keydir", test.root .. "/keys", unpack(arg))
end

function minhooks_mtn(...)
  return raw_mtn("--db=" .. test.root .. "/test.db",
                 "--keydir", test.root .. "/keys",
                 "--rcfile", test.root .. "/min_hooks.lua",
                 "--key=tester@test.net", unpack(arg))
end

function commit(branch, message, mt)
  if branch == nil then branch = "testbranch" end
  if message == nil then message = "blah-blah" end
  if mt == nil then mt = mtn end
  check(mt("commit", "--message", message, "--branch", branch), 0, false, false)
end

function sha1(what)
  check(safe_mtn("identify", what), 0, false, false)
  return trim(readfile("ts-stdout"))
end

function probe_node(filename, rsha, fsha)
  remove("_MTN.old")
  rename("_MTN", "_MTN.old")
  remove(filename)
  check(mtn("checkout", "--revision", rsha, "."), 0, false, true)
  rename("_MTN.old/options", "_MTN")
  check(base_revision() == rsha)
  check(sha1(filename) == fsha)
end

function mtn_setup()
  check(getstd("test_keys"))
  check(getstd("test_hooks.lua"))
  check(getstd("min_hooks.lua"))
  
  check(mtn("db", "init"), 0, false, false)
  check(mtn("read", "test_keys"), 0, false, false)
  check(mtn("setup", "--branch=testbranch", "."), 0, false, false)
  remove("test_keys")
end

function base_revision()
  local workrev = readfile("_MTN/revision")
  local extract = string.gsub(workrev, "^.*old_revision %[(%x*)%].*$", "%1")
  if extract == workrev then
    err("failed to extract base revision from _MTN/revision")
  end
  return extract
end

function base_manifest()
  check(safe_mtn("automate", "get_manifest_of", base_revision()), 0, false)
  check(copy("ts-stdout", "base_manifest_temp"))
  return sha1("base_manifest_temp")
end

function certvalue(rev, name)
  check(safe_mtn("automate", "certs", rev), 0, false)
  local parsed = parse_basic_io(readfile("ts-stdout"))
  local cname
  local goodsig
  -- note: this relies on the name and signature elements appearing
  -- before the value element, in each stanza.
  for _,l in pairs(parsed) do
    if l.name == "name" then cname = l.values[1] end
    if l.name == "signature" then goodsig = l.values[1] end
    if cname == name and l.name == "value" then return l.values[1], goodsig end
  end
  return nil
end

function qgrep(what, where)
  local ok,res = pcall(unpack(grep("-q", what, where)))
  if not ok then err(res) end
  return res == 0
end

function addfile(filename, contents, mt)
  if contents ~= nil then writefile(filename, contents) end
  if mt == nil then mt = mtn end
  check(mt("add", filename), 0, false, false)
end

function adddir(dirname, mt)
  if not isdir(dirname) then mkdir(dirname) end
  if mt == nil then mt = mtn end
  check(mt("add", dirname), 0, false, false)
end

function revert_to(rev, branch, mt)
  if type(branch) == "function" then
    mt = branch
    branch = nil
  end
  if mt == nil then mt = mtn end

  check(mt("automate", "get_manifest_of", base_revision()), 0, true, false)
  rename("stdout", "paths-new")

  remove("_MTN.old")
  rename("_MTN", "_MTN.old")

  check(mt("automate", "get_manifest_of", rev), 0, true, false)
  rename("stdout", "paths-old")

  -- remove all of the files and dirs in this
  -- manifest to clear the way for checkout

  for path in io.lines("paths-new") do
    len = string.len(path) - 1
      
    if (string.match(path, "^   file \"")) then
      path = string.sub(path, 10, len)
    elseif (string.match(path, "^dir \"")) then
      path = string.sub(path, 6, len)
    else
      path = ""
    end

    if (string.len(path) > 0) then
      remove(path)
    end
  end
        
  for path in io.lines("paths-old") do
    len = string.len(path) - 1
      
    if (string.match(path, "^   file \"")) then
      path = string.sub(path, 10, len)
    elseif (string.match(path, "^dir \"")) then
      path = string.sub(path, 6, len)
    else
      path = ""
    end

    if (string.len(path) > 0) then
      remove(path)
    end
  end
        
  if branch == nil then
    check(mt("checkout", "--revision", rev, "."), 0, false, true)
  else
    check(mt("checkout", "--branch", branch, "--revision", rev, "."), 0, false, true)
  end
  check(base_revision() == rev)
end

function canonicalize(filename)
  if ostype == "Windows" then
    L("Canonicalizing ", filename, "\n")
    local f = io.open(filename, "rb")
    local indat = f:read("*a")
    f:close()
    local outdat = string.gsub(indat, "\r\n", "\n")
    f = io.open(filename, "wb")
    f:write(outdat)
    f:close()
  else
    L("Canonicalization not needed (", filename, ")\n")
  end
end

function check_same_db_contents(db1, db2)
  check_same_stdout(mtn("--db", db1, "ls", "keys"),
                    mtn("--db", db2, "ls", "keys"))
  
  check(mtn("--db", db1, "complete", "revision", ""), 0, true, false)
  rename("stdout", "revs")
  check(mtn("--db", db2, "complete", "revision", ""), 0, true, false)
  check(samefile("stdout", "revs"))
  for rev in io.lines("revs") do
    rev = trim(rev)
    check_same_stdout(mtn("--db", db1, "automate", "certs", rev),
                      mtn("--db", db2, "automate", "certs", rev))
    check_same_stdout(mtn("--db", db1, "automate", "get_revision", rev),
                      mtn("--db", db2, "automate", "get_revision", rev))
    check_same_stdout(mtn("--db", db1, "automate", "get_manifest_of", rev),
                      mtn("--db", db2, "automate", "get_manifest_of", rev))
  end
  
  check(mtn("--db", db1, "complete", "file", ""), 0, true, false)
  rename("stdout", "files")
  check(mtn("--db", db2, "complete", "file", ""), 0, true, false)
  check(samefile("stdout", "files"))
  for file in io.lines("files") do
    file = trim(file)
    check_same_stdout(mtn("--db", db1, "automate", "get_file", file),
                      mtn("--db", db2, "automate", "get_file", file))
  end
end

-- maybe these should go in tester.lua?
function do_check_same_stdout(cmd1, cmd2)
  check(cmd1, 0, true, false)
  rename("stdout", "stdout-first")
  check(cmd2, 0, true, false)
  rename("stdout", "stdout-second")
  check(samefile("stdout-first", "stdout-second"))
end
function do_check_different_stdout(cmd1, cmd2)
  check(cmd1, 0, true, false)
  rename("stdout", "stdout-first")
  check(cmd2, 0, true, false)
  rename("stdout", "stdout-second")
  check(not samefile("stdout-first", "stdout-second"))
end
function check_same_stdout(a, b, c)
  if type(a) == "table" and type(b) == "table" then
    return do_check_same_stdout(a, b)
  elseif type(a) == "table" and type(b) == "function" and type(c) == "function" then
    return do_check_same_stdout(b(unpack(a)), c(unpack(a)))
  elseif type(a) == "table" and type(b) == "nil" and type(c) == "nil" then
    return do_check_same_stdout(mtn(unpack(a)), mtn2(unpack(a)))
  else
    err("bad arguments ("..type(a)..", "..type(b)..", "..type(c)..") to check_same_stdout")
  end
end
function check_different_stdout(a, b, c)
  if type(a) == "table" and type(b) == "table" then
    return do_check_different_stdout(a, b)
  elseif type(a) == "table" and type(b) == "function" and type(c) == "function" then
    return do_check_different_stdout(b(unpack(a)), c(unpack(a)))
  elseif type(a) == "table" and type(b) == "nil" and type(c) == "nil" then
    return do_check_different_stdout(mtn(unpack(a)), mtn2(unpack(a)))
  else
    err("bad arguments ("..type(a)..", "..type(b)..", "..type(c)..") to check_different_stdout")
  end
end

function write_large_file(name, size)
  local file = io.open(name, "wb")
  for i = 1,size do
    for j = 1,128 do -- write 1MB
      local str8k = ""
      for k = 1,256 do
        -- 32
        str8k = str8k .. string.char(math.random(255), math.random(255),
                                     math.random(255), math.random(255),
                                     math.random(255), math.random(255),
                                     math.random(255), math.random(255),
                                     math.random(255), math.random(255),
                                     math.random(255), math.random(255),
                                     math.random(255), math.random(255),
                                     math.random(255), math.random(255),
                                     math.random(255), math.random(255),
                                     math.random(255), math.random(255),
                                     math.random(255), math.random(255),
                                     math.random(255), math.random(255),
                                     math.random(255), math.random(255),
                                     math.random(255), math.random(255),
                                     math.random(255), math.random(255),
                                     math.random(255), math.random(255))
      end
      file:write(str8k)
    end
  end
  file:close()
end

------------------------------------------------------------------------
--====================================================================--
------------------------------------------------------------------------
testdir = srcdir.."/tests"

function prepare_to_run_tests (P)
   -- We have a bunch of tests that depend on being able to create
   -- files or directories that we cannot read or write (mostly to
   -- test error handling behavior).
   require_not_root()

   -- Several tests require the ability to create temporary
   -- directories outside the workspace.
   local d = make_temp_dir()
   if d == nil then
      P("This test suite requires the ability to create files\n"..
        "in the system-wide temporary directory.  Please correct the\n"..
        "access permissions on this directory and try again.\n")
      return 1
   end
   unlogged_remove(d)

   monotone_path = getpathof("mtn")
   if monotone_path == nil then monotone_path = "mtn" end
   set_env("mtn", monotone_path)

   writefile_q("in", nil)
   prepare_redirect("in", "out", "err")

   local status = execute(monotone_path, "version", "--full")
   local out = readfile_q("out")
   local err = readfile_q("err")

   if status == 0 and err == "" and out ~= "" then
      logfile:write(out)
   else
      P(string.format("mtn version --full: exit %d\nstdout:\n", status))
      P(out)
      P("stderr:\n")
      P(err)

      if status == 0 then status = 1 end
   end

   unlogged_remove("in")
   unlogged_remove("out")
   unlogged_remove("err")
   return status
end
