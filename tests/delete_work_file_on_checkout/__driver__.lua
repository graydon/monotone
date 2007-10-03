
mtn_setup()

addfile("testfile0", "version 0 of first test file\n")
writefile("testfile1", "version 1 of second test file\n")
commit()
v1 = base_revision()
addfile("testfile1")
check(qgrep("add_file","_MTN/revision"))
remove("_MTN")
remove("testfile0")
check(mtn("checkout", "--revision", v1, "."), 0, false, false)
check(not qgrep("add_file","_MTN/revision"))
