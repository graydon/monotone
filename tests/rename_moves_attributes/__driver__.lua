
mtn_setup()

addfile("testfile", "foo bar")
check(mtn("attr", "set", "testfile", "some_key", "some_value"), 0, false, false)
check(mtn("attr", "get", "testfile"), 0, true, false)
check(qgrep("some_key", "stdout"))
check(qgrep("some_value", "stdout"))

commit()

check(mtn("rename", "--bookkeep-only", "testfile", "otherfile"), 0, false, false)
rename("testfile", "otherfile")
commit()

-- Create a new testfile, so 'attr get' has a chance to succeed
addfile("testfile", "thing stuff")
check(mtn("attr", "get", "testfile"), 0, true, false)
check(not qgrep("some_key", "stdout"))
check(not qgrep("some_value", "stdout"))
check(mtn("attr", "get", "otherfile"), 0, true, false)
check(qgrep("some_key", "stdout"))
check(qgrep("some_value", "stdout"))
