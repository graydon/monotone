
mtn_setup()

mkdir("subdir")

writefile("subdir/testfile1", "foo")
writefile("subdir/testfile2", "bar")
mkdir("subdir/testdir1")
writefile("subdir/testdir1/subfile1", "baz")
writefile("subdir/testdir1/subfile2", "quux")

check(mtn("setup", "--branch=testbranch", "subdir"), 0, false, false)

-- Make sure that "add ." works, even at the root of the tree
chdir("subdir")
-- Recursive and non-recursive may process things differently, check
-- that both return success
check(mtn("add", "."), 0, false, false)
check(mtn("add", "-R", "."), 0, false, false)

-- Make sure that it took
check(mtn("commit", "--message=foo"), 0, false, false)
chdir("..")

remove("subdir/testfile1")
remove("subdir/testfile2")
remove("subdir/testdir1")
chdir("subdir")
check(mtn("revert", "."), 0, false, false)
chdir("..")

check(exists("subdir/testfile1"))
check(exists("subdir/testfile2"))
check(exists("subdir/testdir1/subfile1"))
check(exists("subdir/testdir1/subfile2"))
