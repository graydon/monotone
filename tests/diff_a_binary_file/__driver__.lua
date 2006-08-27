
mtn_setup()

check(get("binary"))

check(mtn("add", "binary"), 0, false, false)
check(mtn("status"), 0, false, false)
check(mtn("diff"), 0, true, false)
check(not qgrep("[^[:print:]]", "stdout"))
