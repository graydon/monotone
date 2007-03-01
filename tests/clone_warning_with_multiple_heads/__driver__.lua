
mtn_setup()

addfile("testfile", "foo")
commit()
REV1=base_revision()

addfile("file2", "bar")
commit()
REV2=base_revision()

check(mtn("update", "-r", REV1), 0, false, false)
addfile("otherfile", "splork")
commit()
REV3=base_revision()

testURI="file:" .. test.root .. "/test.db"

check(nodb_mtn("--branch=testbranch", "clone", testURI, "test_dir1"),
         1, false, true)
check(not exists("test_dir1"))
check(qgrep(REV2, "stderr"))
check(qgrep(REV3, "stderr"))
