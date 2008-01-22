-- Test 'automate inventory', with no path or depth restrictions
--
-- path and depth restrictions are tested in ../automate_inventory_restricted

local index = 1

mtn_setup()

check(getstd("inventory_hooks.lua"))

include ("common/test_utils_inventory.lua")

----------
-- create a basic file history; add some files, then operate on
-- each of them in some way.

addfile("missing", "missing")
addfile("dropped", "dropped")
addfile("original", "original")
addfile("unchanged", "unchanged")
addfile("patched", "patched")
commit()

addfile("added", "added")
writefile("unknown", "unknown")
writefile("ignored~", "ignored~")

remove("missing")
remove("dropped")
rename("original", "renamed")
writefile("patched", "something has changed")

check(mtn("rename", "--bookkeep-only", "original", "renamed"), 0, false, false)
check(mtn("drop", "--bookkeep-only", "dropped"), 0, false, false)

----------
-- see what 'automate inventory' has to say

check(mtn("automate", "inventory", "--rcfile=inventory_hooks.lua"), 0, true, false)

parsed = parse_basic_io(readfile("stdout"))
index = 1

index = check_inventory (parsed, index,
{path = "",
 old_type = "directory",
 new_type = "directory",
 fs_type = "directory",
 status = {"known"}})

index = check_inventory (parsed, index,
{path = "added",
 new_type = "file",
 fs_type = "file",
 status = {"added", "known"},
changes = {"content"}})

index = check_inventory (parsed, index,
{path = "dropped",
 old_type = "file",
 fs_type = "none",
  status = "dropped"})

index = check_inventory (parsed, index,
{   path = "ignored~",
 fs_type = "file",
  status = "ignored"})

-- skip inventory_hooks.lua, keys, keys/tester@test.net, min_hooks.lua
index = index + 3 * 4

index = check_inventory (parsed, index,
{   path = "missing",
old_type = "file",
new_type = "file",
 fs_type = "none",
  status = "missing"})

index = check_inventory (parsed, index,
{   path = "original",
old_type = "file",
new_path = "renamed",
 fs_type = "none",
  status = "rename_source"})

index = check_inventory (parsed, index,
{   path = "patched",
old_type = "file",
new_type = "file",
 fs_type = "file",
  status = "known",
 changes = "content"})

index = check_inventory (parsed, index,
{   path = "renamed",
new_type = "file",
old_path = "original",
 fs_type = "file",
  status = {"rename_target", "known"}})

-- skip test.db, test_hooks.lua, tester.log, ts-stderr, ts-stdin, ts-stdout
index = find_basic_io_line (parsed, {name = "path", values = "unchanged"})

index = check_inventory (parsed, index,
{   path = "unchanged",
old_type = "file",
new_type = "file",
 fs_type = "file",
  status = "known"})

index = check_inventory (parsed, index,
{  path = "unknown",
fs_type = "file",
 status = "unknown"})

----------
-- bookkeep-only swap names

check(mtn("revert", "."), 0, false, false)

check(mtn("rename", "--bookkeep-only", "unchanged", "temporary"), 0, false, false)
check(mtn("rename", "--bookkeep-only", "original", "unchanged"), 0, false, false)
check(mtn("rename", "--bookkeep-only", "temporary", "original"), 0, false, false)

check(mtn("automate", "inventory", "--rcfile=inventory_hooks.lua"), 0, true, false)

parsed = parse_basic_io(readfile("stdout"))

-- Only check the stanzas for the renamed files
index = find_basic_io_line (parsed, {name = "path", values = "original"})

-- This requires a bit of explanation. 'path' is the filesystem path.
-- The new manifest says 'original' is renamed to 'unchanged'. But
-- that has not actually been done in the workspace, so the contents
-- of the file are different between the filesystem and the new
-- manifest (they are the same as the old manifest).
--
-- If the user commits now, they probably get something other than
-- what they wanted; '--bookkeep-only' is dangerous.
--
-- The 'changes' flag indicates that this was bookkeep-only.
check_inventory (parsed, index,
{path     = "original",
 old_type = "file",
 new_path = "unchanged",
 new_type = "file",
 old_path = "unchanged",
 fs_type  = "file",
 status   = {"rename_source", "rename_target", "known"},
 changes  = "content"})

index = find_basic_io_line (parsed, {name = "path", values = "unchanged"})

