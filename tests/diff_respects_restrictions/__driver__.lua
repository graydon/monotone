
mtn_setup()
revs = {}

addfile("file1", "1: data 1")
addfile("file2", "2: data 1")
commit()
revs[1] = base_revision()

writefile("file1", "1: data 2")
writefile("file2", "2: data 2")
commit()
revs[2] = base_revision()

writefile("file1", "1: data 3")
writefile("file2", "2: data 3")

check(mtn("diff", "file1"), 0, true, false)
check(qgrep("file1", "stdout"))
check(not qgrep("file2", "stdout"))

check(mtn("diff", "--revision", revs[1], "file1"), 0, true, false)
check(qgrep("file1", "stdout"))
check(not qgrep("file2", "stdout"))

check(mtn("diff", "--revision", revs[1], "--revision", revs[2], "file1"), 0, true, false)
check(qgrep("file1", "stdout"))
check(not qgrep("file2", "stdout"))

check(mtn("diff", "--revision", revs[2], "--revision", revs[1], "file1"), 0, true, false)
check(qgrep("file1", "stdout"))
check(not qgrep("file2", "stdout"))
