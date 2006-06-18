
mtn_setup()

mkdir("foo")

check(mtn("status"), 0, false, false)
check(mtn("status", "--root", "."), 0, false, false)

check(indir("foo", mtn("status", "--root", "..")), 0, false, false)
check(indir("foo", mtn("status", "--root", ".")), 1, false, false)

-- workspace outside of root
check(mtn("status", "--root", "/tmp"), 1, false, false)

-- root below workspace
check(mtn("status", "--root", "foo"), 1, false, false)
