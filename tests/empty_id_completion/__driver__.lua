
mtn_setup()

-- regression test: completing the revision "" doesn't crash
check(mtn("cat", "-r", "", "nosuchfile"), 1, false, false)
