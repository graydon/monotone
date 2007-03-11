mtn_setup()

-- create the parent revision
addfile("foo", "blabla")
commit()
parent=base_revision()


-- do an unrelated change
addfile("bla", "blabla")
commit()
R1=base_revision()

-- create some divergence
check(mtn("update", "-r", parent), 0, false, false)
check(mtn("attr", "set", "foo", "name", "value"), 0, false, false)
commit()
R2=base_revision()

-- ensure that there are no changes in the current workspace
check(mtn("status"), 0, true, false)
check(qgrep("no changes", "stdout"))

-- update to R1
check(mtn("update", "-r", R1), 0, false, false)
check(mtn("status"), 0, true, false)
xfail(qgrep("no changes", "stdout"))

