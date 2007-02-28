
mtn_setup()

writefile("v1", "foo blah")
writefile("v2", "baz blah")

addfile("randomfile", "blah blah blah")
commit()
base = base_revision()

copy("v1", "testfile")
check(mtn("add", "testfile"), 0, false, false)
commit()

remove("testfile")
check(mtn("drop", "--bookkeep-only", "testfile"), 0, false, false)
commit()

copy("v2", "testfile")
check(mtn("add", "testfile"), 0, false, false)
commit()

revert_to(base)
remove("testfile")

addfile("otherfile", "this space for rent")
commit()

check(mtn("merge"), 0, false, false)
check(mtn("update"), 0, false, false)
check(samefile("testfile", "v2"))
