--
-- For more information on this bug, see 
-- https://savannah.nongnu.org/bugs/?func=detailitem&item_id=15994
--

mtn_setup()

-- at first we create a directory and a file and commit both
addfile("foo", "foofile")
mkdir("bar");
check(mtn("add", "bar"), 0, false, false)
commit()

-- now rename the directory, change the file and move it into the renamed
-- directory
check(mtn("rename", "bar", "baz"), 0, false, false)
writefile("foo", "bazfile")
check(mtn("rename", "foo", "baz/foo"), 0, false, false)

-- if we try to check-in these changes and restrict on baz/foo, we'll hit an 
-- invariant in roster.cc, around line 188
-- note that we do _not_ hit this invariant iff
--  a) the foo file is not changed
--  b) we only restrict on bar
--  c) we only restrict on baz
xfail(mtn("commit", "baz/foo"), 1, false, false)

