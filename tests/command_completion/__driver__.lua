
mtn_setup()

check(mtn("status"), 0, false, false)
check(mtn("st"), 0, false, false)
check(mtn("s"), 1, false, false)

check(mtn("diff"), 0, false, false)
check(mtn("dif"), 0, false, false)
check(mtn("di"), 1, false, false)
