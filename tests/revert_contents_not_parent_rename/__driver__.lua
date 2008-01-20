mtn_setup()

-- this was a bug until 0.38 and appeared to be related to
-- tests/restricted_diff_bug, however was resolved by some clever
-- path handling tricks in revert

mkdir("dir1")
addfile("dir1/test.txt", "booya")
commit()

original=sha1("dir1/test.txt")

check(mtn("mv", "dir1", "dir2"), 0, false, false)
writefile("dir2/test.txt", "boohoo")

-- presumably this should only revert the content of test.txt
-- because it only includes that node

check(mtn("revert", "dir2/test.txt"), 0, false, false)

reverted=sha1("dir2/test.txt")
check(original == reverted)


