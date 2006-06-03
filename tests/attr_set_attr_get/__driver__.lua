
mtn_setup()

addfile("testfile", "foo bar")
check(mtn("attr", "set", "testfile", "test:unique_key", "unique_value"), 0, false, false)
check(mtn("attr", "get", "testfile"), 0, true, false)
check(qgrep("test:unique_key", "stdout"))
check(qgrep("unique_value", "stdout"))
check(mtn("attr", "get", "testfile", "test:unique_key"), 0, true, false)
check(qgrep("unique_value", "stdout"))

commit()
