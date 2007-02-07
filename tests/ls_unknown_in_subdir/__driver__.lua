mtn_setup()

mkdir("foo")
writefile("bar", "bar")
writefile("foo/a", "aaa")
writefile("foo/b", "bbb")

check(mtn("ls", "unknown", "foo"), 0, true, false)
check(grep('foo$', "stdout"), 0, false, false)
check(grep('foo/a$', "stdout"), 1, false, false)
check(grep('foo/b$', "stdout"), 1, false, false)

xfail(indir("foo", mtn("ls", "unknown")), 0, true, false)
check(grep('foo$', "stdout"), 0, false, false)
check(grep('foo/a$', "stdout"), 0, false, false)
check(grep('foo/b$', "stdout"), 0, false, false)

