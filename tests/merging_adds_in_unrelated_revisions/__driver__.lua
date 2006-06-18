
mtn_setup()

-- This test relies on file-suturing
-- AT_XFAIL_IF(true)

addfile("testfile1", "This is test file 1\n")
commit()
left = base_revision()

remove("_MTN")
check(mtn("setup", "--branch=testbranch", "."), 0, false, false)

addfile("testfile2", "This is test file 2\n")
commit()
right = base_revision()

xfail_if(true, mtn("--branch=testbranch", "merge"), 0, false, false)
check(mtn("update"), 0, false, false)

writefile("expected_data1", "This is test file 1\n")
writefile("expected_data2", "This is test file 2\n")

check(samefile("testfile1", "expected_data1"))
check(samefile("testfile2", "expected_data2"))
