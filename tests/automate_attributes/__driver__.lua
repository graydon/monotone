
mtn_setup()

addfile("testfile", "foo bar")
check(mtn("attr", "set", "testfile", "unique_key", "unique_value"), 0, false, false)
check(mtn("automate", "attributes", "testfile"), 0, true, false)
check(qgrep("unique_key", "stdout"))
check(mtn("automate", "attributes"), 0, true, false)
check(qgrep("testfile", "stdout"))
