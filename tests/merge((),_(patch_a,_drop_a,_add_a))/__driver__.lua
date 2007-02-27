
mtn_setup()

writefile("v1a", "foo blah")
writefile("v1b", "bar blah")
writefile("v2a", "baz blah")

copy("v1a", "testfile")
check(mtn("add", "testfile"), 0, false, false)
commit()
base = base_revision()

copy("v1b", "testfile")
commit()

remove("testfile")
check(mtn("drop", "--bookkeep-only", "testfile"), 0, false, false)
commit()

copy("v2a", "testfile")
check(mtn("add", "testfile"), 0, false, false)
commit()

revert_to(base)

addfile("otherfile", "this space for rent")
commit()

check(mtn("merge"), 0, false, false)
check(mtn("update"), 0, false, false)
check(samefile("testfile", "v2a"))
