
mtn_setup()

-- This test relies on file-suturing

writefile("right_1_a", "foo blah")
writefile("right_1_b", "bar blah")
writefile("right_2_a", "baz blah")

writefile("left", "quux blah")

addfile("otherfile", "this space for rent")
commit()
base = base_revision()

copyfile("right_1_a", "testfile")
check(cmd(mtn("add", "testfile")), 0, false, false)
commit()

copyfile("right_1_b", "testfile")
commit()

remove("testfile")
check(cmd(mtn("drop", "testfile")), 0, false, false)
commit()

copyfile("right_2_a", "testfile")
check(cmd(mtn("add", "testfile")), 0, false, false)
commit()

revert_to(base)

copyfile("left", "testfile")
check(cmd(mtn("add", "testfile")), 0, false, false)
commit()

xfail_if(true, cmd(mtn("merge")), 0, false, false)
check(cmd(mtn("update")), 0, false, false)
check(samefile("testfile", "right_2_a") or samefile("testfile", "left"))
