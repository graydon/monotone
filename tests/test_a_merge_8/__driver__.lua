
mtn_setup()

-- This tests a real world case from PostgreSQL

-- kdiff3 and merge(1) can handle this merge correctly.

check(get("parent"))
check(get("left"))
check(get("right"))
check(get("correct"))

copy("parent", "testfile")
check(mtn("add", "testfile"), 0, false, false)
commit()
parent = base_revision()

copy("left", "testfile")
commit()

revert_to(parent)

copy("right", "testfile")
commit()

check(mtn("--branch=testbranch", "merge"), 0, false, false)

check(mtn("update"), 0, false, false)

-- was [bug #18989] merge failure
check(samefile("testfile", "correct"))

