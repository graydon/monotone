
mtn_setup()

-- possible problems:
--   -- the new root doesn't exist
--   -- the new root is not a dir
--   -- the new root has an _MTN in it
--   -- the directory the old root is supposed to end up in doesn't exist
--   -- the directory the old root is supposed to end up in is not a directory
--   -- the directory the old root is supposed to end up in already
--      contains something with the given name
-- then make sure --execute puts things in the right place...

mkdir("workspace")
check(indir("workspace", mtn("setup", ".", "-b", "testbranch")), 0, false, false)

mkdir("workspace/dir1")
mkdir("workspace/dir1/dir2")
writefile("workspace/dir1/file1", "blah blah")
mkdir("workspace/dir3")
mkdir("workspace/dir3/_MTN")
check(indir("workspace", mtn("add", "-R", ".")), 0, false, false)

check(indir("workspace", mtn("commit", "-m", "foo")), 0, false, false)

check(indir("workspace", mtn("pivot_root", "--bookkeep-only", "nosuchdir", "foo")), 1, false, false)
check(indir("workspace", mtn("pivot_root", "--bookkeep-only", "dir1/file1", "foo")), 1, false, false)
check(indir("workspace", mtn("pivot_root", "--bookkeep-only", "dir3", "old_root")), 1, false, false)
check(indir("workspace", mtn("pivot_root", "--bookkeep-only", "dir1", "nosuchdir/old_root")), 1, false, false)
check(indir("workspace", mtn("pivot_root", "--bookkeep-only", "dir1", "file1/old_root")), 1, false, false)
check(indir("workspace", mtn("pivot_root", "--bookkeep-only", "dir1", "dir2")), 1, false, false)

check(indir("workspace", mtn("ls", "changed")), 0)
check(indir("workspace", mtn("ls", "missing")), 0)
check(indir("workspace", mtn("ls", "unknown")), 0)

check(indir("workspace", mtn("pivot_root", "dir1", "old_root")), 0, false, false)

check(isdir("workspace/_MTN"))
check(isdir("workspace/dir2"))
check(exists("workspace/file1"))
check(isdir("workspace/old_root"))
check(isdir("workspace/old_root/dir3"))
check(isdir("workspace/old_root/dir3/_MTN"))

check(indir("workspace", mtn("ls", "missing")), 0)
check(indir("workspace", mtn("ls", "unknown")), 0)

check(indir("workspace", mtn("commit", "-m", "foo")), 0, false, false)
