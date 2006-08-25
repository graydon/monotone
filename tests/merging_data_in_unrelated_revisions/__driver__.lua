
mtn_setup()

-- This test relies on file-suturing

check(get("left", "testfile"))
addfile("testfile")
commit()
left = base_revision()

remove("_MTN")
check(mtn("setup", "--branch=testbranch", "."), 0, false, false)

check(get("right", "testfile"))
addfile("testfile")
commit()
right = base_revision()

xfail_if(true, mtn("--branch=testbranch", "merge"), 0, false, false)
AT_CHECK(mtn("update"), 0, false, false)

check(samefile("left", "testfile") or samefile("right", "testfile"))
