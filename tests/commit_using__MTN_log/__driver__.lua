
mtn_setup()

check(get("commit_log.lua"))
check(get("commit_log_modified_return.lua"))

writefile("_MTN/log", "Log entry")

writefile("input.txt", "version 0 of the file")

check(mtn("add", "input.txt"), 0, false, false)

--this should now fail, given that the log file has content and we don't
--remove the 'magic' line
check(mtn("--branch=testbranch", "--rcfile=commit_log.lua", "commit"), 1, false, true)
check(qgrep('magic line; commit cancelled', "stderr"))

check(exists("_MTN/log"))
check(fsize("_MTN/log") > 0)

--this should pass, as the lua hook now returns a string that doesn't contain
--the 'magic' line
check(mtn("--branch=testbranch", "--rcfile=commit_log_modified_return.lua", "commit"), 0, false, false)

tsha = base_revision()
check(exists("_MTN/log"))
check(fsize("_MTN/log") == 0)
check(mtn("ls", "certs", tsha), 0, true, false)
check(qgrep("Log Entry", "stdout"))
