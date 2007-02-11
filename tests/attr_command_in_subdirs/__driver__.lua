
mtn_setup()

mkdir("foo")
mkdir("foo/bar")
writefile("top", "top data")
writefile("foo/foodata", "foo data")
writefile("foo/bar/bardata", "foobar data")

check(mtn("add", "-R", "top", "foo"), 0, false, false)

-- set attributes in directories

check(indir("foo", mtn("attr", "set", "foodata", "test:test_attr", "true")), 0, false, false)
check(indir("foo/bar", mtn("attr", "set", "bardata", "test:test_attr", "false")), 0, false, false)
commit()
rev = base_revision()

-- see if they're right

check(mtn("checkout", "--revision", rev, "co-dir"), 0, true, true)
check(qgrep("test:test_attr:foo/foodata:true", "stdout"))
check(qgrep("test:test_attr:foo/bar/bardata:false", "stdout"))
