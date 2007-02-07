mtn_setup()

-- have to commit something, so that our restriction does not fail because it
-- doesn't include the root directory.
addfile("a", "some data")
commit()

writefile("a", "other data")

check(mtn("diff", "a", "_MTN"), 0, true, true)
check(not qgrep("_MTN", "stdout"))
check(qgrep("_MTN", "stderr"))
