mtn_setup()

mkdir("foo")
writefile("bar", "bar")
writefile("foo/a", "aaa")
writefile("foo/b", "bbb")

check(mtn("ls", "unknown", "foo"), 0, true, false)
check(qgrep('foo$', "stdout"), 0, false, false)
check(not qgrep('foo/a$', "stdout"))
check(not qgrep('foo/b$', "stdout"))

check(indir("foo", mtn("ls", "unknown")), 0, true, false)
check(qgrep('foo$', "stdout"))
check(not qgrep('foo/a$', "stdout"))
check(not qgrep('foo/b$', "stdout"))

