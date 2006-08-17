
mtn_setup()

mkdir("foo")
writefile("file1", "x")
writefile("foo/bar", "y")

check(mtn("add", "file1"), 0, false, false)
check(mtn("add", "foo/bar"), 0, false, false)
check(mtn("ci", "-m", "x"), 0, false, false)

check(mtn("refresh_inodeprints"))
append("file1", "a")
append("foo/bar", "b")
check(mtn("ci", "--exclude", "foo", "-m", 'x'), 0, false, false)
check(mtn("status"), 0, true)
check(qgrep("foo/bar", "stdout"))
check(not qgrep("file1", "stdout"))
