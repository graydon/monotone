
mtn_setup()

remove("_MTN/log")

writefile("input.txt", "version 0 of the file")

check(mtn("add", "input.txt"), 0, false, false)

commit("testbranch", "Log entry")

tsha = base_revision()
check(mtn("ls", "certs", tsha), 0, true, false)
check(qgrep('Log entry', "stdout"))
