
mtn_setup()

-- In this case, the patch should be completely ignored; we shouldn't
-- even try to do a merge.

writefile("base", "foo blah")
writefile("left", "bar blah")
writefile("new_right", "baz blah")

copy("base", "testfile")
check(mtn("add", "testfile"), 0, false, false)
commit()
base = base_revision()

copy("left", "testfile")
commit()

revert_to(base)

remove("testfile")
check(mtn("drop", "--bookkeep-only", "testfile"), 0, false, false)
commit()

copy("new_right", "testfile")
check(mtn("add", "testfile"), 0, false, false)
commit()

check(mtn("merge"), 0, false, false)
check(mtn("update"), 0, false, false)
check(samefile("testfile", "new_right"))
