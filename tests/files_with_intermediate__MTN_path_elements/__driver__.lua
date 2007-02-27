
mtn_setup()

mkdir("dir1")
mkdir("dir1/_MTN")
mkdir("dir2")
mkdir("dir3")

-- Check both implicit recursive add...
writefile("dir1/_MTN/testfile1", "testfile 1")
writefile("dir2/_MTN", "_MTN file 1")
check(mtn("add", "-R", "dir1"), 0, false, false)
check(mtn("add", "-R", "dir2"), 0, false, false)
commit()

-- ...and explicit add.
writefile("dir1/_MTN/testfile2", "testfile 2")
writefile("dir3/_MTN", "_MTN file 2")
check(mtn("add", "dir1/_MTN/testfile2"), 0, false, false)
check(mtn("add", "dir3/_MTN"), 0, false, false)
commit()

check(mtn("checkout", "outdir1"), 0, false, false)
check(samefile("dir1/_MTN/testfile1", "outdir1/dir1/_MTN/testfile1"))
check(samefile("dir1/_MTN/testfile2", "outdir1/dir1/_MTN/testfile2"))
check(samefile("dir2/_MTN", "outdir1/dir2/_MTN"))
check(samefile("dir3/_MTN", "outdir1/dir3/_MTN"))
 
-- renames

check(mtn("rename", "dir1/_MTN/testfile1", "dir1/_MTN/testfile1x"), 0, false, false)
check(mtn("rename", "dir2/_MTN", "dir2/TM"), 0, false, false)
check(mtn("rename", "dir3", "dir3x"), 0, false, false)
commit()

check(mtn("checkout", "outdir2"), 0, false, false)
check(samefile("dir1/_MTN/testfile1x", "outdir2/dir1/_MTN/testfile1x"))
check(samefile("dir1/_MTN/testfile2", "outdir2/dir1/_MTN/testfile2"))
check(samefile("dir2/TM", "outdir2/dir2/TM"))
check(samefile("dir3x/_MTN", "outdir2/dir3x/_MTN"))

-- explicit drop

check(mtn("drop", "--bookkeep-only", "dir1/_MTN/testfile2"), 0, false, false)
commit()

check(mtn("checkout", "outdir3"), 0, false, false)
check(samefile("dir1/_MTN/testfile1x", "outdir2/dir1/_MTN/testfile1x"))
check(not exists("outdir3/dir1/_MTN/testfile2"))

-- recursive drop

check(mtn("drop", "--bookkeep-only", "--recursive", "dir1"), 0, false, false)
commit()

check(mtn("checkout", "outdir4"), 0, false, false)
check(not exists("outdir4/dir1/_MTN/testfile1x"))
check(not exists("outdir4/dir1/_MTN/testfile2"))
check(not exists("outdir4/dir1/_MTN"))
check(not exists("outdir4/dir1"))
