
mtn_setup()

writefile("v1", "foo blah")
writefile("v2", "baz blah")

addfile("randomfile", "blah blah blah")
commit()
base = base_revision()

copyfile("v1", "testfile")
check(cmd(mtn("add", "testfile")), 0, false, false)
commit()

remove("testfile")
check(cmd(mtn("drop", "testfile")), 0, false, false)
commit()

copyfile("v2", "testfile")
check(cmd(mtn("add", "testfile")), 0, false, false)
commit()

revert_to(base)

addfile("otherfile", "this space for rent")
commit()

check(cmd(mtn("merge")), 0, false, false)
check(cmd(mtn("update")), 0, false, false)
check(samefile("testfile", "v2"))
