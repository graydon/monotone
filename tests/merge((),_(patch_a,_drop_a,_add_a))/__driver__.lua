
mtn_setup()

writefile("v1a", "foo blah")
writefile("v1b", "bar blah")
writefile("v2a", "baz blah")

copyfile("v1a", "testfile")
check(cmd(mtn("add", "testfile")), 0, false, false)
commit()
base = base_revision()

copyfile("v1b", "testfile")
commit()

remove("testfile")
check(cmd(mtn("drop", "testfile")), 0, false, false)
commit()

copyfile("v2a", "testfile")
check(cmd(mtn("add", "testfile")), 0, false, false)
commit()

revert_to(base)

addfile("otherfile", "this space for rent")
commit()

check(cmd(mtn("merge")), 0, false, false)
check(cmd(mtn("update")), 0, false, false)
check(samefile("testfile", "v2a"))
