-- In this test, we put things in .mtn-ignore that trigger every
-- possible syntax error message from the regular expression library,
-- to ensure that the user's view of these errors is sensible.
--
-- It is just too error prone to try to grep for actual error message
-- texts, so instead we check for functionality (despite the syntax
-- errors, "ignoreme" and "ignoremetoo" should be ignored,
-- "dontignoreme" not) and for the presence of at least *some* error
-- messages on stderr.

mtn_setup()

writefile("ignoreme")
writefile("ignoremetoo")
writefile("dontignoreme")
check(get("mtn-ignore", ".mtn-ignore"))

check(raw_mtn("ls", "unknown"), 0, true, true)

check(qgrep("^dontignoreme$", "stdout"))
check(not qgrep("^ignoreme$", "stdout"))
check(not qgrep("^ignoremetoo$", "stdout"))

check(qgrep("error near char", "stderr"))
check(qgrep("skipping this regex", "stderr"))

check(raw_mtn("ls", "ignored"), 0, true, true)

check(not qgrep("^dontignoreme$", "stdout"))
check(qgrep("^ignoreme$", "stdout"))
check(qgrep("^ignoremetoo$", "stdout"))

check(qgrep("error near char", "stderr"))
check(qgrep("skipping this regex", "stderr"))
