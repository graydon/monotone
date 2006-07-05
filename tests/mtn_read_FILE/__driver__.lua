
mtn_setup()

check(mtn("db", "init", "--db=foo.mtn"), 0, false, false)
mkdir("foo")
check(mtn("--db=foo.mtn", "--keydir=foo", "genkey", "foo"), 0, false, false, string.rep("foo\n", 2))

check(mtn("--db=foo.mtn", "--keydir=foo", "pubkey", "foo"), 0, true, false)
rename("stdout", "foo.keyfile")
check(mtn("read", "nonexistent"), 1, false, false)
check(mtn("read", "foo.keyfile"), 0, false, false)

check(mtn("ls", "keys"), 0, true, false)
check(qgrep("foo", "stdout"))
