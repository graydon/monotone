mtn_setup()

addfile("foo", "blah blah")
commit("mybranch")

-- this should first guess the branch to be 'mybranch', and then use that as
-- the default checkout directory name
check(mtn("checkout", "-r", "h:mybranch"), 0, false, false)
check(exists("mybranch"))
check(readfile("foo") == readfile("mybranch/foo"))

-- but now that that directory exists, it should fail
check(mtn("checkout", "-r", "h:mybranch"), 1, false, false)
check(mtn("checkout", "-r", "h:mybranch", "otherdir"), 0, false, false)
