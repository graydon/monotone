
mtn_setup()

remove("_MTN/log")

----------------------
--first, verify that --message-file actually works
----------------------

addfile("input.txt", "version 0 of the file")

writefile("msgfile.txt", "this commit uses the --message-file option")

check(mtn("--branch=testbranch", "commit", "--message-file=msgfile.txt"), 0, false, false)

tsha = base_revision()
check(mtn("ls", "certs", tsha), 0, true, false)
check(qgrep('this commit uses the --message-file option', "stdout"))

----------------------
--also with a file coming outside the workspace
----------------------
check(mtn("setup", "--branch=testbranch", "alt_wrk"), 0, false, false)

writefile("alt_wrk/input1.txt", "files... files...")

writefile("message-out-of-copy.txt", "out out out ")

check(indir("alt_wrk", mtn("add", "input1.txt")), 0, false, false)

check(indir("alt_wrk", mtn("--branch=outbranch", "commit", "--message-file=../message-out-of-copy.txt")), 0, false, false)

tsha = indir("alt_wrk", {base_revision})[1]()
check(indir("alt_wrk", mtn("ls", "certs", tsha)), 0, true, false)
check(qgrep('out out out', "stdout"))

----------------------
--start with the failures: non existing file
----------------------
addfile("input2.txt", "another file")

check(mtn("--branch=testbranch", "commit", "--message-file=to-be-or-not-to-be.txt"), 1, false, false)

----------------------
--then verify that --message and --message-file together cause an error
----------------------

check(mtn("--branch=testbranch", "commit", "--message-file=msgfile.txt",
          "--message=also a message"), 1, false, false)

-- --------------------
-- finally, --message-file and a filled _MTN/log should also fail
-- --------------------

check(get("commit_log.lua"))

writefile("_MTN/log", "Log entry")

check(mtn("--branch=testbranch", "--rcfile=commit_log.lua", "commit",
          "--message-file=msgfile.txt"), 1, false, false)
