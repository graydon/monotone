-- Test 'automate inventory' options --no-ignored, --no-unchanged, --no-unknown

mtn_setup()

--  override standard test_hooks.lua, because 'automate stdio' uses it.
check(get("test_hooks.lua"))

check(get("expected-none.stdout"))
check(get("expected-no-ignored.stdout"))
check(get("expected-no-unknown.stdout"))
check(get("expected-no-unchanged.stdout"))

include ("common/test_utils_inventory.lua")

mkdir ("source")
addfile("source/source_1", "source_1")
addfile("source/source_2", "source_2")
commit()

writefile("source/ignored_1", "ignored_1")
writefile("source/unknown_1", "unknown_1")
writefile ("source/source_2", "source_2 changed")

-- First with no options
check(mtn("automate", "inventory", "source"), 0, true, false)
canonicalize("stdout")
check (readfile("expected-none.stdout") == readfile("stdout"))

check(mtn("automate", "inventory", "source", "--no-ignored"), 0, true, false)
canonicalize("stdout")
check (readfile("expected-no-ignored.stdout") == readfile("stdout"))

-- make sure 'automate stdio' handles inventory options
check(mtn("automate", "stdio"), 0, true, false, "o10:no-ignored0:e l9:inventory6:sourcee")
canonicalize("stdout")
check (("0:0:l:364:" .. readfile("expected-no-ignored.stdout")) == readfile("stdout"))

check(mtn("automate", "inventory", "source", "--no-unknown"), 0, true, false)
canonicalize("stdout")
check (readfile("expected-no-unknown.stdout") == readfile("stdout"))

check(mtn("automate", "inventory", "source", "--no-unchanged"), 0, true, false)
canonicalize("stdout")
check (readfile("expected-no-unchanged.stdout") == readfile("stdout"))

-- end of file
