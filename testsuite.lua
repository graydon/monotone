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

function minhooks_mtn(...)
  return raw_mtn("--db=" .. test_root .. "/test.db",
                 "--keydir", test_root .. "/keys",
                 "--rcfile", test_root .. "/min_hooks.lua",
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
  
  check(cmd(mtn("db", "init")), 0, false, false)
  check(cmd(mtn("read", "test_keys")), 0, false, false)
  check(cmd(mtn("setup", "--branch=testbranch", ".")), 0, false, false)
  os.remove("test_keys")
end


-- netsync

netsync_address = nil

function netsync_setup()
  copyfile("test.db", "test2.db")
  copy_recursive("keys", "keys2")
  copyfile("test.db", "test3.db")
  copy_recursive("keys", "keys3")
  getstdfile("tests/netsync.lua", "netsync.lua")
  netsync_address = "localhost:" .. math.random(20000, 50000)
end

function netsync_setup_with_notes()
  netsync_setup()
  getstdfile("tests/netsync_with_notes.lua", "netsync.lua")
end

function mtn2(...)
  return mtn("--db=test2.db", "--keydir=keys2", unpack(arg))
end

function mtn3(...)
  return mtn("--db=test3.db", "--keydir=keys3", unpack(arg))
end

function netsync_serve_start(pat, n, min)
  if pat == "" or pat == nil then pat = "*" end
  local args = {}
  local fn = mtn
  table.insert(args, "--dump=_MTN/server_dump")
  table.insert(args, "--bind="..netsync_address)
  if min then
    fn = minhooks_mtn
  else
    table.insert(args, "--rcfile=netsync.lua")
  end
  if n ~= nil then
    table.insert(args, "--keydir=keys"..n)
    table.insert(args, "--db=test"..n..".db")
  end
  table.insert(args, "serve")
  table.insert(args, pat)
  local out = bg({fn(unpack(args))}, false, false, false)
  -- wait for "beginning service..."
  while fsize(out.prefix .. "stderr") == 0 do
    sleep(1)
  end
  return out
end

function netsync_client_run(oper, pat, res, n)
  if pat == "" or pat == nil then pat = "*" end
  if n == nil then n = 2 end
  check(cmd(mtn("--rcfile=netsync.lua", "--keydir=keys"..n,
                "--db=test"..n..".db", oper, netsync_address, pat)),
        res, false, false)
end

function run_netsync(oper, pat)
  local srv = netsync_serve_start(pat)
  netsync_client_run(oper, pat, 0)
  srv:finish()
end




function base_revision()
  return string.gsub(readfile("_MTN/revision"), "%s*$", "")
end

function qgrep(what, where)
  return cmd(grep("-q", what, where))() == 0
end

function addfile(filename, contents)
  if contents ~= nil then writefile(filename, contents) end
  check(cmd(mtn("add", filename)), 0, false, false)
end

function revert_to(rev, branch)
  remove_recursive("_MTN.old")
  os.rename("_MTN", "_MTN.old")
  
  if branch == nil then
    check(cmd(mtn("checkout", "--revision", rev, ".")), 0, false)
  else
    check(cmd(mtn("checkout", "--branch", branch, "--revision", rev, ".")), 0, false)
  end
  check(base_revision() == rev)
end

ostype = string.sub(get_ostype(), 1, string.find(get_ostype(), " ")-1)

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

-- maybe this one should go in tester.lua?
function check_same_stdout(cmd1, cmd2)
  check(cmd1, 0, true, false)
  rename("stdout", "stdout-first")
  check(cmd2, 0, true, false)
  check(samefile("stdout", "stdout-first"))
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
table.insert(tests, "tests/merging_adds")
table.insert(tests, "tests/merging_data_in_unrelated_files")
table.insert(tests, "tests/merging_adds_in_unrelated_revisions")
table.insert(tests, "tests/merging_data_in_unrelated_revisions")
table.insert(tests, "tests/calculation_of_other_unidiffs")
table.insert(tests, "tests/delete_work_file_on_checkout")
table.insert(tests, "tests/revert_file_to_base_revision")
table.insert(tests, "tests/addition_of_files_and_directories")
table.insert(tests, "tests/add_and_then_drop_file_does_nothing")
table.insert(tests, "tests/drop_missing_and_unknown_files")
table.insert(tests, "tests/creating_a_bad_criss-cross_merge")
table.insert(tests, "tests/renaming_a_file")
table.insert(tests, "tests/renaming_a_directory")
table.insert(tests, "tests/renaming_and_editing_a_file")
table.insert(tests, "tests/importing_CVS_files")
table.insert(tests, "tests/importing_files_with_non-english_names")
table.insert(tests, "tests/external_unit_test_of_the_line_merger")
table.insert(tests, "tests/exchanging_work_via_netsync")
table.insert(tests, "tests/single_manifest_netsync")
table.insert(tests, "tests/netsync_transfers_public_keys")
table.insert(tests, "tests/repeatedly_exchanging_work_via_netsync")
table.insert(tests, "tests/(normal)_netsync_on_partially_unrelated_revisions")
table.insert(tests, "tests/disapproving_of_a_revision")
