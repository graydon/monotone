
mtn_setup()

getfile("test.lua")
check(mtn("setup", "--rcfile=test.lua", "--branch=testbranch", "subdir"), 0, true, false)
check(qgrep("asdfghjkl", "stdout"))
check(qgrep("qwertyuiop", "stdout"))
