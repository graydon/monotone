
mtn_setup()

addfile("testfile", "blah blah")
commit()

addfile("otherfile", "stuff stuff")

writefile("_MTN/log", "message message")

copy("_MTN/log", "good_log")
copy("_MTN/revision", "good_revision")

check(get("bad_edit_comment.lua"))

check(mtn("commit", "--rcfile=bad_edit_comment.lua"), 1, false, false)

-- Since this commit was canceled due to a problem with the log
-- message, the old log message have been preserved
check(samefile("_MTN/log", "good_log"))

-- The working revision should be unchanged.
check(samefile("_MTN/revision", "good_revision"))
