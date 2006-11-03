
mtn_setup()

check(mtn("add", "test_hooks.lua"), 0, false, false)
check(mtn("ls", "known"), 0, true, false)
check(not qgrep("test_hooks.lua", "stdout"))
check(mtn("add", "--no-respect-ignore", "test_hooks.lua"), 0, false, false)
check(mtn("ls", "known"), 0, true, false)
check(qgrep("test_hooks.lua", "stdout"))
