-- Test 'automate inventory' options --no-ignored, --no-unchanged, --no-unknown
-- and --no-corresponding-renames
--
-- We don't test with --bookkeep-only, because we haven't gotten to it yet.

mtn_setup()

--  override standard test_hooks.lua, because 'automate stdio' uses it.
check(get("test_hooks.lua"))

check(get("expected-none.stdout"))
check(get("expected-no-ignored.stdout"))
check(get("expected-no-unknown.stdout"))
check(get("expected-no-unchanged.stdout"))
check(get("expected-renames-both.stdout"))
check(get("expected-renames-source.stdout"))
check(get("expected-renames-target.stdout"))
check(get("expected-renames-target-no-ignored.stdout"))
check(get("expected-renames-target-no-unknown.stdout"))

include("common/test_utils_inventory.lua")

mkdir("source")
addfile("source/source_1", "source_1")
addfile("source/source_2", "source_2")
addfile("source/rename_source", "rename")
addfile("source/missing", "missing")
addfile("source/dropped", "dropped")
commit()

check(mtn("mv", "source/rename_source", "source/rename_target"), 0, true, false)
check(mtn("drop", "source/dropped"), 0, true, false)
remove("source/missing")

writefile("source/ignored_1", "ignored_1")
writefile("source/unknown_1", "unknown_1")
writefile("source/source_2", "source_2 changed")
addfile("source/added", "added")

--
-- First with no options
--
check(mtn("automate", "inventory", "source"), 0, true, false)
canonicalize("stdout")
check(readfile("expected-none.stdout") == readfile("stdout"))

--
-- check --no-ignored, --no-unchanged, --no-unknown
--
check(mtn("automate", "inventory", "source", "--no-unknown"), 0, true, false)
canonicalize("stdout")
check(readfile("expected-no-unknown.stdout") == readfile("stdout"))

check(mtn("automate", "inventory", "source", "--no-unchanged"), 0, true, false)
canonicalize("stdout")
check(readfile("expected-no-unchanged.stdout") == readfile("stdout"))

check(mtn("automate", "inventory", "source", "--no-ignored"), 0, true, false)
canonicalize("stdout")
check(readfile("expected-no-ignored.stdout") == readfile("stdout"))

-- make sure 'automate stdio' handles at least one of the inventory options as well
check(mtn("automate", "stdio"), 0, true, false, "o10:no-ignored0:e l9:inventory6:sourcee")
canonicalize("stdout")
check(("0:0:l:1149:" .. readfile("expected-no-ignored.stdout")) == readfile("stdout"))

--
-- now check --no-corresponding-renames
--
check(mtn("mv", "source", "target"), 0, false, false)

check(mtn("automate", "inventory", "source"), 0, true, false)
canonicalize("stdout")
check(readfile("expected-renames-both.stdout") == readfile("stdout"))

check(mtn("automate", "inventory", "target"), 0, true, false)
canonicalize("stdout")
check(readfile("expected-renames-both.stdout") == readfile("stdout"))

check(mtn("automate", "inventory", "source", "--no-corresponding-renames"), 0, true, false)
canonicalize("stdout")
check(readfile("expected-renames-source.stdout") == readfile("stdout"))

check(mtn("automate", "inventory", "target", "--no-corresponding-renames"), 0, true, false)
canonicalize("stdout")
check(readfile("expected-renames-target.stdout") == readfile("stdout"))

--
-- check how --no-corresponding-renames works with the other options
--

-- since we restrict to the rename target, all nodes in there should be
-- marked as changed
check(mtn("automate", "inventory", "target", "--no-corresponding-renames", "--no-unchanged"), 0, true, false)
canonicalize("stdout")
check(readfile("expected-renames-target.stdout") == readfile("stdout"))

check(mtn("automate", "inventory", "target", "--no-corresponding-renames", "--no-ignored"), 0, true, false)
canonicalize("stdout")
check(readfile("expected-renames-target-no-ignored.stdout") == readfile("stdout"))

check(mtn("automate", "inventory", "target", "--no-corresponding-renames", "--no-unknown"), 0, true, false)
canonicalize("stdout")
check(readfile("expected-renames-target-no-unknown.stdout") == readfile("stdout"))

-- end of file
