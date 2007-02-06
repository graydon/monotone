mtn_setup()

addfile("a", "some data")

check(mtn("diff", "a", "_MTN"), 0, true, true)
check(not qgrep("_MTN", "stdout"))
check(qgrep("_MTN", "stderr"))
