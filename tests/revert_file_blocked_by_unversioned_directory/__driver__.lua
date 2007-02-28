
mtn_setup()

-- this test is a bug report
--
-- reverting a file that has been replaced by a (non-versioned) directory
-- should do something sensible. I'm not sure what that is though.
-- this is almost a working copy conflict but it seems silly that revert
-- would ever encounter a conflict.

addfile("foo", "foo")

commit()

check(mtn("mv", "foo", "bar"), 0, false, false)

-- create directory blocking revert of foo
mkdir("foo")

xfail_if(true, mtn("revert", "."), 0, false, false)
