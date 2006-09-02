
mtn_setup()

addfile("testfile0", "version 0 of first test file\n")
writefile("testfile1", "version 1 of second test file\n")
commit()
v1 = base_revision()
addfile("testfile1")
check(exists("_MTN/work"))
remove("_MTN")
check(mtn("checkout", "--revision", v1, "."), 0, false, false)
check(not exists("_MTN/work"))