check_inventory (parsed, index,
{path     = "unchanged",
 old_type = "file",
 new_path = "original",
 new_type = "file",
 old_path = "original",
 fs_type  = "file",
 status   = {"rename_source", "rename_target", "known"},
 changes  = "content"})

----------
-- same thing, renamed in filesystem

rename("unchanged", "temporary")
rename("original", "unchanged")
rename("temporary", "original")

check(mtn("automate", "inventory", "--rcfile=inventory_hooks.lua"), 0, true, false)

parsed = parse_basic_io(readfile("stdout"))

index = find_basic_io_line (parsed, {name = "path", values = "original"})

-- Now the 'changes' flag is gone.
check_inventory (parsed, index,
{path     = "original",
 old_type = "file",
 new_path = "unchanged",
 new_type = "file",
 old_path = "unchanged",
 fs_type  = "file",
 status   = {"rename_source", "rename_target", "known"}})

index = find_basic_io_line (parsed, {name = "path", values = "unchanged"})

check_inventory (parsed, index,
{path     = "unchanged",
 old_type = "file",
 new_path = "original",
 new_type = "file",
 old_path = "original",
 fs_type  = "file",
 status   = {"rename_source", "rename_target", "known"}})

----------
-- rename foo -> bar, add foo

check(mtn("revert", "."), 0, false, false)

-- The filesystem rename was done above.
check(mtn("rename", "--bookkeep-only", "original", "renamed"), 0, false, false)
check(mtn("add", "original"), 0, false, false)

check(mtn("automate", "inventory", "--rcfile=inventory_hooks.lua"), 0, true, false)

parsed = parse_basic_io(readfile("stdout"))

check_inventory (parsed, find_basic_io_line (parsed, {name = "path", values = "original"}),
{path     = "original",
 old_type = "file",
 new_path = "renamed",
 new_type = "file",
 fs_type  = "file",
 status   = {"rename_source", "added", "known"}})

check_inventory (parsed, find_basic_io_line (parsed, {name = "path", values = "renamed"}),
{path     = "renamed",
 new_type = "file",
 old_path = "original",
 fs_type  = "file",
 status   = {"rename_target", "known"}})

----------
-- bookkeep-only rename cycle
--   dropped -> missing -> original -> dropped

-- "original" has been renamed in filesystem to "renamed" above.

check(mtn("revert", "."), 0, false, false)

check(mtn("rename", "--bookkeep-only", "original", "temporary"), 0, false, false)
check(mtn("rename", "--bookkeep-only", "missing", "original"), 0, false, false)
check(mtn("rename", "--bookkeep-only", "dropped", "missing"), 0, false, false)
check(mtn("rename", "--bookkeep-only", "temporary", "dropped"), 0, false, false)

check(mtn("automate", "inventory", "--rcfile=inventory_hooks.lua"), 0, true, false)

parsed = parse_basic_io(readfile("stdout"))

-- This output is pretty confusing. But as we said above in the two
-- file rename test, '--bookkeep-only' is dangerous.
check_inventory (parsed,  find_basic_io_line (parsed, {name = "path", values = "dropped"}),
{path = "dropped",
old_type = "file",
new_path = "missing",
new_type = "file",
old_path = "original",
 fs_type = "file",
  status = {"rename_source", "rename_target", "known"},
 changes = "content"})

index = find_basic_io_line (parsed, {name = "path", values = "missing"})

check_inventory (parsed, index,
{   path = "missing",
old_type = "file",
new_path = "original",
new_type = "file",
old_path = "dropped",
 fs_type = "file",
  status = {"rename_source", "rename_target", "known"},
 changes = "content"})

index = find_basic_io_line (parsed, {name = "path", values = "original"})

check_inventory (parsed, index,
{   path = "original",
old_type = "file",
new_path = "dropped",
new_type = "file",
old_path = "missing",
 fs_type = "file",
  status = {"rename_source", "rename_target", "known"},
 changes = "content"})

----------
-- same, now renamed in the filesystem

rename("original", "temporary")
rename("missing", "original")
rename("dropped", "missing")
rename("temporary", "dropped")

check(mtn("automate", "inventory", "--rcfile=inventory_hooks.lua"), 0, true, false)

parsed = parse_basic_io(readfile("stdout"))

check_inventory (parsed, find_basic_io_line (parsed, {name = "path", values = "dropped"}),
{path = "dropped",
old_type = "file",
new_path = "missing",
new_type = "file",
old_path = "original",
 fs_type = "file",
  status = {"rename_source", "rename_target", "known"}})

