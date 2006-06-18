
mtn_setup()

check(mtn(), 2, false, false)
check(mtn("--norc"), 2, false, false)
check(mtn("--rcfile=test_hooks.lua"), 2, false, false)

check(mtn("--unknown-option"), 1, false, false)
check(mtn("--rcfile"), 1, false, false)
