
mtn_setup()

addfile("file0", "file 0")
addfile("file1", "file 1")
addfile("file2", "file 2")
addfile("file3", "file 3")

writefile("file2", "a change")
writefile("file3", "a change")

check(mtn("drop", "file0"), 0, false, false)
check(not exists("file0"))

check(mtn("drop", "--bookkeep-only", "file1"), 0, false, false)
check(exists("file1"))

check(mtn("drop", "file2"), 0, false, true)
check(qgrep("file file2 changed", "stderr"))
check(exists("file2"))

check(mtn("drop", "--bookkeep-only", "file3"), 0, false, false)
check(exists("file3"))
