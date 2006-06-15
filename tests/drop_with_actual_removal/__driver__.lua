
mtn_setup()

addfile("file0", "file 0")
addfile("file1", "file 1")
addfile("file2", "file 2")

check(mtn("drop", "file0"), 0, false, false)
check(mtn("drop", "--execute", "file1"), 0, false, false)
check(mtn("drop", "-e", "file2"), 0, false, false)

check(exists("file0"))
check(not exists("file1"))
check(not exists("file2"))
