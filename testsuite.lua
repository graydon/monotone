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

function mtn2(...)
  return mtn("--db=test2.db", "--keydir=keys2", unpack(arg))
end

function mtn3(...)
  return mtn("--db=test3.db", "--keydir=keys3", unpack(arg))
end

netsync = {}
netsync.internal = {}

function netsync.setup()
  copyfile("test.db", "test2.db")
  copy_recursive("keys", "keys2")
  copyfile("test.db", "test3.db")
  copy_recursive("keys", "keys3")
  getstdfile("tests/netsync.lua", "netsync.lua")
end

function netsync.setup_with_notes()
  netsync.setup()
  getstdfile("tests/netsync_with_notes.lua", "netsync.lua")
end

function netsync.internal.client(srv, oper, pat, n, res)
  if pat == "" or pat == nil then pat = "*" end
  if n == nil then n = 2 end
  check(cmd(mtn("--rcfile=netsync.lua", "--keydir=keys"..n,
                "--db=test"..n..".db", oper, srv.address, pat)),
        res, false, false)
end
function netsync.internal.pull(srv, pat, n, res) srv:client("pull", pat, n, res) end
function netsync.internal.push(srv, pat, n, res) srv:client("push", pat, n, res) end
function netsync.internal.sync(srv, pat, n, res) srv:client("sync", pat, n, res) end

function netsync.start(pat, n, min)
  if pat == "" or pat == nil then pat = "*" end
  local args = {}
  local fn = mtn
  local addr = "localhost:" .. math.random(20000, 50000)
  table.insert(args, "--dump=_MTN/server_dump")
  table.insert(args, "--bind="..addr)
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
  out.address = addr
  local mt = getmetatable(out)
  mt.client = netsync.internal.client
  mt.pull = netsync.internal.pull
  mt.push = netsync.internal.push
  mt.sync = netsync.internal.sync
  return out
end

function netsync.internal.run(oper, pat)
  local srv = netsync.start(pat)
  srv:client(oper, pat)
  srv:finish()
end
function netsync.pull(pat) netsync.internal.run("pull", pat) end
function netsync.push(pat) netsync.internal.run("push", pat) end
function netsync.sync(pat) netsync.internal.run("sync", pat) end




function base_revision()
  return (string.gsub(readfile("_MTN/revision"), "%s*$", ""))
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

function check_same_db_contents(db1, db2)
  check_same_stdout(cmd(mtn("--db", db1, "ls", "keys")),
                    cmd(mtn("--db", db2, "ls", "keys")))
  
  check(cmd(mtn("--db", db1, "complete", "revision", "")), 0, true, false)
  rename("stdout", "revs")
  check(cmd(mtn("--db", db2, "complete", "revision", "")), 0, true, false)
  check(samefile("stdout", "revs"))
  for rev in io.lines("revs") do
    rev = trim(rev)
    check_same_stdout(cmd(mtn("--db", db1, "automate", "certs", rev)),
                      cmd(mtn("--db", db2, "automate", "certs", rev)))
    check_same_stdout(cmd(mtn("--db", db1, "automate", "get_revision", rev)),
                      cmd(mtn("--db", db2, "automate", "get_revision", rev)))
    check_same_stdout(cmd(mtn("--db", db1, "automate", "get_manifest_of", rev)),
                      cmd(mtn("--db", db2, "automate", "get_manifest_of", rev)))
  end
  
  check(cmd(mtn("--db", db1, "complete", "file", "")), 0, true, false)
  rename("stdout", "files")
  check(cmd(mtn("--db", db2, "complete", "file", "")), 0, true, false)
  check(samefile("stdout", "files"))
  for file in io.lines("files") do
    file = trim(file)
    check_same_stdout(cmd(mtn("--db", db1, "automate", "get_file", file)),
                      cmd(mtn("--db", db2, "automate", "get_file", file)))
  end
end

-- maybe this one should go in tester.lua?
function check_same_stdout(cmd1, cmd2)
  check(cmd1, 0, true, false)
  rename_over("stdout", "stdout-first")
  check(cmd2, 0, true, false)
  rename_over("stdout", "stdout-second")
  check(samefile("stdout-first", "stdout-second"))
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
table.insert(tests, "tests/creating_a_good_and_bad_test_result")
table.insert(tests, "tests/importing_a_CVS_file_with_one_version")
table.insert(tests, "tests/list_missing_files")
table.insert(tests, "tests/attr_set_get_commands")
table.insert(tests, "tests/single_character_filename_support")
table.insert(tests, "tests/manifest_restrictions")
table.insert(tests, "tests/subdirectory_restrictions")
table.insert(tests, "tests/renaming_a_patched_file")
table.insert(tests, "tests/renaming_a_deleted_file")
table.insert(tests, "tests/merging_a_rename_twice")
table.insert(tests, "tests/updating_from_a_merge_which_adds_a_file")
table.insert(tests, "tests/changing_passphrase_of_a_private_key")
table.insert(tests, "tests/diffing_a_revision_with_an_added_file")
table.insert(tests, "tests/updating_to_a_given_revision")
table.insert(tests, "tests/'heads'")
table.insert(tests, "tests/'heads'_with_discontinuous_branches")
table.insert(tests, "tests/test_a_merge")
table.insert(tests, "tests/test_a_merge_2")
table.insert(tests, "tests/tags_and_tagging_of_revisions")
table.insert(tests, "tests/mtn_add_.")
table.insert(tests, "tests/(minor)_update_cleans_emptied_directories")
table.insert(tests, "tests/merging_<add_a>_with_<add_a,_drop_a>")
table.insert(tests, "tests/merging_an_add_edge")
table.insert(tests, "tests/merge(<>,_<patch_a,_drop_a,_add_a>)")
table.insert(tests, "tests/merge(<>,_<add_a,_drop_a,_add_a>)")
table.insert(tests, "tests/merge(<add_a>,_<add_a,_drop_a,_add_a>)")
table.insert(tests, "tests/merge(<>,_<add_a,_patch_a,_drop_a,_add_a>)")
table.insert(tests, "tests/merge(<patch_a>,_<drop_a,_add_a>)")
table.insert(tests, "tests/explicit_merge")
table.insert(tests, "tests/update_with_multiple_candidates")
table.insert(tests, "tests/checkout_validates_target_directory")
table.insert(tests, "tests/checkout_creates_right__MTN_options")
table.insert(tests, "tests/trust_hooks_and_'trusted'_command")
table.insert(tests, "tests/attr_set_attr_get")
table.insert(tests, "tests/--rcfile_requires_extant_file")
table.insert(tests, "tests/persistent_netsync_server_-_revs_&_certs")
table.insert(tests, "tests/persistent_netsync_server_-_keys")
table.insert(tests, "tests/first_extent_normalization_pass")
table.insert(tests, "tests/(imp)_deleting_directories")
table.insert(tests, "tests/schema_migration")
table.insert(tests, "tests/database_dump_load")
table.insert(tests, "tests/no-change_deltas_disappear")
