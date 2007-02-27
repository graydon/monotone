
mtn_setup()

writefile("v1a", "foo blah")
writefile("v1b", "bar blah")
writefile("v2a", "baz blah")

copy("v1a", "testfile")
check(mtn("add", "testfile"), 0, false, false)
addfile("renamefile", "this will be overwritten")
check(mtn("add", "renamefile"), 0, false, false)
commit()
base = base_revision()

remove("testfile")
check(mtn("drop", "--bookkeep-only", "testfile"), 0, false, false)
check(mtn("rename", "--bookkeep-only", "renamefile", "testfile"), 0, false, false)
rename("renamefile", "testfile")
commit()

copy("v2a", "testfile")
commit()

revert_to(base)

addfile("otherfile", "this space for rent")
commit()

check(mtn("merge"), 0, false, false)
check(mtn("update"), 0, false, false)
check(samefile("testfile", "v2a"))
