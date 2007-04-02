
include("/common/netsync.lua")
mtn_setup()
netsync.setup()

-- Check that we can netsync a 32MB file.

write_large_file("largish", 32)

check(mtn("add", "largish"), 0, false, false)
commit()
file = sha1("largish")

srv = netsync.start()

srv:pull("testbranch")

srv:stop()

check(mtn("--db=test2.db", "--branch=testbranch", "checkout", "other"), 0, false, false)
check(file == sha1("other/largish"))
