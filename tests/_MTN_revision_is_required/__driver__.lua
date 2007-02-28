
mtn_setup()

addfile("foo", "blah blah")
addfile("bar", "blah blah")
remove("foo")
check(mtn("ls", "missing"), 0, false, false)
check(mtn("drop", "--bookkeep-only", "foo"), 0, false, false)
check(mtn("ls", "missing"), 0, false, false)
commit()

remove("bar")
check(mtn("ls", "missing"), 0, false, false)

remove("_MTN/revision")
check(mtn("ls", "missing"), 1, false, false)
