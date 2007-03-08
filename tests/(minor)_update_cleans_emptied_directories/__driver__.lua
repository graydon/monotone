
mtn_setup()

mkdir("testdir")
addfile("testdir/foo", "blah blah blah")
commit()
base = base_revision()

remove("testdir")
check(mtn("drop", "--bookkeep-only", "testdir/foo", "testdir"), 0, false, false)
commit()

revert_to(base)

check(exists("testdir"))
check(mtn("update"), 0, false, false)

check(base ~= base_revision())

check(not exists("testdir"))
