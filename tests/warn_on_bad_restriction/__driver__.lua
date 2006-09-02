
mtn_setup()

check(mtn("diff", "bogusdir1", "bogusdir2"), 1, false, true)
