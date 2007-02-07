
mtn_setup()

mkdir("foo")
mkdir("bar")

writefile("foo/foo.txt", "file foo.txt in dir foo")
writefile("bar/bar.txt", "file bar.txt in dir bar")

check(mtn("add", "-R", "foo"), 0, false, false)
check(mtn("add", "-R", "bar"), 0, false, false)

commit()

writefile("foo/foo.txt", "file foo.txt in dir foo changed")
writefile("bar/bar.txt", "file bar.txt in dir bar changed")

-- should have tests for 
-- add, drop, rename, revert
--       - which use prefixing
-- ls unknown, ignored, missing
--       - which use add_restriction and in_restriction directly
-- commit, diff, status 
--       - which use add_restriction and in_restriction through 
--         restrict_patch_set

check(mtn("status"), 0, true, 0)
check(qgrep("foo/foo", "stdout"))
check(qgrep("bar/bar", "stdout"))

chdir("foo")
check(mtn("--norc", "status"), 0, true)
check(qgrep("foo/foo", "stdout"))
check(qgrep("bar/bar", "stdout"))

check(mtn("--norc", "status", "."), 0, true)
check(qgrep("foo/foo", "stdout"))
check(not qgrep("bar/bar", "stdout"))

check(mtn("--norc", "status", ".."), 0, true)
check(qgrep("foo/foo", "stdout"))
check(qgrep("bar/bar", "stdout"))
chdir("..")

chdir("bar")
check(mtn("--norc", "status"), 0, true)
check(qgrep("foo/foo", "stdout"))
check(qgrep("bar/bar", "stdout"))

check(mtn("--norc", "status", "."), 0, true)
check(not qgrep("foo/foo", "stdout"))
check(qgrep("bar/bar", "stdout"))

check(mtn("--norc", "status", ".."), 0, true)
check(qgrep("foo/foo", "stdout"))
check(qgrep("bar/bar", "stdout"))
chdir("..")

-- TODO: test a.c a.h a/foo.c a/foo.h from inside and outside of a