check_inventory (parsed, find_basic_io_line (parsed, {name = "path", values = "missing"}),
{   path = "missing",
old_type = "file",
new_path = "original",
new_type = "file",
old_path = "dropped",
 fs_type = "file",
  status = {"rename_source", "rename_target", "known"}})

check_inventory (parsed, find_basic_io_line (parsed, {name = "path", values = "original"}),
{   path = "original",
old_type = "file",
new_path = "dropped",
new_type = "file",
old_path = "missing",
 fs_type = "file",
  status = {"rename_source", "rename_target", "known"}})

----------
-- bookkeep-only dropped

check(mtn("revert", "."), 0, false, false)

check(mtn("drop", "--bookkeep-only", "dropped"), 0, false, false)

check(mtn("automate", "inventory", "--rcfile=inventory_hooks.lua"), 0, true, false)

parsed = parse_basic_io(readfile("stdout"))

check_inventory (parsed, find_basic_io_line (parsed, {name = "path", values = "dropped"}),
{path = "dropped",
old_type = "file",
 fs_type = "file",
  status = {"dropped", "unknown"}})

----------
-- added but removed and thus missing

check(mtn("revert", "."), 0, false, false)

check(mtn("add", "added"), 0, false, false)
remove("added")

check(mtn("automate", "inventory", "--rcfile=inventory_hooks.lua"), 0, true, false)

parsed = parse_basic_io(readfile("stdout"))

check_inventory (parsed, find_basic_io_line (parsed, {name = "path", values = "added"}),
{path = "added",
new_type = "file",
 fs_type = "none",
  status = {"added", "missing"}})

----------
-- bookkeep-only rename; unknown source and missing target

check(mtn("revert", "."), 0, false, false)

remove("renamed")
check(mtn("rename", "--bookkeep-only", "original", "renamed"), 0, false, false)

check(mtn("automate", "inventory", "--rcfile=inventory_hooks.lua"), 0, true, false)

parsed = parse_basic_io(readfile("stdout"))

check_inventory (parsed, find_basic_io_line (parsed, {name = "path", values = "original"}),
{path = "original",
old_type = "file",
new_path = "renamed",
 fs_type = "file",
  status = {"rename_source", "unknown"}})

check_inventory (parsed, find_basic_io_line (parsed, {name = "path", values = "renamed"}),
{path = "renamed",
new_type = "file",
old_path = "original",
 fs_type = "none",
  status = {"rename_target", "missing"}})

----------
-- renamed in filesystem only; missing source and unknown target

check(mtn("revert", "."), 0, false, false)

rename("original", "renamed")

check(mtn("automate", "inventory", "--rcfile=inventory_hooks.lua"), 0, true, false)

parsed = parse_basic_io(readfile("stdout"))

check_inventory (parsed, find_basic_io_line (parsed, {name = "path", values = "original"}),
{path = "original",
old_type = "file",
new_type = "file",
 fs_type = "none",
  status = {"missing"}})

check_inventory (parsed, find_basic_io_line (parsed, {name = "path", values = "renamed"}),
{path = "renamed",
 fs_type = "file",
  status = {"unknown"}})

----------
-- filesystem and manifest renamed and patched

check(mtn("revert", "."), 0, false, false)

writefile("renamed", "renamed and patched")
remove("original")

check(mtn("rename", "--bookkeep-only", "original", "renamed"), 0, false, false)
check(mtn("automate", "inventory", "--rcfile=inventory_hooks.lua"), 0, true, false)

parsed = parse_basic_io(readfile("stdout"))

check_inventory (parsed, find_basic_io_line (parsed, {name = "path", values = "original"}),
{path = "original",
old_type = "file",
new_path = "renamed",
 fs_type = "none",
  status = {"rename_source"}})

check_inventory (parsed, find_basic_io_line (parsed, {name = "path", values = "renamed"}),
{path = "renamed",
new_type = "file",
old_path = "original",
 fs_type = "file",
  status = {"rename_target", "known"},
  changes = "content"})

----------
-- check if unknown/missing/dropped directories are recognized as such

mkdir("new_dir")
check(mtn("automate", "inventory", "--rcfile=inventory_hooks.lua"), 0, true, false)

parsed = parse_basic_io(readfile("stdout"))

check_inventory (parsed, find_basic_io_line (parsed, {name = "path", values = "new_dir"}),
{path = "new_dir",
 fs_type = "directory",
  status = {"unknown"}})

