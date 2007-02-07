
mtn_setup()

addfile("a", "hello there")
check(mtn("--debug", "commit", "--message", "line 1", "--message", "line 2"), 0, false, false)
check(certvalue(base_revision(), "changelog") == "line 1\nline 2\n")
