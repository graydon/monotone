
mtn_setup()

check(get("on.lua"))
check(get("off.lua"))

check(not exists("_MTN/inodeprints"))
addfile("testfile", "blah blah")
commit()
rev = base_revision()

check(mtn("setup", "--branch=testbranch", "setup_default"), 0, false, false)
check(not exists("setup_default/_MTN/inodeprints"))
check(mtn("checkout", "--revision", rev, "co_default"), 0, false, false)
check(not exists("co_default/_MTN/inodeprints"))

check(mtn("--rcfile=off.lua", "setup", "--branch=testbranch", "setup_off"), 0, false, false)
check(not exists("setup_off/_MTN/inodeprints"))
check(mtn("--rcfile=off.lua", "checkout", "--revision", rev, "co_off"), 0, false, false)
check(not exists("co_off/_MTN/inodeprints"))

check(mtn("--rcfile=on.lua", "setup", "--branch=testbranch", "setup_on"), 0, false, false)
check(exists("setup_on/_MTN/inodeprints"))
check(mtn("--rcfile=on.lua", "checkout", "--revision", rev, "co_on"), 0, false, false)
check(exists("co_on/_MTN/inodeprints"))
check(fsize("co_on/_MTN/inodeprints") ~= 0)
