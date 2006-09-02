
mtn_setup()

check(get("commit_log.lua"))

writefile("_MTN/log", "Log entry")

writefile("input.txt", "version 0 of the file")

check(mtn("add", "input.txt"), 0, false, false)

check(mtn("--branch=testbranch", "--rcfile=commit_log.lua", "commit", "--message=Cause me an error"), 1, false, false)
