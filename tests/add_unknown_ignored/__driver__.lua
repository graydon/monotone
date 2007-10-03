
mtn_setup()

check(mtn("add", "--unknown"), 0, false, false)
check(mtn("ls", "known"), 0, true, false)
check(not qgrep("test_hooks.lua", "stdout"))
check(mtn("add", "--unknown", "--no-respect-ignore"), 0, false, false)
check(mtn("ls", "known"), 0, true, false)
check(qgrep("test_hooks.lua", "stdout"))
