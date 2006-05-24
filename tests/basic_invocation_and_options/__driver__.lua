
mtn_setup()

check(prepare(raw_mtn("--norc")), 2, false)
check(prepare(raw_mtn("--help")), 2, false)
check(prepare(raw_mtn("--version")), 0, false)
check(prepare(raw_mtn("--nostd", "--help")), 2, false)
check(prepare(raw_mtn("--norc", "--help")), 2, false)
check(prepare(raw_mtn("--debug", "--help")), 2, false, false)
check(prepare(raw_mtn("--quiet", "--help")), 2, false)
check(prepare(raw_mtn("--db=foo.db", "--help")), 2, false)
check(prepare(raw_mtn("--db", "foo.db", "--help")), 2, false)
check(prepare(raw_mtn("--key=someone@foo.com", "--help")), 2, false)
check(prepare(raw_mtn("--key", "someone@foo.com", "--help")), 2, false)
