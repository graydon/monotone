
mtn_setup()

addfile("testfile", "flagella")
check(mtn("diff"), 0, true, false)
check(qgrep("testfile", "stdout"))
check(qgrep("flagella", "stdout"))
