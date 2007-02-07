
mtn_setup()

mkdir("subdir")
writefile("subdir/file1", "111")

-- make sure this works too, since it has failed in the past
check(mtn("add", "-R", "."), 0, false, false)

-- simulate shell glob of "*" from workspace root dir
check(mtn("add", "-R", "_MTN", "subdir"), 0, false, false)

-- simulate shell glob of "../*" from workspace subdir
check(indir("subdir", mtn("add", "-R", "../_MTN", "../subdir")), 0, false, false)
