mtn_setup()

-- create the parent revision
addfile("foo", "blabla")
commit()
parent = base_revision()

-- do an unrelated change
addfile("bla", "blabla")
commit()
child = base_revision()

-- create some divergence
revert_to(parent)
check(mtn("attr", "set", "foo", "name", "value"), 0, false, false)
commit()

-- ensure that there are no changes in the current workspace
check(mtn("status"), 0, true, false)
check(qgrep("no changes", "stdout"))

-- update to child
check(mtn("update", "-r", child), 0, false, false)
check(mtn("status"), 0, true, false)
xfail(qgrep("no changes", "stdout"))

