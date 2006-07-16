
skip_if(not existsonpath("chmod"))
mtn_setup()

addfile("testfile", "blah blah")

-- Make it unwriteable, so our  edit_comment hook will have a chance to
-- run, but  the overall commit  will fail.  (How  do I know  this will
-- work?  Well, it did...)
check({"chmod", "a-w", "test.db"})

check(get("my_hook.lua"))

check(mtn("commit", "-btestbranch", "--rcfile=my_hook.lua"), 1, false, false)

check(samelines("_MTN/log", {"foobar"}))

remove("_MTN/log")
writefile("_MTN/log", "")

-- -m messages don't get written out to _MTN/log
-- (if they do, it breaks the workflow:
--   $ mtn commit -m 'foo'
--   <fails>
--   <fixup>
--   $ mtn commit -m 'foo'
--   error, _MTN/log non-empty and -m specified
check(mtn("commit", "-btestbranch", "-m", "blah blah"), 1, false, false)

-- So _MTN/log should still be empty
check(fsize("_MTN/log") == 0)
