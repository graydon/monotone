
mtn_setup()

-- This test is a bug report
-- I think the problem is just generally that revert does not do a good
-- job cleaning up after renames?

mkdir("workspace")
check(indir("workspace", mtn("setup", ".", "-b", "testbranch")), 0, false, false)

mkdir("workspace/dir1")
mkdir("workspace/dir1/dir2")
writefile("workspace/dir1/file1", "blah blah")
mkdir("workspace/dir3")
mkdir("workspace/dir3/_MTN")
check(indir("workspace", mtn("add", "-R", ".")), 0, false, false)

check(indir("workspace", mtn("commit", "-m", "foo")), 0, false, false)

check(indir("workspace", mtn("pivot_root", "dir1", "old_root")), 0, false, false)

check(isdir("workspace/_MTN"))
check(isdir("workspace/dir2"))
check(exists("workspace/file1"))
check(isdir("workspace/old_root"))
check(isdir("workspace/old_root/dir3"))
check(isdir("workspace/old_root/dir3/_MTN"))

check(indir("workspace", mtn("ls", "missing")), 0)
check(indir("workspace", mtn("ls", "unknown")), 0)

check(indir("workspace", mtn("revert", ".")), 0, false, false)

check(isdir("workspace/_MTN"))
check(isdir("workspace/dir1"))
check(isdir("workspace/dir1/dir2"))
check(exists("workspace/dir1/file1"))
check(isdir("workspace/dir3"))
check(isdir("workspace/dir3/_MTN"))

check(indir("workspace", mtn("ls", "changed")), 0)
check(indir("workspace", mtn("ls", "missing")), 0)
xfail_if(true, indir("workspace", mtn("ls", "unknown")), 0)
