
mtn_setup()

mkdir("foo")

writefile("foo/foo.db", "foo file")

check(indir("foo", mtn("--db=", "ls", "keys")), 1, false, false)
check(indir("foo", mtn("--db=foo.db", "ls", "keys")), 1, false, false)
check(indir("foo", mtn("--db=missing.db", "ls", "keys")), 1, false, false)
