--
-- These tests ensure that a workspace which is affected by a revision
-- removal is gracefully handled
--

mtn_setup()

addfile("first", "first")
commit()
first = base_revision()

addfile("second", "second")
commit()
second = base_revision()

-- ensure that the revision cannot be removed if there are changes...
addfile("third", "third")
check(mtn("db", "kill_rev_locally", second), 1, false, false)
check(mtn("drop", "third"), 0, false, false)
-- ...or missing files in the workspace
remove("second")
check(mtn("db", "kill_rev_locally", second), 1, false, false)
check(mtn("revert", "."), 0, false, false)

-- now check that the revision is properly recreated if the revision
-- is killed for real...
check(mtn("db", "kill_rev_locally", second), 0, false, false)
check(base_revision() == first)
-- ...and if the changes can be recommitted so the same revision is created
commit()
check(base_revision() == second)


-- finally ensure that the workspace is not touched if we kill
-- a revision which is not the base of the current workspace
check(mtn("update", "-r", first), 0, false, false)
addfile("fourth", "fourth")
check(mtn("db", "kill_rev_locally", second), 0, false, false)

