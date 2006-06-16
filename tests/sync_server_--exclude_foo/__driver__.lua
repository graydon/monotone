
mtn_setup()

-- check that sync with no includes doesn't I()
check(mtn("sync", "127.0.0.9:65528", "--exclude=foo"), 1, false, false)
