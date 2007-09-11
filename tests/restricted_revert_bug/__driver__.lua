mtn_setup()

-- this bug appears to be related to tests/restricted_diff_bug

mkdir("dir1")
addfile("dir1/test.txt", "booya")
commit()

original=sha1("dir1/test.txt")

check(mtn("mv", "dir1", "dir2"), 0, false, false)
writefile("dir2/test.txt", "boohoo")

-- presumably this should only revert the content of test.txt
-- because it only includes that node
-- note that it is the parents name that has changed

check(mtn("revert", "dir2/test.txt"), 0, false, false)

reverted=sha1("dir2/test.txt")
xfail(original == reverted)


