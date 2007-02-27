
mtn_setup()

-- This test relies on file-suturing

writefile("right_1_a", "foo blah")
writefile("right_1_b", "bar blah")
writefile("right_2_a", "baz blah")

writefile("left", "quux blah")

addfile("otherfile", "this space for rent")
commit()
base = base_revision()

copy("right_1_a", "testfile")
check(mtn("add", "testfile"), 0, false, false)
commit()

copy("right_1_b", "testfile")
commit()

remove("testfile")
check(mtn("drop", "--bookkeep-only", "testfile"), 0, false, false)
commit()

copy("right_2_a", "testfile")
check(mtn("add", "testfile"), 0, false, false)
commit()

revert_to(base)

copy("left", "testfile")
check(mtn("add", "testfile"), 0, false, false)
commit()

xfail_if(true, mtn("merge"), 0, false, false)
check(mtn("update"), 0, false, false)
check(samefile("testfile", "right_2_a") or samefile("testfile", "left"))
