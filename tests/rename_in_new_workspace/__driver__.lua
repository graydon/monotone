mtn_setup()

check(mtn("rename", "unversioned", "also-unversioned"), 1, false, false)
check(mtn("status"), 0, false, false)
