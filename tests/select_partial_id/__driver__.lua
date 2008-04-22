
mtn_setup()

addfile("testfile", "this is just a file")
commit()
rev = base_revision()

-- Check that automate select returns the correct id when given a partial one
check(mtn("automate", "select", string.sub(rev,1,8)), 0, true, false)
check(grep(rev, "stdout"), 0, false, false)
