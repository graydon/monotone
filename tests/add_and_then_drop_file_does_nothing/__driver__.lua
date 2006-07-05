
mtn_setup()

addfile("maude", "the file maude\n")
check(mtn("drop", "maude"), 0, false, false)
check(mtn("status"), 0, true)
check(not qgrep("_file", "stdout"))

addfile("liver", "the file liver\n")
check(mtn("drop", "liver"), 0, false, false)
check(mtn("status"), 0, true)
check(not qgrep("_file", "stdout"))
