
mtn_setup()

writefile("input.txt", "random content")

check(mtn("add", "input.txt"), 0, false, false)

check(mtn("--branch=testbranch", "--log=log.log", "commit", "-m", "test"), 0, true, false)

check(not qgrep('^mtn:', "stdout"))
check(qgrep('^mtn:', "log.log"))
