
mtn_setup()

addfile("file0", "file 0")
addfile("file1", "file 1")

check(mtn("rename", "--bookkeep-only", "file0", "newfile0"), 0, false, false)
check(mtn("rename", "file1", "newfile1"), 0, false, false)

check(exists("file0"))
check(not exists("file1"))
check(not exists("newfile0"))
check(exists("newfile1"))
