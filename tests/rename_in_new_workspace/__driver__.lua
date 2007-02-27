mtn_setup()

check(mtn("rename", "--bookkeep-only", "unversioned", "also-unversioned"), 1, false, false)
check(mtn("status"), 0, false, false)
