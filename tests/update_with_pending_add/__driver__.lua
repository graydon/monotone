
mtn_setup()

-- This test relies on file-suturing

addfile("file", "file")

commit()

check(mtn("--branch", "testbranch", "co", "codir"), 0, false, false)

addfile("file2", "file2")

commit()

writefile("codir/file2", "file2")
check(indir("codir", mtn("add", "file2")), 0, false, false)
xfail_if(true, indir("codir", mtn("update")), 0, false, false)

-- make sure there are no changes in the workspace

check(indir("codir", mtn("diff")), 0, true, false)
check(grep('no changes', "stdout"), 0, false, false)
