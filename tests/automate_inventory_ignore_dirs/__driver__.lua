-- Test that 'automate inventory' properly ignores directories given in .mtn_ignore

mtn_setup()

check(get("local_hooks.lua"))
check(get("expected.stderr"))
check(get("expected.stdout"))

include ("common/test_utils_inventory.lua")

----------
-- The local local_hooks.lua defines ignore_file to ignore 'ignored'
-- directory, 'source/ignored_1' file. It also writes the name of each
-- file checked to stderr. So we check to see that it does _not_ write
-- the names of the files in the ignored directory.

mkdir ("source")
addfile("source/source_1", "source_1")
addfile("source/source_2", "source_2")
writefile("source/ignored_1", "source ignored_1")

mkdir ("source/ignored_dir")
writefile ("source/ignored_dir/file_1", "ignored file 1")
writefile ("source/ignored_dir/file_2", "ignored file 2")

check(mtn("automate", "inventory", "--rcfile=local_hooks.lua", "source"), 0, true, false)

canonicalize("stdout")
canonicalize("ts-stderr")

check (readfile("expected.stderr") == readfile("ts-stderr"))
check (readfile("expected.stdout") == readfile("stdout"))

-- end of file
