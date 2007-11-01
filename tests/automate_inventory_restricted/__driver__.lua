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

-- Test that 'automate inventory' shows all directories
check(mtn("automate", "inventory"), 0, true, false)

parsed = parse_basic_io(readfile("stdout"))

index = check_inventory (parsed, index,
{    path = "",
 old_type = "directory",
 new_type = "directory",
  fs_type = "directory",
   status = {"known"}})

index = check_inventory (parsed, index,
{    path = "dir_a",
 old_type = "directory",
 new_type = "directory",
  fs_type = "directory",
   status = {"known"}})

index = check_inventory (parsed, index,
{    path = "dir_a/file_a",
 old_type = "file",
 new_type = "file",
  fs_type = "file",
   status = {"known"}})

index = check_inventory (parsed, index,
{    path = "dir_b",
 old_type = "directory",
 new_type = "directory",
  fs_type = "directory",
 status = {"known"}})

index = check_inventory (parsed, index,
{    path = "dir_b/file_b",
 old_type = "file",
 new_type = "file",
  fs_type = "file",
   status = {"known"}})

index = check_inventory (parsed, index,
{    path = "file_0",
 old_type = "file",
 new_type = "file",
  fs_type = "file",
   status = {"known"}})

-- skip the test files in root

----------
-- Test that 'automate inventory --depth=0' shows only the files in root

check(mtn("automate", "inventory", "--depth=0"), 0, true, false)

parsed = parse_basic_io(readfile("stdout"))
index = 1

index = check_inventory (parsed, index,
{    path = "",
 old_type = "directory",
 new_type = "directory",
  fs_type = "directory",
   status = {"known"}})

index = check_inventory (parsed, index,
{    path = "dir_a",
 old_type = "directory",
 new_type = "directory",
  fs_type = "directory",
   status = {"known"}})

index = check_inventory (parsed, index,
{    path = "dir_b",
 old_type = "directory",
 new_type = "directory",
  fs_type = "directory",
   status = {"known"}})

index = check_inventory (parsed, index,
{    path = "file_0",
 old_type = "file",
 new_type = "file",
  fs_type = "file",
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
   status = {"known"}})

index = check_inventory (parsed, index,
{    path = "dir_a/file_a",
 old_type = "file",
 new_type = "file",
  fs_type = "file",
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
 status = {"known"}})

index = check_inventory (parsed, index,
{    path = "dir_b/file_a",
 new_type = "file",
 old_path = "dir_a/file_a",
  fs_type = "none",
   status = {"rename_target", "missing"}})
-- "missing" because of --bookkeep-only

index = check_inventory (parsed, index,
{    path = "dir_b/file_b",
 old_type = "file",
 new_type = "file",
  fs_type = "file",
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
 status = {"known"}})

index = check_inventory (parsed, index,
{    path = "dir_b/file_a",
 new_type = "file",
 old_path = "dir_a/file_a",
  fs_type = "file",
   status = {"rename_target", "known"}})

index = check_inventory (parsed, index,
{    path = "dir_b/file_b",
 old_type = "file",
 new_type = "file",
  fs_type = "file",
   status = {"known"}})

checkexp ("checked all", #parsed, index-1)

----------
-- rename a file from root to dir_a, test --depth=0

check(mtn("rename", "--bookkeep-only", "file_0", "dir_a/file_0"), 0, true, false)

check(mtn("automate", "inventory", "--depth=0"), 0, true, false)

parsed = parse_basic_io(readfile("stdout"))
index = 1

index = check_inventory (parsed, index,
{    path = "",
 old_type = "directory",
 new_type = "directory",
  fs_type = "directory",
   status = {"known"}})

index = check_inventory (parsed, index,
{    path = "dir_a",
 old_type = "directory",
 new_type = "directory",
  fs_type = "directory",
   status = {"known"}})

index = check_inventory (parsed, index,
{    path = "dir_b",
 old_type = "directory",
 new_type = "directory",
  fs_type = "directory",
   status = {"known"}})

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

check(mtn("automate", "inventory", "--depth=0"), 0, true, false)

parsed = parse_basic_io(readfile("stdout"))
index = 1

index = check_inventory (parsed, index,
{    path = "",
 old_type = "directory",
 new_type = "directory",
  fs_type = "directory",
   status = {"known"}})

index = check_inventory (parsed, index,
{    path = "dir_a",
 old_type = "directory",
 new_type = "directory",
  fs_type = "directory",
   status = {"known"}})

index = check_inventory (parsed, index,
{    path = "dir_b",
 old_type = "directory",
 new_type = "directory",
  fs_type = "directory",
   status = {"known"}})

index = check_inventory (parsed, index,
{    path = "file_0",
 old_type = "file",
 new_path = "dir_a/file_0",
  fs_type = "none",
   status = {"rename_source"}})

-- skip tester-generated files
index = index + 3 * 10

checkexp ("checked all", #parsed, index-1)

-- end of file
