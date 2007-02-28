mtn_setup()

--    root               - contains a, b, c
--     |  \
--     |   \
--     |    two_rev      - drop c, add d
--     |          |
--     one_rev    |      - edit a
--     |    \     |
--     |     \    |
--     |      \   |
--     |       merge 1
--     |
--     pluck_rev         - edit b
--
-- In a workspace at merge 1, we want to pluck the edit of b from pluck_rev.

-- set up root
addfile("a", "original\n")
addfile("b", "original\n")
addfile("c", "original\n")
commit()
root_rev = base_revision()

-- and one_rev
writefile("a", "updated\n")
commit()
one_rev = base_revision()

-- and pluck_rev
writefile("b", "updated\n")
commit()
pluck_rev = base_revision()

-- and two_rev
revert_to(root_rev)
addfile("d", "original\n")
check(mtn("drop", "c"), 0, false, false)
commit()
two_rev = base_revision()

-- and merge 1
check(mtn("explicit_merge", one_rev, two_rev, "testbranch"), 0, false, false)
check(mtn("update"), 0, false, false)

-- check that we need the edit for b
check(readfile("b") == "original\n", 0, false, false)
-- pluck the edit
check(mtn("pluck", "-r", pluck_rev), 0, false, false)
-- check that we go it
check(readfile("b") == "updated\n", 0, false, false)

-- check the pending rev; we don't expect to see changes from two_rev
check(mtn("automate", "get_revision"), 0, true, false)
check(not qgrep("delete", "stdout"))
check(not qgrep("add_file", "stdout"))

