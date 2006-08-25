
mtn_setup()

check(get("commit_log.lua"))

writefile("_MTN/log", "Log entry")

writefile("input.txt", "version 0 of the file")

check(mtn("add", "input.txt"), 0, false, false)

check(mtn("--branch=testbranch", "--rcfile=commit_log.lua", "commit"), 0, false, false)

tsha = base_revision()
check(exists("_MTN/log"))
check(fsize("_MTN/log") == 0)
check(mtn("ls", "certs", tsha), 0, true, false)
check(qgrep('Log entry', "stdout"))
