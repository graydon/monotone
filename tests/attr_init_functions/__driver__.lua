
mtn_setup()

check(get("hook.lua"))

writefile("testfile", "foo")
check(mtn("--rcfile=hook.lua", "add", "testfile"), 0, false, false)
writefile("magic", "stuff")
check(mtn("--rcfile=hook.lua", "add", "magic"), 0, false, false)

check(mtn("attr", "get", "testfile"), 0, true, false)
check(not qgrep("test:test_attr", "stdout"))
check(not qgrep("bob", "stdout"))

check(mtn("attr", "get", "magic"), 0, true, false)
check(qgrep("test:test_attr", "stdout"))
check(qgrep("bob", "stdout"))
