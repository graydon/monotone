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
--
-- The problem here is that when diff is restricted
-- to "dir2/test.txt" (node 3) the roster does not *have* 
-- anything called "dir2" because that rename has been 
-- excluded. The file is still called "dir1/test.txt" under
-- the restriction. This is rather odd, since we restricted 
-- it using the name "dir2/test.txt" which is valid under
-- the full post-state roster. The bad name exists in 
-- the restricted cset and faults in check_restricted_cset.
--
-- the original roster contains
-- node 1 (root)
-- node 2 (dir2)
-- node 3 (test.txt)
--
-- the restricted roster contains
-- node 1 (root)
-- node 2 (dir1)
-- node 3 (test.txt)
--
-- the restricted cset contains
-- patch "dir2/test.txt"
--  from [...]
--    to [...]
--
-- so it looks like the restricted cset is bad.

mkdir("dir1")
addfile("dir1/test.txt", "booya")
commit()

check(mtn("mv", "dir1", "dir2"), 0, false, false)
writefile("dir2/test.txt", "boohoo")
check(mtn("diff"), 0, false, false)
check(mtn("diff", "dir2/test.txt"), 1, false, false)

