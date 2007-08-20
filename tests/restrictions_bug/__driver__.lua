mtn_setup()

-- test a bug reported a while ago by Rob Schoening
--
-- Steps to reproduce on Windows & *nix:
--
-- 1) Given a directory and file dir1/test.txt
-- 2) mtn mv dir1 dir2
-- 3) edit dir2/test.txt & change content
-- 4) mtn diff dir2/test.txt
-- 
-- Note that (4) will not fail unless you perform both
-- steps (2) and (3).
-- 
-- Note also that running "mtn diff" with no path
-- argument works fine.

mkdir("dir1")
addfile("dir1/test.txt", "booya")
commit()

check(mtn("mv", "dir1", "dir2"), 0, false, false)
writefile("dir2/test.txt", "boohoo")
check(mtn("diff"), 0, false, false)
xfail(mtn("diff", "dir2/test.txt"), 0, false, false)

