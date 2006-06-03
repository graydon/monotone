
mtn_setup()

-- This test relies on file-suturing

getfile("left", "testfile")
addfile("testfile")
commit()
left = base_revision()

remove_recursive("_MTN")
check(mtn("setup", "--branch=testbranch", "."), 0, false, false)

getfile("right", "testfile")
addfile("testfile")
commit()
right = base_revision()

xfail_if(true, mtn("--branch=testbranch", "merge"), 0, false, false)
AT_CHECK(mtn("update"), 0, false, false)

check(samefile("left", "testfile") or samefile("right", "testfile"))
