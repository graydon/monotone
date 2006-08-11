
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

check(mtn("--branch=testbranch", "checkout", "test_dir1"),
         1, false, true)
check(qgrep(REV2, "stderr"))
check(qgrep(REV3, "stderr"))
