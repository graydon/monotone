
mtn_setup()

check(mtn("ls", "missing"), 0, nil, false)

writefile("foo", "the foo file")
writefile("bar", "the bar file")
check(mtn("add", "foo", "bar"), 0, false, false)

check(mtn("ls", "known"), 0, true)
check(sort("stdout"), 0, "bar\nfoo\n")

mkdir("dir")
writefile("dir/foo", "the foo file")
writefile("dir/bar", "the bar file")
check(mtn("add", "dir/foo", "dir/bar"), 0, false, false)

check(mtn("ls", "known"), 0, true)
check(sort("stdout"), 0, "bar\ndir\ndir/bar\ndir/foo\nfoo\n")

check(mtn("--branch=testbranch", "commit", "--message=committed"), 0, false, false)

check(mtn("ls", "known"), 0, true)
check(sort("stdout"), 0, "bar\ndir\ndir/bar\ndir/foo\nfoo\n")

check(mtn("drop", "--bookkeep-only", "foo"), 0, false, false)
check(mtn("rename", "dir", "dir2"), 0, false, false)
check(mtn("rename", "bar", "baz"), 0, false, false)

check(mtn("ls", "known"), 0, true)
check(sort("stdout"), 0, "baz\ndir2\ndir2/bar\ndir2/foo\n")
