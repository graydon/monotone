-- Test 'automate inventory', with path and depth restrictions
--
-- nominal test in ../automate_inventory
--
-- We only test issues dealing specifically with path restrictions
-- here.

local index = 1

mtn_setup()

check(getstd("inventory_hooks.lua"))

include ("common/test_utils_inventory.lua")

----------
--  main process

-- create a basic file history; add directories and files, then
-- operate on some of them.

mkdir("dir_a")
mkdir("dir_b")

addfile("file_0", "original: root file_0")
addfile("dir_a/file_a", "original: dir_a file_a")
addfile("dir_b/file_b", "original: dir_b file_b")
commit()
rev1 = base_revision()

-- Test that 'automate inventory' shows all directories
check(mtn("automate", "inventory"), 0, true, false)

parsed = parse_basic_io(readfile("stdout"))

index = check_inventory (parsed, index,
{    path = "",
 old_type = "directory",
 new_type = "directory",
  fs_type = "directory",
    birth = rev1,
   status = {"known"}})

index = check_inventory (parsed, index,
{    path = "dir_a",
 old_type = "directory",
 new_type = "directory",
  fs_type = "directory",
    birth = rev1,
   status = {"known"}})

index = check_inventory (parsed, index,
{    path = "dir_a/file_a",
 old_type = "file",
 new_type = "file",
  fs_type = "file",
    birth = rev1,
   status = {"known"}})

index = check_inventory (parsed, index,
{    path = "dir_b",
 old_type = "directory",
 new_type = "directory",
  fs_type = "directory",
    birth = rev1,
 status = {"known"}})

index = check_inventory (parsed, index,
{    path = "dir_b/file_b",
 old_type = "file",
 new_type = "file",
  fs_type = "file",
    birth = rev1,
   status = {"known"}})

index = check_inventory (parsed, index,
{   path = "emptyhomedir",
 fs_type = "directory",
  status = "unknown"})

index = check_inventory (parsed, index,
{    path = "file_0",
 old_type = "file",
 new_type = "file",
  fs_type = "file",
    birth = rev1,
   status = {"known"}})

-- skip the test files in root

----------
-- Test that 'automate inventory --depth=1' shows only the files in root

check(mtn("automate", "inventory", "--depth=1"), 0, true, false)

parsed = parse_basic_io(readfile("stdout"))
index = 1

index = check_inventory (parsed, index,
{    path = "",
 old_type = "directory",
 new_type = "directory",
  fs_type = "directory",
    birth = rev1,
   status = {"known"}})

index = check_inventory (parsed, index,
{    path = "dir_a",
 old_type = "directory",
 new_type = "directory",
  fs_type = "directory",
    birth = rev1,
   status = {"known"}})

index = check_inventory (parsed, index,
{    path = "dir_b",
 old_type = "directory",
 new_type = "directory",
  fs_type = "directory",
    birth = rev1,
   status = {"known"}})

index = check_inventory (parsed, index,
{   path = "emptyhomedir",
 fs_type = "directory",
  status = "unknown"})

index = check_inventory (parsed, index,
{    path = "file_0",
 old_type = "file",
 new_type = "file",
  fs_type = "file",
    birth = rev1,
   status = {"known"}})

-- skip tester-generated files
index = index + 3 * 10