check(mtn("add", "new_dir"), 0, false, false)
remove("new_dir");

check(mtn("automate", "inventory", "--rcfile=inventory_hooks.lua"), 0, true, false)
parsed = parse_basic_io(readfile("stdout"))
check_inventory (parsed, find_basic_io_line (parsed, {name = "path", values = "new_dir"}),
{    path = "new_dir",
 new_type = "directory",
  fs_type = "none",
   status = {"added", "missing"}})

mkdir("new_dir")
commit()

check(mtn("automate", "inventory", "--rcfile=inventory_hooks.lua"), 0, true, false)
parsed = parse_basic_io(readfile("stdout"))
check_inventory (parsed, find_basic_io_line (parsed, {name = "path", values = "new_dir"}),
{path = "new_dir",
old_type = "directory",
new_type = "directory",
 fs_type = "directory",
  status = {"known"}})

remove("new_dir")
check(mtn("drop", "--bookkeep-only", "new_dir"), 0, false, false)

check(mtn("automate", "inventory", "--rcfile=inventory_hooks.lua"), 0, true, false)
parsed = parse_basic_io(readfile("stdout"))
check_inventory (parsed, find_basic_io_line (parsed, {name = "path", values = "new_dir"}),
{   path = "new_dir",
old_type = "directory",
 fs_type = "none",
  status = {"dropped"}})

----------
-- check for drop file / add dir with the same name and vice versa

addfile("still-a-file--soon-a-dir", "bla")
adddir("still-a-dir--soon-a-file")
commit()

check(mtn("drop", "still-a-file--soon-a-dir"), 0, false, false)
check(mtn("drop", "still-a-dir--soon-a-file"), 0, false, false)
adddir("still-a-file--soon-a-dir")
addfile("still-a-dir--soon-a-file", "bla")

check(mtn("automate", "inventory", "--rcfile=inventory_hooks.lua"), 0, true, false)
parsed = parse_basic_io(readfile("stdout"))

index = find_basic_io_line (parsed, {name = "path", values = "still-a-file--soon-a-dir"})
check_inventory (parsed, index,
{   path = "still-a-file--soon-a-dir",
old_type = "file",
new_type = "directory",
 fs_type = "directory",
  status = {"dropped", "added", "known"}})

index = find_basic_io_line (parsed, {name = "path", values = "still-a-dir--soon-a-file"})
check_inventory (parsed, index,
{   path = "still-a-dir--soon-a-file",
old_type = "directory",
new_type = "file",
 fs_type = "file",
  status = {"dropped", "added", "known"}})

----------
-- check for attribute changes

addfile("file-with-attributes", "bla")
check(mtn("attr", "set", "file-with-attributes", "foo", "bar"), 0, false, false)

check(mtn("automate", "inventory", "--rcfile=inventory_hooks.lua"), 0, true, false)
parsed = parse_basic_io(readfile("stdout"))
index = find_basic_io_line (parsed, {name = "path", values = "file-with-attributes"})
check_inventory (parsed, index,
{   path = "file-with-attributes",
new_type = "file",
 fs_type = "file",
  status = {"added", "known"},
changes = {"content", "attrs"}})

commit()

check(mtn("automate", "inventory", "--rcfile=inventory_hooks.lua"), 0, true, false)
parsed = parse_basic_io(readfile("stdout"))
index = find_basic_io_line (parsed, {name = "path", values = "file-with-attributes"})
check_inventory (parsed, index,
{   path = "file-with-attributes",
old_type = "file",
new_type = "file",
 fs_type = "file",
  status = {"known"}})

check(mtn("attr", "drop", "file-with-attributes", "foo"), 0, false, false)

check(mtn("automate", "inventory", "--rcfile=inventory_hooks.lua"), 0, true, false)
parsed = parse_basic_io(readfile("stdout"))
index = find_basic_io_line (parsed, {name = "path", values = "file-with-attributes"})
check_inventory (parsed, index,
{   path = "file-with-attributes",
old_type = "file",
new_type = "file",
 fs_type = "file",
  status = {"known"},
 changes = {"attrs"}})

-- FIXME: tests for renaming directories
-- also test that iff foo/ is renamed to bar/, any previous foo/node is
-- now listed as bar/node

-- FIXME: add test for 'pivot_root'

-- FIXME: add test for the 'invalid' state:
-- a) missing file, unversioned directory in the way
-- b) missing directory, unversioned file is in the way

-- end of file
