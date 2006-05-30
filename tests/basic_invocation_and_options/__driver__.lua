
mtn_setup()

check(cmd(raw_mtn("--norc")), 2, false)
check(cmd(raw_mtn("--help")), 2, false)
check(cmd(raw_mtn("--version")), 0, false)
check(cmd(raw_mtn("--nostd", "--help")), 2, false)
check(cmd(raw_mtn("--norc", "--help")), 2, false)
check(cmd(raw_mtn("--debug", "--help")), 2, false, false)
check(cmd(raw_mtn("--quiet", "--help")), 2, false)
check(cmd(raw_mtn("--db=foo.db", "--help")), 2, false)
check(cmd(raw_mtn("--db", "foo.db", "--help")), 2, false)
check(cmd(raw_mtn("--key=someone@foo.com", "--help")), 2, false)
check(cmd(raw_mtn("--key", "someone@foo.com", "--help")), 2, false)