-- prove that we checked all the output
checkexp ("checked all", #parsed, index-1)

-- Test that 'automate inventory dir' shows only the files in dir

check(mtn("automate", "inventory", "dir_a"), 0, true, false)

parsed = parse_basic_io(readfile("stdout"))
index = 1

index = check_inventory (parsed, index,
{    path = "dir_a",
 old_type = "directory",
 new_type = "directory",
  fs_type = "directory",
    birth = rev1,
   status = {"known"}})

index = check_inventory (parsed, index,
{    path = "dir_a/file_a",
 old_type = "file",
 new_type = "file",
  fs_type = "file",
    birth = rev1,
   status = {"known"}})

-- prove that we checked all the output
checkexp ("checked all", #parsed, index-1)

----------
-- Rename a file from dir_a to dir_b, bookkeep-only; inventory dir_a
check(mtn("rename", "--bookkeep-only", "dir_a/file_a", "dir_b/file_a"), 0, true, false)

check(mtn("automate", "inventory", "dir_a"), 0, true, false)
parsed = parse_basic_io(readfile("stdout"))
index = 1

index = check_inventory (parsed, index,
{    path = "dir_a",
 old_type = "directory",
 new_type = "directory",
  fs_type = "directory",
    birth = rev1,
   status = {"known"}})

index = check_inventory (parsed, index,
{    path = "dir_a/file_a",
 old_type = "file",
 new_path = "dir_b/file_a",
  fs_type = "file",
   status = {"rename_source", "unknown"}})
-- "unknown" because of --bookkeep-only

checkexp ("checked all", #parsed, index-1)

check(mtn("automate", "inventory", "dir_b"), 0, true, false)
parsed = parse_basic_io(readfile("stdout"))
index = 1

index = check_inventory (parsed, index,
{    path = "dir_b",
 old_type = "directory",
 new_type = "directory",
  fs_type = "directory",
    birth = rev1,
 status = {"known"}})

index = check_inventory (parsed, index,
{    path = "dir_b/file_a",
 new_type = "file",
 old_path = "dir_a/file_a",
  fs_type = "none",
    birth = rev1,
   status = {"rename_target", "missing"}})
-- "missing" because of --bookkeep-only

index = check_inventory (parsed, index,
{    path = "dir_b/file_b",
 old_type = "file",
 new_type = "file",
  fs_type = "file",
    birth = rev1,
   status = {"known"}})

checkexp ("checked all", #parsed, index-1)

----------
-- same thing, actually renamed
rename("dir_a/file_a", "dir_b/file_a")

check(mtn("automate", "inventory", "dir_a"), 0, true, false)
parsed = parse_basic_io(readfile("stdout"))
index = 1

index = check_inventory (parsed, index,
{    path = "dir_a",
 old_type = "directory",
 new_type = "directory",
  fs_type = "directory",
    birth = rev1,
   status = {"known"}})

index = check_inventory (parsed, index,
{    path = "dir_a/file_a",
 old_type = "file",
 new_path = "dir_b/file_a",
  fs_type = "none",
   status = {"rename_source"}})

checkexp ("checked all", #parsed, index-1)

check(mtn("automate", "inventory", "dir_b"), 0, true, false)
parsed = parse_basic_io(readfile("stdout"))
index = 1

index = check_inventory (parsed, index,
{    path = "dir_b",
 old_type = "directory",
 new_type = "directory",
  fs_type = "directory",
    birth = rev1,
 status = {"known"}})

index = check_inventory (parsed, index,
{    path = "dir_b/file_a",
 new_type = "file",
 old_path = "dir_a/file_a",
  fs_type = "file",
    birth = rev1,
   status = {"rename_target", "known"}})

index = check_inventory (parsed, index,
{    path = "dir_b/file_b",
 old_type = "file",
 new_type = "file",
  fs_type = "file",
    birth = rev1,
   status = {"known"}})

checkexp ("checked all", #parsed, index-1)

----------
-- rename a file from root to dir_a, test --depth=1

check(mtn("rename", "--bookkeep-only", "file_0", "dir_a/file_0"), 0, true, false)

check(mtn("automate", "inventory", "--depth=1"), 0, true, false)

parsed = parse_basic_io(readfile("stdout"))
index = 1

index = check_inventory (parsed, index,
{    path = "",
 old_type = "directory",
 new_type = "directory",
  fs_type = "directory",
    birth = rev1,
   status = {"known"}})

index = check_inventory (parsed, index,
{    path = "dir_a",
 old_type = "directory",
 new_type = "directory",
  fs_type = "directory",
    birth = rev1,
   status = {"known"}})

index = check_inventory (parsed, index,
{    path = "dir_b",
 old_type = "directory",
 new_type = "directory",
  fs_type = "directory",
    birth = rev1,
   status = {"known"}})

index = check_inventory (parsed, index,
{   path = "emptyhomedir",
 fs_type = "directory",
  status = "unknown"})

index = check_inventory (parsed, index,
{    path = "file_0",
 old_type = "file",
 new_path = "dir_a/file_0",
  fs_type = "file",
   status = {"rename_source", "unknown"}})
-- unknown because of --bookkeep-only

-- skip tester-generated files
index = index + 3 * 10

checkexp ("checked all", #parsed, index-1)

----------
-- same thing, actually renamed

rename("file_0", "dir_a/file_0")

check(mtn("automate", "inventory", "--depth=1"), 0, true, false)

parsed = parse_basic_io(readfile("stdout"))
index = 1

index = check_inventory (parsed, index,
{    path = "",
 old_type = "directory",
 new_type = "directory",
  fs_type = "directory",
    birth = rev1,
   status = {"known"}})

index = check_inventory (parsed, index,
{    path = "dir_a",
 old_type = "directory",
 new_type = "directory",
  fs_type = "directory",
    birth = rev1,
   status = {"known"}})

index = check_inventory (parsed, index,
{    path = "dir_b",
 old_type = "directory",
 new_type = "directory",
  fs_type = "directory",
    birth = rev1,
   status = {"known"}})

index = check_inventory (parsed, index,
{   path = "emptyhomedir",
 fs_type = "directory",
  status = "unknown"})

index = check_inventory (parsed, index,
{    path = "file_0",
 old_type = "file",
 new_path = "dir_a/file_0",
  fs_type = "none",
   status = {"rename_source"}})

-- skip tester-generated files
index = index + 3 * 10

checkexp ("checked all", #parsed, index-1)

----------
-- reset the workspace for further tests

check(mtn("revert", "."), 0, false, false)
remove("dir_a/file_0")

----------
-- rename a node and restrict to source and target

check(mtn("mv", "dir_a", "dir_c"), 0, false, false)

check(mtn("automate", "inventory", "dir_a"), 0, true, false)
source_restricted = readfile("stdout")

check(mtn("automate", "inventory", "dir_c"), 0, true, false)
target_restricted = readfile("stdout")

-- restricting to either path, old or new, should lead to the same output
check(source_restricted == target_restricted)

-- now check this output
parsed = parse_basic_io(source_restricted)
index = 1

index = check_inventory (parsed, index,
{    path = "dir_a",
 old_type = "directory",
 new_path = "dir_c",
  fs_type = "none",
   status = {"rename_source"}})

index = check_inventory (parsed, index,
{    path = "dir_a/file_a",
 old_type = "file",
 new_path = "dir_c/file_a",
  fs_type = "none",
   status = {"rename_source"}})

index = check_inventory (parsed, index,
{    path = "dir_c",
 new_type = "directory",
 old_path = "dir_a",
  fs_type = "directory",
    birth = rev1,
   status = {"rename_target", "known" }})

index = check_inventory (parsed, index,
{    path = "dir_c/file_a",
 new_type = "file",
 old_path = "dir_a/file_a",
  fs_type = "file",
    birth = rev1,
   status = {"rename_target", "known" }})

checkexp ("checked all", #parsed, index-1)

----------
-- keep the above workspace situtation, but exclude the output of the file_a
-- node in all scenarios

check(mtn("automate", "inventory", "dir_a", "--exclude", "dir_a/file_a"), 0, true, false)
rename("stdout", "source_source_excluded")

check(mtn("automate", "inventory", "dir_a", "--exclude", "dir_c/file_a"), 0, true, false)
rename("stdout", "source_target_excluded")

check(mtn("automate", "inventory", "dir_c", "--exclude", "dir_c/file_a"), 0, true, false)
rename("stdout", "target_target_excluded")

check(mtn("automate", "inventory", "dir_c", "--exclude", "dir_a/file_a"), 0, true, false)
rename("stdout", "target_source_excluded")

-- all of the above calls should return the same output
check(samefile("source_source_excluded", "source_target_excluded"))
check(samefile("target_target_excluded", "target_source_excluded"))
check(samefile("source_source_excluded", "target_target_excluded"))

-- now check this output
parsed = parse_basic_io(readfile("source_source_excluded"))
index = 1

index = check_inventory (parsed, index,
{    path = "dir_a",
 old_type = "directory",
 new_path = "dir_c",
  fs_type = "none",
   status = {"rename_source"}})

index = check_inventory (parsed, index,
{    path = "dir_c",
 new_type = "directory",
 old_path = "dir_a",
  fs_type = "directory",
    birth = rev1,
   status = {"rename_target", "known" }})

checkexp ("checked all", #parsed, index-1)

-- end of file
