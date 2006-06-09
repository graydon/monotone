
mtn_setup()

-- This tests the db kill_rev_locally command

-- Prepare a db with two revisions
addfile("testfile", "blah blah")
commit()
ancestor = base_revision()

writefile("testfile", "stuff stuff")
commit()
child = base_revision()

-- trying to kill the ancestor. This *is supposed to fail*
check(mtn("db", "kill_rev_locally", ancestor), 1, false, false)
check(mtn("automate", "get_revision", ancestor), 0, false, false)
check(mtn("db", "check"), 0, false, false)

-- killing children is ok, though :)
check(mtn("automate", "get_revision", child), 0, false, false)
check(mtn("db", "kill_rev_locally", child), 0, false, false)
check(mtn("automate", "get_revision", child), 1, false, false)
check(mtn("db", "check"), 0, false, false)
