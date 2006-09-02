
mtn_setup()

-- First of all, add an empty file.
writefile("foo1")
check(mtn("add", "foo1"), 0, false, false)
commit()

-- Add some contents to the previously added file.
writefile("foo1", "Some contents.")
commit()

rev = base_revision()

-- Verify that the latest revision contains a patch, rather than a delete/add
-- sequence (as reported in bug #9964).

check(mtn("automate", "get_revision", rev), 0, true, false)

check(grep('^patch "foo1"$', "stdout"), 0, false, false)
check(not qgrep("'add'", "stdout"))
check(not qgrep("'delete'", "stdout"))
