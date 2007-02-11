
mtn_setup()

writefile("foo", "some data")
-- Check a single character filename too, because those have had bugs.
writefile("a", "some data")

check(mtn("add", "foo"), 0, false, false)
check(mtn("add", "a"), 0, false, false)
check(mtn("attr", "set", "foo", "test:test_attr", "true"), 0, false, false)
check(mtn("attr", "set", "a", "test:test_attr", "1"), 0, false, false)
commit()
co_r_sha1 = base_revision()

check(mtn("attr", "drop", "foo", "test:test_attr"), 0, false, false)
check(mtn("attr", "set", "a", "test:test_attr", "2"), 0, false, false)
commit()
update_r_sha1 = base_revision()

-- Check checkouts.
remove("co-dir")
check(mtn("checkout", "--revision", co_r_sha1, "co-dir"), 0, true, true)
check(qgrep("test:test_attr:foo:true", "stdout"))
check(qgrep("test:test_attr:a:1", "stdout"))

-- Check updates.
remove("co-dir")
check(mtn("checkout", "--revision", update_r_sha1, "co-dir"), 0, true, true)
check(not qgrep("test:test_attr:foo", "stdout"))
check(qgrep("test:test_attr:a:2", "stdout"))

-- check that files must exist to have attributes set
check(mtn("attr", "set", "missing", "mtn:execute"), 1, false, false)
