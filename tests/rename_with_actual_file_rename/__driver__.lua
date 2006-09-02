
mtn_setup()

addfile("file0", "file 0")
addfile("file1", "file 1")
addfile("file2", "file 2")

check(mtn("rename", "file0", "newfile0"), 0, false, false)
check(mtn("rename", "--execute", "file1", "newfile1"), 0, false, false)
check(mtn("rename", "-e", "file2", "newfile2"), 0, false, false)

check(exists("file0"))
check(not exists("file1"))
check(not exists("file2"))
check(not exists("newfile0"))
check(exists("newfile1"))
check(exists("newfile2"))
