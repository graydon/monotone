mtn_setup()

addfile("foo", "bar")
check(mtn("commit", "-mx", "--no-workspace"), 1, false, false)
check(mtn("commit", "-mx"), 0, false, false)