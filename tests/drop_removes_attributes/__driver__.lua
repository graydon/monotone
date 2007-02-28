
mtn_setup()

addfile("testfile", "foo bar")
check(mtn("attr", "set", "testfile", "some_key", "some_value"), 0, false, false)
check(mtn("attr", "get", "testfile"), 0, true, false)
check(qgrep("some_key", "stdout"))
check(qgrep("some_value", "stdout"))

commit()

check(mtn("drop", "--bookkeep-only", "testfile"), 0, false, false)
check(mtn("attr", "get", "testfile"), 1, true, false)
