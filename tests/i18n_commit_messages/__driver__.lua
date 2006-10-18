-- Note that a significant part of the logic for this test lives in
-- extra_hooks.lua

mtn_setup()

get("extra_hooks.lua")
get("utf8.txt")
get("euc-jp.txt")

addfile("testfile", "blah blah")

set_env("CHARSET", "euc-jp")
check(mtn("attr", "set", "testfile", "testattr", readfile("euc-jp.txt")),
      0, false, false)

-- First, we do a commit that should _fail_, because validate_commit_message
-- fires.  but it should trigger a writeout of the log message to _MTN/log
f = io.open("fail_comment", "w")
f:close()
check(exists("fail_comment"))
check(mtn("commit", "--rcfile", "extra_hooks.lua"), 1, true, false)
check(qgrep("EDIT: BASE GOOD", "stdout"))
check(qgrep("EDIT: MSG NONESUCH", "stdout"))
check(qgrep("VALIDATE: MSG GOOD", "stdout"))
check(qgrep("VALIDATE: REV GOOD", "stdout"))

-- Now the localized message should have been written out to _MTN/log
check(readfile("euc-jp.txt") == readfile("_MTN/log"))

-- and if we commit again, both hooks should succeed
remove("fail_comment")
check(mtn("commit", "--rcfile", "extra_hooks.lua"), 0, true, false)
check(qgrep("EDIT: BASE GOOD", "stdout"))
check(qgrep("EDIT: MSG GOOD", "stdout"))
check(qgrep("VALIDATE: MSG GOOD", "stdout"))
check(qgrep("VALIDATE: REV GOOD", "stdout"))

-- and if we look at the cert, it should be in unicode
rev = base_revision()
check(mtn("automate", "certs", rev), 0, true, false)
check(qgrep(readfile("utf8.txt"), "stdout"))
